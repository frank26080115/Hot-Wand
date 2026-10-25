// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "fault_test.h"
#include "hotwand.h"
#include "adc.h"
#include "adc_sim.h"
#include "battery.h"
#include "button.h"
#include "conf.h"
#include "fan.h"
#include "fault.h"
#include "miscutils.h"
#include "nvm.h"
#include "oled.h"
#include "pwrmgt.h"
#include "pwrlvl.h"
#include "rfgen.h"
#include "setup_menu.h"
#include "stm32f0xx_hal.h"
#include "systick.h"
#include "tipdetect.h"
#include "uart_debug.h"
#include "tests.h"
#include "watchdog.h"

#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#if !defined(HOT_WAND_TARGET_STM32F030) && !defined(HOT_WAND_TARGET_STM32F042)
#error "This source supports only the STM32F030 and STM32F042 targets."
#endif

#define SETUP_HOLD_BAR_WIDTH             32
#define SETUP_HOLD_BAR_HEIGHT            3
#define SETUP_HOLD_BAR_Y                 45
#define MAIN_DISPLAY_FRAME_INTERVAL_MS   67
#define DEBUG_DISPLAY_FRAME_INTERVAL_MS  200
#define MAIN_DISPLAY_VOLTAGE_BUFFER_SIZE 8

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static const uint32_t auto_sleep_delay_ms[] = {0, 5 * 60 * 1000, 15 * 60 * 1000, 30 * 60 * 1000};
static const uint32_t auto_dim_delay_ms[]   = {0, 15 * 1000, 30 * 1000, 60 * 1000};

static uint32_t debug_readings[5];
static uint32_t debug_peaks[5];
static bool     debug_entry_release_pending;
static bool     debug_entry_release_timing;
static uint32_t debug_entry_release_started_ms;
static bool     debug_view_enabled;
static uint8_t  debug_press_count;
static uint32_t debug_last_press_ms;

#ifdef SHOW_SPLASH
static uint32_t splash_started_ms;
static bool     splash_visible;
#endif

uint8_t pixshift_x = 0; // randomized pixel shift to avoid OLED burn-in
uint8_t pixshift_y = 0;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void boot_check_for_setup(void);
static void boot_draw_setup_hold(u8g2_t* graphics, uint8_t bar_width);
static void boot_wait_for_power_stable(void);
static void __attribute__((noinline)) main_render_display(void);
static void main_handle_short_press(uint32_t now);
static void main_render_debug_view(u8g2_t* graphics, uint16_t input_millivolts);
static void main_update_debug_readings(void);
static void main_reset_debug_peaks(void);
static void main_handle_runtime_button(uint32_t now);
static void Error_Handler(void);

void enter_sleep_mode(void);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

int main(void)
{
#if !BTN_SWD_TEST_HARNESS
    test_run(); // if a test is enabled, it does its own initialization and never returns
#endif

    battery_guess_t     battery_guess_result;
    hotwand_setup_nvm_t settings;
    uint32_t            random_seed;
    uint32_t            display_last_frame_ms;
    uint16_t            input_millivolts;
    bool                boot_button_down;

    // HAL provides the reset-clock tick for the RCC timeout and optional test boot gate.
    // After test arming, select HSE before starting ordinary acquisition.
    HAL_Init();

    /* Establish the fail-low RF state before starting the watchdog. */
    rfgen_stop();
#if HOT_WAND_TEST_HARNESS
    /* Establish minimum buck output even if watchdog startup fails. */
    pwrlvl_force_minimum();
#endif
    if (!watchdog_init())
    {
        Error_Handler();
    }

#if ADC_SIM_ENABLED
    fault_test_init();
    /* Wait before any ADC or output activity. The host enqueues complete
     * frames, then commits start_request = 1
     * through SWD. No target calls or
     * long debugger halt are needed; IWDG stays serviced during the wait. */
    pwrlvl_force_minimum();
    adc_sim_init();
    while (adc_sim.state == ADC_SIM_WAITING)
    {
        if (adc_sim.start_request != 0)
        {
            adc_sim_start();
            adc_sim.start_request = 0;
        }
        watchdog_feed();
        HAL_Delay(1);
    }
#endif
    rfgen_clock_init();
    systick_init();
    FT_SET(stage, FT_ADC);
    adc_init();
    btn_init();

    FT_SET(stage, FT_OLED);
    I2C1_Init();
    if (fault_test_inject(FT_OLED_INIT) || !OLED_Init(&oled, &i2c1))
    {
        Error_Handler();
    }
    OLED_ConfigureGraphics(&oled);

#if defined(HOT_WAND_VISION_TEST) && HOT_WAND_VISION_TEST
    /* Framebuffer capture is always available in the test image. The host
     * requests this optional camera target
     * at the ADC boot gate; fault tests
     * use the reset default (zero) and proceed without a camera or dismissal.
     */
    if (vision_test_fiducial_enabled != 0)
    {
        vision_test_fiducial_gate();
    }
#endif

    nvm_init();
    if (!nvm_read(&settings))
    {
        nvm_apply_defaults(&settings);
    }
#if HOT_WAND_TEST_HARNESS
    if ((fault_test.setup & 0x80000000) != 0)
    {
        settings.batt_mode = (fault_test.setup >> 8) & 7;
        settings.startup_power_level = (fault_test.setup >> 11) & 3;
        settings.input_v_calib = (fault_test.setup >> 13) & 15;
        settings.auto_sleep = (fault_test.setup >> 17) & 3;
        settings.auto_dim = (fault_test.setup >> 19) & 3;
    }
#endif
    adc_set_input_voltage_calibration(settings.input_v_calib);

    // when all ADC channels have been read once
    // we use the LSB of the data to use as a RNG seed
    // taking advantage of noise
    /* Deliberately do not feed here: a DMA/ADC path that never produces a
     * usable seed is a startup fault and
     * should be recovered by IWDG. */
    do
    {
        random_seed = adc_get_rand_seed();
    } while (random_seed == 0);
    hotwand_srand(random_seed);

#ifdef SHOW_SPLASH
    if (settings.show_splash)
    {
        show_splash();
        splash_started_ms = systick_get_ms();
        splash_visible    = true;
    }
#endif

    pixshift_x = hotwand_rand() % (OLED_MAX_PIXEL_SHIFT_X_BIGFONT + 1);
    pixshift_y = hotwand_rand() % (OLED_MAX_PIXEL_SHIFT_Y + 1);

    boot_button_down = btn_is_down();
    UART_SetAllowed(boot_button_down);
    if (boot_button_down)
    {
        boot_check_for_setup();
        // this might just reboot
    }

    boot_wait_for_power_stable();

    input_millivolts = adc_to_millivolts(DC_SENS_IDX);

    if ((settings.batt_mode != BATT_MODE_NONE) && (settings.batt_mode <= BATT_MODE_LIFE_SAFE))
    {
        // automatic cell count determination
        if (battery_guess(input_millivolts, settings.batt_mode, &battery_guess_result))
        {
            battery_set_params(battery_guess_result.optimistic_cell_count, settings.batt_mode);
        }
    }
    else
    {
        // 0 cell will disable battery level monitoring
        battery_set_params(0, BATT_MODE_NONE);
    }

#if HOT_WAND_TEST_HARNESS
    if (((fault_test.setup & 0x80000000) != 0) && ((fault_test.setup & 255) != 0))
    {
        battery_set_params(fault_test.setup & 255, settings.batt_mode);
    }
#endif
    /* perform all other initialization here */
    tipdetect_init();
    pwrlvl_init();
    pwrmgt_set_desired_power_level((pwrlvl_mode_t)settings.startup_power_level);
    pwrmgt_set_idle_power_threshold(settings.idle_detect_thresh);
    settings.fan_mode = fan_normalize_mode(settings.fan_mode);
    fan_init(settings.fan_mode, settings.fan_sig_inv != 0);
#if ADC_SIM_ENABLED
    /* Resume near-full attenuation, then let the normal supervisor regulate
     * and decide whether RF may start.
     * This does not bypass any interlock. */
    pwrlvl_release_minimum();
#endif
    display_last_frame_ms = systick_get_ms() - MAIN_DISPLAY_FRAME_INTERVAL_MS;

    for (;;)
    {
        uint32_t inactive_ms;
        uint32_t now;
        bool     short_msg_active;

        fault_test_task();
        btn_task();
        tipdetect_task();

        // Both faults are latched and stop the RF generator at their source. Their screens are terminal until the user
        // requests a reset.
        if (tipdetect_has_triggered())
        {
            show_fault("TIP\nFAULT", true);
        }
        if (rfgen_has_clock_fault())
        {
            show_fault("CLOCK\nFAULT", true);
        }

        // Central runtime output supervisor:
        // - Checks the configured battery cutoff, input-power loss, buck-output overvoltage, recoverable input
        //   undervoltage, and sustained overtemperature shutdown.
        // - Applies temperature and input-voltage derating with hysteresis.
        // - Selects the effective power level and advances PWM attenuation.
        // - Enforces current and short-circuit limits in the lower-level task.
        // - Updates attenuation diagnostics, activity timing, and graph history.
        // A terminal fault blocks here until the user resets the controller.
        pwrmgt_task();

        // Retry after supervision has ruled out terminal power faults. The RF driver performs its own clock-fault and
        // tip-debounce checks too.
        if (!rfgen_is_active() && rfgen_tip_allows_start())
        {
            rfgen_start();
        }

        ntc_task();        // reports missing external temperature sensors once after startup
        fan_task();        // spins the fan as appropriate
        UART_debug_task(); // spits out a debug message when requested

        now = systick_get_ms();

        main_handle_runtime_button(now);

        inactive_ms = pwrmgt_get_time_since_last_activity_ms();

        if (!debug_view_enabled && (settings.auto_sleep != AUTO_SLEEP_OFF) &&
            (inactive_ms >= auto_sleep_delay_ms[settings.auto_sleep]))
        {
            enter_sleep_mode();
        }

        OLED_SetDimMode(&oled,
                        (settings.auto_dim != AUTO_DIM_OFF) && (inactive_ms >= auto_dim_delay_ms[settings.auto_dim]));

        // Transient messages own the display while active. Refresh the graph at up to
        // 15 FPS and debug numbers at 5 FPS; peak sampling remains independent.
        short_msg_active = short_msg_task();

#ifdef SHOW_SPLASH
        if (short_msg_active)
        {
            splash_visible = false;
        }
#endif

        if (!short_msg_active &&
            ((uint32_t)(now - display_last_frame_ms) >=
             (debug_view_enabled ? DEBUG_DISPLAY_FRAME_INTERVAL_MS : MAIN_DISPLAY_FRAME_INTERVAL_MS)))
        {
            display_last_frame_ms = now;
            main_render_display();
        }

        /* Feed only after the complete supervised foreground pass succeeds. */
#if HOT_WAND_TEST_HARNESS
        fault_test_snapshot();
        fault_test.stage = FT_RUNTIME;
#endif
        watchdog_feed();
    }
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

static void boot_check_for_setup(void)
{
    u8g2_t*  graphics = OLED_GetGraphics(&oled);
    uint32_t hold_start_ms;
    uint32_t release_start_ms = 0;
    uint8_t  drawn_width      = 0;
    bool     release_pending  = false;

    if (fault_test_inject(FT_GRAPHICS))
    {
        graphics = NULL;
    }
    if (graphics == NULL)
    {
        Error_Handler();
    }

#ifdef SHOW_SPLASH
    splash_visible = false;
#endif

    boot_draw_setup_hold(graphics, 0);
    hold_start_ms = systick_get_ms();

    for (;;)
    {
        uint32_t elapsed;
        uint32_t now;
        uint8_t  width;

        btn_task();
        now = systick_get_ms();

        if (!btn_is_down())
        {
            if (!release_pending)
            {
                release_start_ms = now;
                release_pending  = true;
            }
            else if ((uint32_t)(now - release_start_ms) >= BTN_DEBOUNCE_MS)
            {
                u8g2_ClearBuffer(graphics);
                OLED_SendBuffer(&oled);
                btn_has_short_press(true);
                btn_has_long_press(true);
                return;
            }

            HAL_Delay(1);
            watchdog_feed();
            continue;
        }

        release_pending = false;
        elapsed         = (uint32_t)(now - hold_start_ms);
        if (elapsed >= SETUP_HOLD_DURATION_MS)
        {
            width = SETUP_HOLD_BAR_WIDTH;
        }
        else
        {
            width = (uint8_t)((elapsed * SETUP_HOLD_BAR_WIDTH) / SETUP_HOLD_DURATION_MS);
        }

        if (width != drawn_width)
        {
            boot_draw_setup_hold(graphics, width);
            drawn_width = width;
        }

        if (width >= SETUP_HOLD_BAR_WIDTH)
        {
            setup_menu();
            return;
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

static void boot_wait_for_power_stable(void)
{
    uint16_t maximum_millivolts = adc_to_millivolts(DC_SENS_IDX);
    uint32_t wait_started_ms    = systick_get_ms();
    uint32_t maximum_reached_ms = wait_started_ms;
    bool     wait_message_drawn = false;

    for (;;)
    {
        uint16_t millivolts = adc_to_millivolts(DC_SENS_IDX);
        uint32_t now        = systick_get_ms();

#ifdef SHOW_SPLASH
        btn_task();
        if (splash_visible && btn_has_short_press(true))
        {
            u8g2_t* graphics = OLED_GetGraphics(&oled);

            splash_visible = false;
            if (graphics != NULL)
            {
                u8g2_ClearBuffer(graphics);
                OLED_SendBuffer(&oled);
            }
        }
#endif

        if (millivolts > maximum_millivolts)
        {
            maximum_millivolts = millivolts;
            maximum_reached_ms = now;
        }

        if ((uint32_t)(now - maximum_reached_ms) >= BOOT_POWER_STABLE_MS)
        {
            return;
        }

        if ((uint32_t)(now - wait_started_ms) >= BOOT_POWER_TIMEOUT_MS)
        {
            return;
        }

        if (!wait_message_drawn && ((uint32_t)(now - wait_started_ms) >= BOOT_POWER_WAIT_MS))
        {
            u8g2_t* graphics = OLED_GetGraphics(&oled);

            if (graphics != NULL)
            {
#ifdef SHOW_SPLASH
                if (splash_visible)
                {
                    u8g2_SetDrawColor(graphics, 0);
                    u8g2_DrawBox(graphics, 0, 0, 32, OLED_TEXT_LINE_HEIGHT);
                    u8g2_SetDrawColor(graphics, 1);
                }
                else
#endif
                {
                    u8g2_ClearBuffer(graphics);
                }
                u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE, ".....");
                OLED_SendBuffer(&oled);
            }

            wait_message_drawn = true;
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

static void main_handle_short_press(uint32_t now)
{
#ifdef SHOW_SPLASH
    if (splash_visible)
    {
        splash_visible = false;
        return;
    }
#endif

    pwrmgt_change_pwr_lvl();

    if (debug_view_enabled)
    {
        return;
    }

    // Independent of the six-press Easter egg, including when derating blocks a power change.
    if ((uint32_t)(now - debug_last_press_ms) > BTN_CONSECUTIVE_PRESS_TIMEOUT_MS)
    {
        debug_press_count = 0;
    }
    debug_last_press_ms = now;
    if (++debug_press_count == 10)
    {
        debug_view_enabled          = true;
        debug_press_count           = 0;
        debug_entry_release_pending = true;
        debug_entry_release_timing  = false;
        main_reset_debug_peaks();
    }
}

static void main_reset_debug_peaks(void)
{
    for (uint8_t i = 0; i < 5; ++i)
    {
        debug_peaks[i] = 0;
    }
}

static void main_update_debug_readings(void)
{
    debug_readings[0] = adc_to_millivolts(BUCK_SENS_IDX);
    debug_readings[1] = adc_to_milliamps(CURR_SENS_IDX);
    // Track the calculated power of this sample, independently of voltage/current peaks.
    debug_readings[2] = adc_get_milliwatts();
    debug_readings[3] = adc_to_celcius(THERM_1_IDX);
    debug_readings[4] = adc_to_celcius(THERM_2_IDX);
    for (uint8_t i = 0; i < 5; ++i)
    {
        if (debug_readings[i] > debug_peaks[i])
        {
            debug_peaks[i] = debug_readings[i];
        }
    }
}

static void main_handle_runtime_button(uint32_t now)
{
    if (btn_has_short_press(true))
    {
        main_handle_short_press(now);
    }
    if (debug_view_enabled)
    {
        // Wait for the entry press to finish before changing event classification.
        // Its release must not become an extra short press or reset the Easter-egg counter.
        if (debug_entry_release_pending)
        {
            if (btn_is_down())
            {
                debug_entry_release_timing = false;
            }
            else if (!debug_entry_release_timing)
            {
                debug_entry_release_started_ms = now;
                debug_entry_release_timing     = true;
            }
            else if ((uint32_t)(now - debug_entry_release_started_ms) >= BTN_DEBOUNCE_MS)
            {
                btn_set_short_press_mode(BTN_SHORT_PRESS_ON_RELEASE);
                debug_entry_release_pending = false;
            }
        }
        // Keep sampling even when transient messages own the display. A long hold
        // clears peaks after this sample; the next pass starts accumulating again.
        main_update_debug_readings();
    }
    if (btn_has_long_press(true))
    {
        if (debug_view_enabled)
        {
            main_reset_debug_peaks();
        }
        else
        {
            enter_sleep_mode();
        }
    }
}

static void main_draw_debug_text(u8g2_t* graphics, uint8_t baseline, const char* text)
{
    u8g2_uint_t width     = u8g2_GetStrWidth(graphics, text);
    u8g2_uint_t available = u8g2_GetDisplayWidth(graphics);
    u8g2_uint_t shift     = width < available ? available - width : 0;
    if (shift > pixshift_x)
    {
        shift = pixshift_x;
    }
    u8g2_DrawStr(graphics, shift, baseline + pixshift_y, text);
}

static void main_render_debug_view(u8g2_t* graphics, uint16_t input_millivolts)
{
    char   text[16];
    size_t length;

    u8g2_SetFont(graphics, u8g2_font_5x7_tr);
    text[0] = '>';
    millivolts_to_str(input_millivolts, text + 1, 0, &length);
    length += 1;
    text[length++] = 'V';
    text[length]   = '\0';
    main_draw_debug_text(graphics, 9, text);

    text[0] = 'M';
    text[1] = ':';
    int_to_str(100 - 25 * pwrmgt_get_applied_power_level(), text + 2, 10, NULL);
    main_draw_debug_text(graphics, 17, text);

    for (uint8_t group = 0; group < 2; ++group)
    {
        main_draw_debug_text(graphics, 25 + 48 * group, group == 0 ? "NOW" : "MAX");
        for (uint8_t row = 0; row < 5; ++row)
        {
            uint32_t value = group == 0 ? debug_readings[row] : debug_peaks[row];
            char     unit;
            switch (row)
            {
            case 0:
                millivolts_to_str(value, text, 1, &length);
                unit = 'V';
                break;
            case 1:
                milliamps_to_str(value, text, 1, &length);
                unit = 'A';
                break;
            case 2:
                milliwatts_to_str(value, text, 1, &length);
                unit = 'W';
                break;
            default:
                celcius_to_str(value, text, &length);
                unit = 'C';
                break;
            }
            text[length++] = unit;
            text[length]   = '\0';
            main_draw_debug_text(graphics, 33 + 48 * group + 8 * row, text);
        }
    }
    if ((pwrmgt_get_attenuation_reasons() & PWRMGT_ATTENUATION_TEMPERATURE) != 0 &&
        ((systick_get_ms() / 1000) & 1) != 0)
    {
        main_draw_debug_text(graphics, 121, "!HOT!");
    }
}

static void __attribute__((noinline)) main_render_display(void)
{
    char     voltage[MAIN_DISPLAY_VOLTAGE_BUFFER_SIZE];
    size_t   voltage_length;
    uint16_t input_millivolts;
    u8g2_t*  graphics = OLED_GetGraphics(&oled);

#ifdef SHOW_SPLASH
    if (splash_visible)
    {
        if ((uint32_t)(systick_get_ms() - splash_started_ms) < SHOW_SPLASH_MS)
        {
            return;
        }

        splash_visible = false;
    }
#endif

    if (graphics == NULL)
    {
        return;
    }

    input_millivolts = adc_to_millivolts(DC_SENS_IDX);
    u8g2_ClearBuffer(graphics);
    if (debug_view_enabled)
    {
        main_render_debug_view(graphics, input_millivolts);
    }
    else
    {
        millivolts_to_str(input_millivolts, voltage, 1, &voltage_length);
        voltage[voltage_length++] = 'V';
        voltage[voltage_length]   = '\0';

        u8g2_SetFont(graphics, u8g2_font_6x10_tr);
        /* Alternate voltage and !HOT! in one-second phases while thermal
         * derating is active, including its
         * hysteresis recovery interval. */
        u8g2_DrawStr(graphics,
                     (u8g2_uint_t)pixshift_x,
                     (u8g2_uint_t)(OLED_FIRST_TEXT_BASELINE + pixshift_y),
                     ((pwrmgt_get_attenuation_reasons() & PWRMGT_ATTENUATION_TEMPERATURE) != 0 &&
                      ((systick_get_ms() / 1000) & 1) != 0)
                         ? "!HOT!"
                         : voltage);
        pwrmgt_render_graph(graphics);
    }
    pwrmgt_render_high_voltage_warning(graphics, input_millivolts);
    OLED_SendBuffer(&oled);
}

void enter_sleep_mode(void)
{
    debug_press_count = 0;
    show_fault("ZZZZZ", true);
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

static void boot_draw_setup_hold(u8g2_t* graphics, uint8_t bar_width)
{
    u8g2_ClearBuffer(graphics);
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE, "HOLD");
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + OLED_TEXT_LINE_HEIGHT, "TO");
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + (2 * OLED_TEXT_LINE_HEIGHT), "ENTER");
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + (3 * OLED_TEXT_LINE_HEIGHT), "SETUP");
    u8g2_DrawBox(graphics, 0, SETUP_HOLD_BAR_Y, bar_width, SETUP_HOLD_BAR_HEIGHT);
    OLED_SendBuffer(&oled);
}

// -----------------------------------------------------------------------------
// Debug / Fault Helpers
// -----------------------------------------------------------------------------

static void Error_Handler(void)
{
    /* Leave every power-stage control fail-low, then let IWDG recover. */
    rfgen_emergency_stop();
    __disable_irq();

    /* Deliberately do not feed the watchdog in a fatal handler. */
    for (;;)
    {
    }
}
