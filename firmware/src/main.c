#include "hotwand.h"
#include "adc.h"
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

#include <stddef.h>
#include <stdint.h>

#if !defined(HOT_WAND_TARGET_STM32F030) && !defined(HOT_WAND_TARGET_STM32F042)
#error "This source supports only the STM32F030 and STM32F042 targets."
#endif

#define SETUP_HOLD_BAR_WIDTH 32
#define SETUP_HOLD_BAR_HEIGHT 3
#define SETUP_HOLD_BAR_Y 45
#define MAIN_DISPLAY_FRAME_INTERVAL_MS 67
#define MAIN_DISPLAY_VOLTAGE_BUFFER_SIZE 8
#define MAIN_DISPLAY_VOLTAGE_BASELINE 9

static const uint32_t auto_sleep_delay_ms[] = {0, 5 * 60 * 1000, 15 * 60 * 1000, 30 * 60 * 1000};
static const uint32_t auto_dim_delay_ms[]   = {0, 15 * 1000, 30 * 1000, 60 * 1000};

static void boot_check_for_setup(void);
static void boot_draw_setup_hold(u8g2_t* graphics, uint8_t bar_width);
static void boot_wait_for_power_stable(void);
static void main_render_display(void);
static void Error_Handler(void);

void enter_sleep_mode(void);

uint8_t pixshift_x = 0;
uint8_t pixshift_y = 0;

int main(void)
{
    battery_guess_t     battery_guess_result;
    hotwand_setup_nvm_t settings;
    uint32_t            random_seed;
    uint32_t            display_last_frame_ms;
    uint16_t            input_millivolts;
    bool                boot_button_down;

    /* HAL provides the temporary reset-clock tick needed by the RCC startup
     * timeout.  Application initialization
     * begins with HSE immediately
     * afterward. */
    HAL_Init();

    rfgen_clock_init();
    systick_init();
    adc_init();
    btn_init();

    I2C1_Init();
    if (!OLED_Init(&oled, &i2c1))
    {
        Error_Handler();
    }

    nvm_init();
    if (!nvm_read(&settings))
    {
        nvm_apply_defaults(&settings);
    }
    adc_set_input_voltage_calibration(settings.input_v_calib);

    do
    {
        random_seed = adc_get_rand_seed();
    } while (random_seed == 0);
    hotwand_srand(random_seed);
    pixshift_x = hotwand_rand() & OLED_MAX_PIXEL_SHIFT_X;
    pixshift_y = hotwand_rand() & OLED_MAX_PIXEL_SHIFT_Y;

    boot_button_down = btn_is_down();
    UART_SetAllowed(boot_button_down);
    if (boot_button_down)
    {
        boot_check_for_setup();
    }

    boot_wait_for_power_stable();

    input_millivolts = adc_to_millivolts(DC_SENS_IDX);

    if ((settings.batt_mode != BATT_MODE_NONE) && (settings.batt_mode <= BATT_MODE_LIFE_SAFE))
    {
        if (battery_guess(input_millivolts, settings.batt_mode, &battery_guess_result))
        {
            battery_set_params(battery_guess_result.optimistic_cell_count, settings.batt_mode);
        }
    }
    else
    {
        battery_set_params(0, BATT_MODE_NONE);
    }

    /* perform all other initialization here */
    tipdetect_init();
    pwrlvl_init();
    pwrmgt_set_desired_power_level((pwrlvl_mode_t)settings.startup_power_level);
    pwrmgt_set_idle_power_threshold(settings.idle_detect_thresh);
    fan_init();
    display_last_frame_ms = systick_get_ms() - MAIN_DISPLAY_FRAME_INTERVAL_MS;

    test_run(); // if the test is enabled, then this will never return

    for (;;)
    {
        uint32_t now;

        btn_task();
        tipdetect_task();

        /* Both faults are latched and stop the RF generator at their source.
         * Their screens are terminal
         * until the user requests a reset. */
        if (tipdetect_has_triggered())
        {
            show_fault("TIP\nFAULT", true);
        }
        if (rfgen_has_fault())
        {
            show_fault("CLOCK\nFAULT", true);
        }

        /* Power management owns battery, temperature, and input-voltage
         * supervision.  The lower power-level
         * task applies its decision. */
        pwrmgt_task();
        pwrlvl_task();
        fan_task();
        UART_debug_task();

        now = systick_get_ms();

        if (btn_has_short_press(true))
            pwrmgt_change_pwr_lvl();
        if (btn_has_long_press(true))
            enter_sleep_mode();

        {
            uint32_t inactive_ms = pwrmgt_get_time_since_last_activity_ms();

            if ((settings.auto_sleep != AUTO_SLEEP_OFF) && (inactive_ms >= auto_sleep_delay_ms[settings.auto_sleep]))
            {
                enter_sleep_mode();
            }

            OLED_SetDimMode(
                &oled,
                (settings.auto_dim != AUTO_DIM_OFF) && (inactive_ms >= auto_dim_delay_ms[settings.auto_dim]));
        }

        /* Transient messages own the display while active.  Otherwise draw
         * the live voltage and graph at no
         * more than 15 frames per second. */
        if (!short_msg_task() && ((uint32_t)(now - display_last_frame_ms) >= MAIN_DISPLAY_FRAME_INTERVAL_MS))
        {
            display_last_frame_ms = now;
            main_render_display();
        }
    }
}

static void boot_check_for_setup(void)
{
    u8g2_t*  graphics = OLED_GetGraphics(&oled);
    uint32_t hold_start_ms;
    uint32_t release_start_ms = 0;
    uint8_t  drawn_width      = 0;
    bool     release_pending  = false;

    if (graphics == NULL)
    {
        Error_Handler();
    }

    u8g2_SetDisplayRotation(graphics, U8G2_R1);
    u8g2_SetFont(graphics, u8g2_font_5x7_tr);
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
    }
}

static void boot_draw_setup_hold(u8g2_t* graphics, uint8_t bar_width)
{
    u8g2_ClearBuffer(graphics);
    u8g2_DrawStr(graphics, 1, 9, "HOLD");
    u8g2_DrawStr(graphics, 1, 19, "TO");
    u8g2_DrawStr(graphics, 1, 29, "ENTER");
    u8g2_DrawStr(graphics, 1, 39, "SETUP");
    u8g2_DrawBox(graphics, 0, SETUP_HOLD_BAR_Y, bar_width, SETUP_HOLD_BAR_HEIGHT);
    OLED_SendBuffer(&oled);
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
                u8g2_SetDisplayRotation(graphics, U8G2_R1);
                u8g2_SetFont(graphics, u8g2_font_5x7_tr);
                u8g2_ClearBuffer(graphics);
                u8g2_DrawStr(graphics, 1, 9, ".....");
                OLED_SendBuffer(&oled);
            }

            wait_message_drawn = true;
        }

        HAL_Delay(1);
    }
}

static void main_render_display(void)
{
    char    voltage[MAIN_DISPLAY_VOLTAGE_BUFFER_SIZE];
    char*   end;
    u8g2_t* graphics = OLED_GetGraphics(&oled);

    if (graphics == NULL)
    {
        return;
    }

    millivolts_to_str(adc_to_millivolts(DC_SENS_IDX), voltage, 1);
    end = voltage;
    while (*end != '\0')
    {
        ++end;
    }
    *end++ = 'V';
    *end   = '\0';

    u8g2_SetDisplayRotation(graphics, U8G2_R1);
    u8g2_SetFont(graphics, u8g2_font_5x7_tr);
    u8g2_ClearBuffer(graphics);
    u8g2_DrawStr(graphics, (u8g2_uint_t)pixshift_x, (u8g2_uint_t)(MAIN_DISPLAY_VOLTAGE_BASELINE + pixshift_y), voltage);
    pwrmgt_render_graph(graphics);
    OLED_SendBuffer(&oled);
}

static void Error_Handler(void)
{
    __disable_irq();

    for (;;)
    {
    }
}

void enter_sleep_mode(void)
{
    show_fault("ZZZZZ", true);
}
