// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "tests.h"

#include "adc.h"
#include "battery.h"
#include "button.h"
#include "fan.h"
#if FAN_PWM_ENABLED
#include "fan_pwm.h"
#endif
#include "fault.h"
#include "miscutils.h"
#include "nvm.h"
#include "oled.h"
#include "pins.h"
#include "pwrlvl.h"
#include "rfgen.h"
#include "splash.h"
#include "stm32f0xx_hal.h"
#include "systick.h"
#include "tipdetect.h"
#include "uart_debug.h"
#include "watchdog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define TEST_STATUS_INTERVAL_MS               100
#define TEST_WATCHDOG_FEED_DURATION_MS        5000
#define TEST_RFGEN_BURST_DURATION_MS          1000
/* Set to 1 only when the RF/tip-detection hardware is absent during bring-up. */
#define TEST_RFGEN_BYPASS_TIP_DETECTOR         1
#define TEST_PWRLVL_75_PERCENT_CCR            24
/* PA14 is SWCLK as well as UART TX. Keep UART off for GDB builds. */
#ifndef TEST_PWRLVL_SWEEP_UART
#define TEST_PWRLVL_SWEEP_UART                 0
#endif
#define TEST_PWRLVL_SWEEP_START_DELAY_MS      5000
#define TEST_PWRLVL_SWEEP_STEP_DELAY_MS       1000
#define TEST_PWRLVL_SWEEP_PERIOD_COUNTS       32
#define TEST_PWRLVL_SWEEP_ROW_COUNT           (TEST_PWRLVL_SWEEP_PERIOD_COUNTS + 1)
#define TEST_NVM_VERBOSE_SAVE_COUNT           4
#define TEST_BATTERY_GUESS_MINIMUM_MILLIVOLTS 14000
#define TEST_BATTERY_GUESS_MAXIMUM_MILLIVOLTS 36000
#define TEST_BATTERY_GUESS_STEP_MILLIVOLTS    250

/* CT diagnostic settings; override these with build definitions.
 * 1: hold attenuation low as GPIO (no PWM), with no starting-voltage window.
 * 0: use fixed CCR1 attenuation and the starting-voltage window below.
 * This option affects only test_ct_reaction(); RF interlocks remain active. */
#ifndef TEST_CT_UNATTENUATED
#define TEST_CT_UNATTENUATED 1
#endif
#ifndef TEST_CT_BURST_DURATION_US
#define TEST_CT_BURST_DURATION_US 5000UL
#endif
#ifndef TEST_CT_ATTENUATION_CCR
#define TEST_CT_ATTENUATION_CCR 14U
#endif
#ifndef TEST_CT_MINIMUM_MILLIVOLTS
#define TEST_CT_MINIMUM_MILLIVOLTS 14000U
#endif
#ifndef TEST_CT_MAXIMUM_MILLIVOLTS
#define TEST_CT_MAXIMUM_MILLIVOLTS 16000U
#endif
#ifndef TEST_CT_COOLDOWN_MS
#define TEST_CT_COOLDOWN_MS 5000UL
#endif

/* TIM16 is private to this diagnostic; do not initialize fan PWM here. With
 * the 27.12 MHz HSE, dividing by 27 gives approximately one tick per us. */
#define TEST_CT_TIMER_DIVIDER 27UL
#define TEST_CT_TIMER_TICKS                                                                                             \
    (((1ULL * HSE_VALUE * TEST_CT_BURST_DURATION_US) + (TEST_CT_TIMER_DIVIDER * 1000000ULL) - 1) /                         \
     (TEST_CT_TIMER_DIVIDER * 1000000ULL))

_Static_assert((TEST_CT_BURST_DURATION_US > 0) && (TEST_CT_BURST_DURATION_US <= 65000UL),
               "CT burst duration must be between 1 and 65000 us");
_Static_assert((TEST_CT_TIMER_TICKS >= 1) && (TEST_CT_TIMER_TICKS <= 65536UL),
               "CT burst duration does not fit TIM16");
_Static_assert((TEST_CT_UNATTENUATED == 0) || (TEST_CT_UNATTENUATED == 1),
               "TEST_CT_UNATTENUATED must be 0 or 1");
#if !TEST_CT_UNATTENUATED
_Static_assert(TEST_CT_ATTENUATION_CCR <= 32U, "CT attenuation CCR must be between 0 and 32");
_Static_assert((TEST_CT_MINIMUM_MILLIVOLTS > 0) &&
                   (TEST_CT_MINIMUM_MILLIVOLTS <= TEST_CT_MAXIMUM_MILLIVOLTS) &&
                   (TEST_CT_MAXIMUM_MILLIVOLTS <= UINT16_MAX),
               "CT starting voltage range is invalid");
#endif
_Static_assert((TEST_CT_COOLDOWN_MS > 0) && (TEST_CT_COOLDOWN_MS < 0x80000000UL),
               "CT cooldown must be positive and less than half the tick range");

#if (TEST_RFGEN_BYPASS_TIP_DETECTOR != 0) && (TEST_RFGEN_BYPASS_TIP_DETECTOR != 1)
#error "TEST_RFGEN_BYPASS_TIP_DETECTOR must be 0 or 1"
#endif

#if (TEST_PWRLVL_SWEEP_UART != 0) && (TEST_PWRLVL_SWEEP_UART != 1)
#error "TEST_PWRLVL_SWEEP_UART must be 0 or 1"
#endif
#if TEST_PWRLVL_SWEEP_UART && defined(HOT_WAND_SWD_DEBUG) && HOT_WAND_SWD_DEBUG
#error "The PWM sweep UART mode cannot share PA14 with SWD debugging"
#endif

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

typedef struct
{
    uint8_t  compare;
    uint8_t  duty_percent;
    uint16_t output_millivolts;
} test_pwrlvl_sweep_row_t;

typedef enum
{
    TEST_CT_WAIT = 0,
    TEST_CT_READY,
    TEST_CT_RUNNING,
    TEST_CT_COOLDOWN,
} test_ct_state_t;

typedef struct
{
    uint32_t count;
    uint16_t start_millivolts;
    uint16_t end_millivolts;
    bool     valid;
    bool     aborted;
} test_ct_result_t;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

extern const uint8_t __nvm_page_start__;
extern const uint8_t __nvm_page_end__;

#if defined(HOT_WAND_VISION_TEST) && HOT_WAND_VISION_TEST
/* GDB/script: set var vision_test_fiducial_press = 1
 * The word is deliberately visible in the test ELF even with LTO. It models
 * a new button press without changing the SWD button harness's boot hold. */
volatile uint32_t vision_test_fiducial_press __attribute__((used, externally_visible));
/* Set before releasing the ADC boot gate. This never disables framebuffer
 * capture; it only selects the optional camera calibration screen at boot. */
volatile uint32_t vision_test_fiducial_enabled __attribute__((used, externally_visible));
#endif

/* GDB: p test_pwrlvl_sweep_count; p test_pwrlvl_sweep_rows; p test_pwrlvl_sweep_complete.
 * Count is published only after the corresponding row has been filled. */
volatile test_pwrlvl_sweep_row_t test_pwrlvl_sweep_rows[TEST_PWRLVL_SWEEP_ROW_COUNT];
volatile uint8_t                 test_pwrlvl_sweep_count;
volatile bool                    test_pwrlvl_sweep_complete;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

#if !FAN_PWM_ENABLED
static void test_fan_pin_init(void);
#endif
static void      test_report_tip_state(bool triggered);
static bool      test_rfgen_try_start(void);
static bool      test_ct_voltage_allows_start(uint16_t millivolts);
static void      test_ct_draw_voltage(u8g2_t* graphics, uint8_t baseline, uint16_t millivolts);
static void      test_ct_render(u8g2_t* graphics, test_ct_state_t state, const test_ct_result_t* result);
static void      test_nvm_make_settings(uint16_t sequence, hotwand_setup_nvm_t* settings);
static bool      test_nvm_settings_equal(const hotwand_setup_nvm_t* left, const hotwand_setup_nvm_t* right);
static uintptr_t test_nvm_page_start(void);
static uintptr_t test_nvm_page_end(void);
static uint16_t  test_nvm_slot_count(void);
static uintptr_t test_nvm_slot_address(uint16_t slot);
static bool      test_nvm_slot_is_erased(uint16_t slot);
static uint16_t  test_nvm_used_slot_count(void);
static uint16_t  test_nvm_next_slot(void);
static bool      test_nvm_erase_is_expected(void);
static void      test_nvm_read_physical(uintptr_t address, hotwand_setup_nvm_t* settings);
static void      test_uart_write_number(uint32_t value);
static void      test_uart_write_address(uintptr_t address);
static void      test_nvm_report_settings(const hotwand_setup_nvm_t* settings);
static bool      test_nvm_verbose_save(uint16_t sequence, uint16_t trace_number);
static void      test_nvm_restore_defaults(void);
static void      test_battery_guess_write_row(uint8_t                battery_mode,
                                              uint16_t               battery_millivolts,
                                              bool                   valid,
                                              const battery_guess_t* guess);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

/*
 * PA14 is shared by UART TX and SWCLK. Each UART test leaves UART disabled
 * until a physical button action explicitly opts in and sacrifices SWD.
 */
#if defined(HOT_WAND_SWD_DEBUG) && HOT_WAND_SWD_DEBUG
__attribute__((noinline, used))
#endif
void test_run(void)
{
    /* Only one test can be enabled at a time by uncommenting it. */
    // test_bringup_systick();
    // test_bringup_button();
    // test_bringup_oled_button();
    // test_bringup_adc();
    // test_bringup_fan();
    // test_bringup_fan_pwm_steps();
    // test_bringup_pwrlvl();
    // test_bringup_pwrlvl_min();
    // test_bringup_oled();
    // test_bringup_oled_fiducial();
    // test_bringup_oled_kiddiepool();
    // test_bringup_oled_inputs();
    // test_bringup_watchdog_simple();
    // test_rfgen();
    // test_rfgen_no_power();
    // test_rfgen_burst();
    // test_ct_reaction();
    // test_watchdog_rf_reset();
    // test_nvm_simple();
    // test_nvm_full_page();
    // test_battery_guess();
    // test_pwrlvl_sweep();
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

void test_ct_reaction(void)
{
    test_ct_state_t  state  = TEST_CT_WAIT;
    test_ct_result_t result = {0};
    uint32_t        cooldown_started_ms = 0;
    uint32_t        last_refresh_ms;
    u8g2_t*         graphics;

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init() || !rfgen_clock_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
    UART_SetAllowed(false);
    btn_init();
    btn_set_short_press_mode(BTN_SHORT_PRESS_ON_PRESS);
    btn_has_short_press(true);
    btn_has_long_press(true);
    tipdetect_init();
    adc_init();
#if TEST_CT_UNATTENUATED
    /* A static low leaves the attenuation diode undriven and gives the pot
     * and analog CT feedback sole control of the buck. Do not start TIM3. */
    {
        GPIO_InitTypeDef gpio_cfg = {0};

        __HAL_RCC_GPIOA_CLK_ENABLE();
        HAL_GPIO_WritePin(PWR_ATTENU_GPIOx, PWR_ATTENU_PINn, GPIO_PIN_RESET);
        gpio_cfg.Pin   = PWR_ATTENU_PINn;
        gpio_cfg.Mode  = GPIO_MODE_OUTPUT_PP;
        gpio_cfg.Pull  = GPIO_NOPULL;
        gpio_cfg.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(PWR_ATTENU_GPIOx, &gpio_cfg);
    }
#else
    pwrlvl_init();
    TIM3->CCR1 = TEST_CT_ATTENUATION_CCR;
#endif

    __HAL_RCC_TIM16_CLK_ENABLE();
    __HAL_RCC_TIM16_FORCE_RESET();
    __HAL_RCC_TIM16_RELEASE_RESET();
    __HAL_DBGMCU_UNFREEZE_TIM16();
    TIM16->CR1  = TIM_CR1_OPM;
    TIM16->PSC  = TEST_CT_TIMER_DIVIDER - 1;
    TIM16->ARR  = (uint32_t)TEST_CT_TIMER_TICKS - 1;
    TIM16->DIER = 0; /* Poll UIF; ADC and tip detection retain their IRQs. */
    TIM16->EGR  = TIM_EGR_UG;
    TIM16->SR   = 0;

    I2C1_Init();
    if (!OLED_Init(&oled, &i2c1))
    {
        for (;;)
        {
            watchdog_feed();
        }
    }
    OLED_ConfigureGraphics(&oled);
    graphics = OLED_GetGraphics(&oled);
    if (graphics == NULL)
    {
        for (;;)
        {
            watchdog_feed();
        }
    }
    last_refresh_ms = systick_get_ms() - TEST_STATUS_INTERVAL_MS;

    for (;;)
    {
        bool     pressed;
        bool     eligible;
        uint32_t now;

        btn_task();
        pressed = btn_has_short_press(true);
        btn_has_long_press(true);

        if (state == TEST_CT_RUNNING)
        {
            bool interrupted = !rfgen_is_active() || rfgen_has_fault();

            if (interrupted || ((TIM16->SR & TIM_SR_UIF) != 0))
            {
                /* Read the existing filtered round-robin result while RF is
                 * still on, then stop before doing any bookkeeping or I2C. */
                result.end_millivolts = adc_to_millivolts(BUCK_SENS_IDX);
                rfgen_stop();
                cooldown_started_ms = systick_get_ms();
                TIM16->CR1 &= ~TIM_CR1_CEN;
                result.aborted = interrupted || tipdetect_has_triggered() || rfgen_has_fault();
                result.valid   = true;
                state          = TEST_CT_COOLDOWN;
                last_refresh_ms = cooldown_started_ms - TEST_STATUS_INTERVAL_MS;
            }

            /* Never run the display, fault-reporting task, or HAL_Delay in
             * the active interval. A press in this state is discarded. */
            watchdog_feed();
            continue;
        }

        tipdetect_task();
        /* Only the detector can clear a disconnect latch, and only after a
         * connected input has completed qualification. RF is off here. */
        tipdetect_reset();
        now = systick_get_ms();
        if ((state != TEST_CT_COOLDOWN) || ((uint32_t)(now - cooldown_started_ms) > TEST_CT_COOLDOWN_MS))
        {
            /* This diagnostic permits another deliberate run after a power
             * dip. Only rearm with RF off, recovered DC input, and the full
             * cooldown complete; retain the previous pair and ABORT marker. */
            adc_rearm_power_loss_for_test();
        }
        eligible = test_ct_voltage_allows_start(adc_to_millivolts(BUCK_SENS_IDX)) &&
                   rfgen_tip_allows_start() && !rfgen_has_fault();

        if (state == TEST_CT_COOLDOWN)
        {
            /* Consume even a press on the transition out of cooldown. Using
             * > also guarantees five full seconds despite 1 ms tick rounding. */
            pressed = false;
            if ((uint32_t)(now - cooldown_started_ms) > TEST_CT_COOLDOWN_MS)
            {
                state = eligible ? TEST_CT_READY : TEST_CT_WAIT;
            }
        }
        else
        {
            state = eligible ? TEST_CT_READY : TEST_CT_WAIT;
        }

        if ((state == TEST_CT_READY) && pressed)
        {
            uint16_t start_millivolts;

            TIM16->CR1 = TIM_CR1_OPM;
            TIM16->CNT = 0;
            TIM16->SR  = 0;
            /* Snapshot at the start boundary; the voltage window applies only
             * in attenuated mode. rfgen_start() independently rechecks the tip
             * and fault interlocks. Never use test_rfgen_try_start(), which
             * may bypass the tip detector. */
            start_millivolts = adc_to_millivolts(BUCK_SENS_IDX);
            if (test_ct_voltage_allows_start(start_millivolts))
            {
                rfgen_start();
                TIM16->CR1 |= TIM_CR1_CEN;
                if (rfgen_is_active())
                {
                    result.start_millivolts = start_millivolts;
                    ++result.count;
                    state = TEST_CT_RUNNING;
                    continue;
                }
                TIM16->CR1 &= ~TIM_CR1_CEN;
            }
            state = TEST_CT_WAIT;
        }

        if ((uint32_t)(now - last_refresh_ms) >= TEST_STATUS_INTERVAL_MS)
        {
            last_refresh_ms = now;
            test_ct_render(graphics, state, &result);
        }
        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_bringup_oled_button(void)
{
    char                   short_line[]        = "SH: 0";
    char                   long_line[]         = "LN: 0";
    const char*            mode_line           = "(RLS)";
    btn_short_press_mode_t short_mode          = BTN_SHORT_PRESS_ON_RELEASE;
    uint32_t               release_started_ms  = 0;
    bool                   mode_toggle_pending = false;
    bool                   release_timing      = false;
    u8g2_t*                graphics;

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
    btn_init();

    /* Start in release mode so a long hold does not also count as short. */
    btn_set_short_press_mode(short_mode);
    btn_has_short_press(true);
    btn_has_long_press(true);

    I2C1_Init();
    if (!OLED_Init(&oled, &i2c1))
    {
        /* Inspect oled.error and oled.last_i2c_status in the debugger. */
        for (;;)
        {
            watchdog_feed();
        }
    }
    OLED_ConfigureGraphics(&oled);
    graphics = OLED_GetGraphics(&oled);
    if (graphics == NULL)
    {
        for (;;)
        {
            watchdog_feed();
        }
    }

    /* The 6x10 font includes parentheses and fits each line on the display. */
    u8g2_SetFont(graphics, u8g2_font_6x10_tr);
    u8g2_ClearBuffer(graphics);
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE, short_line);
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + OLED_TEXT_LINE_HEIGHT, mode_line);
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + (2 * OLED_TEXT_LINE_HEIGHT), long_line);
    OLED_SendBuffer(&oled);

    for (;;)
    {
        bool changed = false;

        btn_task();
        if (btn_has_short_press(true))
        {
            bool wrapped = short_line[4] == '9';

            short_line[4] = wrapped ? '1' : (char)(short_line[4] + 1);
            changed       = true;
            if (wrapped)
            {
                if (short_mode == BTN_SHORT_PRESS_ON_RELEASE)
                {
                    /* This event followed a debounced release, so switching
                     * to down-edge mode cannot recount the same press. */
                    short_mode = BTN_SHORT_PRESS_ON_PRESS;
                    mode_line  = "(DWN)";
                    btn_set_short_press_mode(short_mode);
                }
                else
                {
                    /* Keep down-edge mode through this press's release.
                     * Switching now would count its release a second time. */
                    mode_toggle_pending = true;
                    release_timing      = false;
                }
            }
        }
        if (btn_has_long_press(true))
        {
            long_line[4] = (long_line[4] == '9') ? '1' : (char)(long_line[4] + 1);
            changed      = true;
        }

        if (mode_toggle_pending)
        {
            if (btn_is_down())
            {
                release_timing = false;
            }
            else if (!release_timing)
            {
                release_started_ms = systick_get_ms();
                release_timing     = true;
            }
            else if ((uint32_t)(systick_get_ms() - release_started_ms) >= BTN_DEBOUNCE_MS)
            {
                short_mode = BTN_SHORT_PRESS_ON_RELEASE;
                mode_line  = "(RLS)";
                btn_set_short_press_mode(short_mode);
                mode_toggle_pending = false;
                changed             = true;
            }
        }

        if (changed)
        {
            u8g2_ClearBuffer(graphics);
            u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE, short_line);
            u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + OLED_TEXT_LINE_HEIGHT, mode_line);
            u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + (2 * OLED_TEXT_LINE_HEIGHT), long_line);
            OLED_SendBuffer(&oled);
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_bringup_oled_inputs(void)
{
    char     input_voltage[10];
    char     output_voltage[10];
    char     current[10];
    char     thermistor_1[10];
    char     thermistor_2[10];
    size_t   value_length;
    uint32_t last_refresh_ms;
    u8g2_t*  graphics;

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
    adc_init();
    btn_init();
    I2C1_Init();
    if (!OLED_Init(&oled, &i2c1))
    {
        /* Inspect oled.error and oled.last_i2c_status in the debugger. */
        for (;;)
        {
            watchdog_feed();
        }
    }
    OLED_ConfigureGraphics(&oled);
    tipdetect_init();
    graphics = OLED_GetGraphics(&oled);
    if (graphics == NULL)
    {
        for (;;)
        {
            watchdog_feed();
        }
    }

    last_refresh_ms = systick_get_ms() - TEST_STATUS_INTERVAL_MS;

    for (;;)
    {
        uint32_t now;

        btn_task();
        tipdetect_task();
        now = systick_get_ms();

        if ((uint32_t)(now - last_refresh_ms) >= TEST_STATUS_INTERVAL_MS)
        {
            last_refresh_ms = now;

            input_voltage[0] = 'I';
            millivolts_to_str(adc_to_millivolts(DC_SENS_IDX), &input_voltage[1], 1, &value_length);
            input_voltage[value_length + 1] = 'V';
            input_voltage[value_length + 2] = '\0';

            output_voltage[0] = 'O';
            millivolts_to_str(adc_to_millivolts(BUCK_SENS_IDX), &output_voltage[1], 1, &value_length);
            output_voltage[value_length + 1] = 'V';
            output_voltage[value_length + 2] = '\0';

            current[0] = 'C';
            milliamps_to_str(adc_to_milliamps(CURR_SENS_IDX), &current[1], 1, &value_length);
            current[value_length + 1] = 'A';
            current[value_length + 2] = '\0';

            thermistor_1[0] = 'T';
            thermistor_1[1] = ' ';
            celcius_to_str(adc_to_celcius(THERM_1_IDX), &thermistor_1[2], &value_length);
            thermistor_1[value_length + 2] = 'C';
            thermistor_1[value_length + 3] = '\0';

            thermistor_2[0] = 'T';
            thermistor_2[1] = ' ';
            celcius_to_str(adc_to_celcius(THERM_2_IDX), &thermistor_2[2], &value_length);
            thermistor_2[value_length + 2] = 'C';
            thermistor_2[value_length + 3] = '\0';

            /* The production detector intentionally latches a missing tip.
             * Clear that latch when the input is safely high so this bring-up
             * display follows both unplug and replug transitions. */
            tipdetect_reset();

            u8g2_ClearBuffer(graphics);
            u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE, input_voltage);
            u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + OLED_TEXT_LINE_HEIGHT, output_voltage);
            u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + (2 * OLED_TEXT_LINE_HEIGHT), current);
            u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + (3 * OLED_TEXT_LINE_HEIGHT), thermistor_1);
            u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + (4 * OLED_TEXT_LINE_HEIGHT), thermistor_2);
            u8g2_DrawStr(graphics,
                         1,
                         OLED_FIRST_TEXT_BASELINE + (5 * OLED_TEXT_LINE_HEIGHT),
                         tipdetect_has_triggered() ? "NO TIP" : "TIP");
            if (btn_is_down())
            {
                u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + (6 * OLED_TEXT_LINE_HEIGHT), "BTN");
            }
            OLED_SendBuffer(&oled);
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_nvm_full_page(void)
{
    hotwand_setup_nvm_t expected;
    hotwand_setup_nvm_t read_back;
    uint16_t            fill_count;
    uint16_t            sequence;
    uint16_t            trace;

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }
    btn_init();
    btn_has_short_press(true);
    nvm_init();
    UART_SetAllowed(false);

    /* Do absolutely nothing until a press opts in to UART and flash writes. */
    for (;;)
    {
        btn_task();
        if (btn_has_short_press(true))
        {
            UART_SetAllowed(true);
            break;
        }
        HAL_Delay(1);
        watchdog_feed();
    }

    UART_Write("\r\nNVM FULL PAGE TEST\r\n");
    UART_Write("PAGE START ");
    test_uart_write_address(test_nvm_page_start());
    UART_Write("\r\nPAGE END   ");
    test_uart_write_address(test_nvm_page_end());
    UART_Write("\r\nSLOT SIZE  ");
    test_uart_write_number((uint32_t)sizeof(hotwand_setup_nvm_t));
    UART_Write("\r\nSLOT COUNT ");
    test_uart_write_number(test_nvm_slot_count());
    UART_Write("\r\n");

    if ((test_nvm_slot_count() < 2) || !nvm_factory_reset())
    {
        UART_Write("FACTORY ERASE FAILED\r\n");
        test_nvm_restore_defaults();
        for (;;)
        {
            HAL_Delay(1);
            watchdog_feed();
        }
    }
    UART_Write("FACTORY ERASE PASSED\r\n");

    fill_count = (uint16_t)(test_nvm_slot_count() - 2);
    UART_Write("SILENTLY FILLING ");
    test_uart_write_number(fill_count);
    UART_Write(" SLOTS\r\n");

    for (sequence = 0; sequence < fill_count; ++sequence)
    {
        test_nvm_make_settings(sequence, &expected);
        if (!nvm_save(&expected) || !nvm_read(&read_back) || !test_nvm_settings_equal(&expected, &read_back))
        {
            UART_Write("SILENT FILL FAILED AT SLOT ");
            test_uart_write_number(sequence);
            UART_Write("\r\n");
            test_nvm_restore_defaults();
            for (;;)
            {
                HAL_Delay(1);
                watchdog_feed();
            }
        }

        watchdog_feed();
    }

    UART_Write("SILENT FILL PASSED; FREE SLOTS ");
    test_uart_write_number((uint32_t)(test_nvm_slot_count() - test_nvm_used_slot_count()));
    UART_Write("\r\n");

    for (trace = 0; trace < TEST_NVM_VERBOSE_SAVE_COUNT; ++trace)
    {
        if (!test_nvm_verbose_save((uint16_t)(fill_count + trace), (uint16_t)(trace + 1)))
        {
            UART_Write("VERBOSE SAVE FAILED\r\n");
            test_nvm_restore_defaults();
            for (;;)
            {
                HAL_Delay(1);
                watchdog_feed();
            }
        }

        watchdog_feed();
    }

    UART_Write("FINAL USED SLOTS ");
    test_uart_write_number(test_nvm_used_slot_count());
    UART_Write(test_nvm_used_slot_count() == 2 ? " (PASS)\r\n" : " (FAIL, EXPECTED 2)\r\n");
    test_nvm_restore_defaults();
    UART_Write("NVM FULL PAGE TEST COMPLETE\r\n");

    for (;;)
    {
        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_pwrlvl_sweep(void)
{
    uint32_t boot_ms;
    uint32_t step_ms;
    uint8_t  compare;

    HAL_Init();
    boot_ms = systick_get_ms();
    rfgen_stop();
    if (!rfgen_clock_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
    adc_init();
    pwrlvl_init();
    TIM3->CCR1 = 0;

    test_pwrlvl_sweep_count    = 0;
    test_pwrlvl_sweep_complete = false;
#if TEST_PWRLVL_SWEEP_UART
    UART_SetAllowed(true);
    UART_Write("compare,duty_percent,output_millivolts\r\n");
#endif

    /* Leave the buck at its pot-adjusted idle voltage for five seconds from
     * boot. The ADC also has time to fill its filtered voltage reading. */
    while ((uint32_t)(systick_get_ms() - boot_ms) < TEST_PWRLVL_SWEEP_START_DELAY_MS)
    {
        HAL_Delay(1);
    }

    for (compare = 0; compare < TEST_PWRLVL_SWEEP_ROW_COUNT; ++compare)
    {
        if (compare != 0)
        {
            if (compare == TEST_PWRLVL_SWEEP_PERIOD_COUNTS)
            {
                /* Full duty is held high continuously at the final setting. */
                pwrlvl_force_minimum();
            }
            else
            {
                TIM3->CCR1 = compare;
            }

            step_ms = systick_get_ms();
            while ((uint32_t)(systick_get_ms() - step_ms) < TEST_PWRLVL_SWEEP_STEP_DELAY_MS)
            {
                HAL_Delay(1);
            }
        }

        test_pwrlvl_sweep_rows[compare].compare             = compare;
        test_pwrlvl_sweep_rows[compare].duty_percent        =
            (uint8_t)(((uint16_t)compare * 100 + (TEST_PWRLVL_SWEEP_PERIOD_COUNTS / 2)) /
                      TEST_PWRLVL_SWEEP_PERIOD_COUNTS);
        test_pwrlvl_sweep_rows[compare].output_millivolts = adc_to_millivolts(BUCK_SENS_IDX);
        test_pwrlvl_sweep_count                           = compare + 1;

#if TEST_PWRLVL_SWEEP_UART
        test_uart_write_number(compare);
        UART_Write(",");
        test_uart_write_number(test_pwrlvl_sweep_rows[compare].duty_percent);
        UART_Write(",");
        test_uart_write_number(test_pwrlvl_sweep_rows[compare].output_millivolts);
        UART_Write("\r\n");
#endif
    }

    test_pwrlvl_sweep_complete = true;

    /* Keep maximum attenuation and the completed table available to GDB.
     * Do not start the watchdog: a debugger halt must not erase the results. */
    for (;;)
    {
        HAL_Delay(1);
    }
}

void test_rfgen_burst(void)
{
    uint32_t burst_started_ms = 0;
    bool     burst_active     = false;
    bool     last_tip_state;

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init() || !rfgen_clock_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
    btn_init();
#if !TEST_RFGEN_BYPASS_TIP_DETECTOR
    tipdetect_init();
#endif
    I2C1_Init();
    OLED_Init(&oled, &i2c1);
    OLED_ConfigureGraphics(&oled);
    UART_SetAllowed(false);
    btn_has_short_press(true);
    last_tip_state = false;
#if !TEST_RFGEN_BYPASS_TIP_DETECTOR
    last_tip_state = tipdetect_has_triggered();
    test_report_tip_state(last_tip_state);
#endif

    for (;;)
    {
        uint32_t now;
        bool     tip_state;

        btn_task();
#if !TEST_RFGEN_BYPASS_TIP_DETECTOR
        tipdetect_task();
#endif
        now = systick_get_ms();

        if (btn_has_short_press(true))
        {
            UART_SetAllowed(true);
            burst_started_ms = now;
            burst_active     = test_rfgen_try_start();

#if !TEST_RFGEN_BYPASS_TIP_DETECTOR
            /* Report even when reset leaves the detector state unchanged. */
            last_tip_state = tipdetect_has_triggered();
            test_report_tip_state(last_tip_state);
#endif
        }

#if TEST_RFGEN_BYPASS_TIP_DETECTOR
        tip_state = false;
#else
        tip_state = tipdetect_has_triggered();
        if (tip_state != last_tip_state)
        {
            last_tip_state = tip_state;
            test_report_tip_state(tip_state);
        }
#endif

        if (burst_active && (tip_state || ((uint32_t)(now - burst_started_ms) >= TEST_RFGEN_BURST_DURATION_MS)))
        {
            rfgen_stop();
            burst_active = false;
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_bringup_watchdog_simple(void)
{
    char     elapsed_seconds[12];
    size_t   elapsed_length;
    uint32_t started_ms;
    uint32_t last_refresh_ms;
    u8g2_t*  graphics;

    HAL_Init();
    rfgen_stop();
    systick_init();
    I2C1_Init();
    if (!OLED_Init(&oled, &i2c1))
    {
        /* Inspect oled.error and oled.last_i2c_status in the debugger. */
        for (;;)
        {
        }
    }
    OLED_ConfigureGraphics(&oled);
    graphics = OLED_GetGraphics(&oled);
    if (graphics == NULL)
    {
        for (;;)
        {
        }
    }

    /* Start the watchdog only after display setup so the visible counter and
     * watchdog timeout begin together. */
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }

    started_ms      = systick_get_ms();
    last_refresh_ms = started_ms - TEST_STATUS_INTERVAL_MS;

    for (;;)
    {
        uint32_t now        = systick_get_ms();
        uint32_t elapsed_ms = now - started_ms;

        if (elapsed_ms < TEST_WATCHDOG_FEED_DURATION_MS)
        {
            watchdog_feed();
        }

        if ((uint32_t)(now - last_refresh_ms) >= TEST_STATUS_INTERVAL_MS)
        {
            last_refresh_ms = now;
            millivolts_to_str(elapsed_ms, elapsed_seconds, 1, &elapsed_length);
            elapsed_seconds[elapsed_length++] = 's';
            elapsed_seconds[elapsed_length]   = '\0';

            u8g2_ClearBuffer(graphics);
            u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE, elapsed_seconds);
            OLED_SendBuffer(&oled);
        }

        HAL_Delay(1);
    }
}

#if !ADC_SIM_ENABLED

void test_bringup_oled_kiddiepool(void)
{
    uint8_t splash_index = 0;
    u8g2_t* graphics;

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
    btn_init();
    I2C1_Init();
    if (!OLED_Init(&oled, &i2c1))
    {
        /* Inspect oled.error and oled.last_i2c_status in the debugger. */
        for (;;)
        {
            watchdog_feed();
        }
    }
    OLED_ConfigureGraphics(&oled);
    graphics = OLED_GetGraphics(&oled);
    if (graphics == NULL)
    {
        for (;;)
        {
            watchdog_feed();
        }
    }

    btn_has_short_press(true);
    u8g2_SetDrawColor(graphics, 1);
    u8g2_SetBitmapMode(graphics, 0);
    u8g2_ClearBuffer(graphics);
    u8g2_DrawXBMP(graphics, 0, 0, SPLASH_SCREEN_WIDTH, SPLASH_SCREEN_HEIGHT, splash_screens[splash_index]);
    OLED_SendBuffer(&oled);

    for (;;)
    {
        btn_task();

        if (btn_has_short_press(true))
        {
            ++splash_index;
            if (splash_index >= SPLASH_SCREEN_COUNT)
            {
                splash_index = 0;
            }

            u8g2_ClearBuffer(graphics);
            u8g2_DrawXBMP(graphics, 0, 0, SPLASH_SCREEN_WIDTH, SPLASH_SCREEN_HEIGHT, splash_screens[splash_index]);
            OLED_SendBuffer(&oled);
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

#endif

void test_bringup_fan(void)
{
    bool fan_pin_owned = false;

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }
#if FAN_PWM_ENABLED
    if (!rfgen_clock_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
#endif
    btn_init();

    /* Prevent the production temperature task from fighting direct control. */
    fan_stop();

    for (;;)
    {
        if (btn_is_down())
        {
            if (!fan_pin_owned)
            {
#if FAN_PWM_ENABLED
                if (!fanpwm_init(FANPWM_MODE_DIRECT))
                {
                    continue;
                }
#else
                test_fan_pin_init();
#endif
                fan_pin_owned = true;
            }
#if FAN_PWM_ENABLED
            fanpwm_set(50);
#else
            HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, GPIO_PIN_SET);
#endif
        }
        else if (fan_pin_owned)
        {
#if FAN_PWM_ENABLED
            fanpwm_set(0);
#else
            HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, GPIO_PIN_RESET);
#endif
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_bringup_fan_pwm_steps(void)
{
    u8g2_t* graphics;
#if FAN_PWM_ENABLED
    static const char* const duty_text[]   = {"0%", "25%", "50%", "75%", "100%"};
    uint8_t                  step          = 0;
    bool                     fan_pin_owned = false;
#endif

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init() || !rfgen_clock_init())
    {
        // RF stays off; let the watchdog reset an unsuccessful clock setup.
        for (;;)
        {
        }
    }
    systick_init();
    btn_init();
    btn_set_short_press_mode(BTN_SHORT_PRESS_ON_PRESS);
    // A button held during boot must be released before it can select a step.
    btn_discard_until_release();
    fan_stop(); // Keep the production temperature controller out of this test.

    I2C1_Init();
    if (!OLED_Init(&oled, &i2c1))
    {
        // Keep RF off and leave PA13 available for debugging display setup.
        for (;;)
        {
            watchdog_feed();
        }
    }
    OLED_ConfigureGraphics(&oled);
    graphics = OLED_GetGraphics(&oled);
    if (graphics == NULL)
    {
        for (;;)
        {
            watchdog_feed();
        }
    }
    u8g2_SetFont(graphics, u8g2_font_6x10_tr);
    u8g2_ClearBuffer(graphics);
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE, "FAN");
#if FAN_PWM_ENABLED
    // PA13 still belongs to SWD. Do not display a commanded duty until the
    // first press actually initializes the PWM hardware at zero percent.
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + OLED_TEXT_LINE_HEIGHT, "PRESS");
#else
    // GPIO-only builds cannot represent the intermediate PWM steps.
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + OLED_TEXT_LINE_HEIGHT, "PWM");
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + (2 * OLED_TEXT_LINE_HEIGHT), "OFF");
#endif
    OLED_SendBuffer(&oled);

    for (;;)
    {
        btn_task();
#if FAN_PWM_ENABLED
        if (btn_has_short_press(true))
        {
            if (!fan_pin_owned)
            {
                // Standalone direct-input test: SJ4-A and SJ5-D. No saved
                // settings are read. SJ5-C instead needs EXTERNAL_MOSFET mode.
                if (!fanpwm_init(FANPWM_MODE_DIRECT))
                {
                    show_fault("FAN\nPWM\nFAULT", false);
                }
                fan_pin_owned = true;
            }
            else
            {
                step = (uint8_t)((step + 1) % (sizeof(duty_text) / sizeof(duty_text[0])));
            }
            // This is PWM duty, not tachometer-measured RPM. Hold each value
            // until the next down-edge; a held button must not auto-repeat.
            fanpwm_set((uint8_t)(step * 25));
            u8g2_ClearBuffer(graphics);
            u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE, "FAN");
            u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + OLED_TEXT_LINE_HEIGHT, duty_text[step]);
            OLED_SendBuffer(&oled);
        }
#endif
        btn_has_long_press(true); // No sleep or alternate action in this test.
        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_watchdog_rf_reset(void)
{
#if TEST_WATCHDOG_ENABLED
    HAL_Init();
    rfgen_stop();
    if (!watchdog_init() || !rfgen_clock_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
    btn_init();
    tipdetect_init();
    UART_SetAllowed(false);
    btn_has_short_press(true);

    for (;;)
    {
        btn_task();

        if (btn_has_short_press(true))
        {
            UART_SetAllowed(true);
            UART_Write("WATCHDOG RF RESET TEST: RF starts after this line\r\n");

            /* Give the active-RF interval one full watchdog period. */
            watchdog_feed();
            rfgen_start();

            if (rfgen_is_active())
            {
                __disable_irq();

                /* Deliberately do not feed. TIM1 must keep toggling until the
                 * independent watchdog resets the MCU and PB1 fails low. */
                for (;;)
                {
                }
            }

            UART_Write("WATCHDOG RF RESET TEST: RF start was blocked\r\n");
        }

        HAL_Delay(1);
        watchdog_feed();
    }
#else
    /* This test deliberately relies on IWDG to terminate active RF. Refuse to
     * arm RF when the test-wide watchdog switch is off. */
    HAL_Init();
    rfgen_stop();
    for (;;)
    {
    }
#endif
}

void test_battery_guess(void)
{
    battery_guess_t guess;
    uint16_t        battery_millivolts;
    uint8_t         battery_mode;
    bool            valid;

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }
    btn_init();
    btn_has_short_press(true);
    UART_SetAllowed(false);

    /* PA14 remains available for SWD until a deliberate press starts the
     * test and opts in to UART output. */
    for (;;)
    {
        btn_task();
        if (btn_has_short_press(true))
        {
            UART_SetAllowed(true);
            break;
        }
        HAL_Delay(1);
        watchdog_feed();
    }

    UART_Write("battery_mode,battery_millivolts,valid,optimistic_cell_count,pessimistic_cell_count,");
    UART_Write("optimistic_millivolts_per_cell,pessimistic_millivolts_per_cell\r\n");

    for (battery_mode = BATT_MODE_NONE; battery_mode <= BATT_MODE_LIFE_SAFE; ++battery_mode)
    {
        for (battery_millivolts = TEST_BATTERY_GUESS_MINIMUM_MILLIVOLTS;
             battery_millivolts <= TEST_BATTERY_GUESS_MAXIMUM_MILLIVOLTS;
             battery_millivolts = (uint16_t)(battery_millivolts + TEST_BATTERY_GUESS_STEP_MILLIVOLTS))
        {
            valid = battery_guess(battery_millivolts, battery_mode, &guess);
            test_battery_guess_write_row(battery_mode, battery_millivolts, valid, &guess);
            watchdog_feed();
        }
    }

    for (;;)
    {
        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_nvm_simple(void)
{
    hotwand_setup_nvm_t current;
    hotwand_setup_nvm_t next;
    uint16_t            sequence = 0;

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }
    btn_init();
    btn_has_short_press(true);
    nvm_init();
    UART_SetAllowed(false);

    for (;;)
    {
        btn_task();

        if (btn_has_short_press(true))
        {
            UART_SetAllowed(true);
            UART_Write("\r\nNVM SIMPLE TEST\r\n");

            /* nvm_save() deliberately skips unchanged settings. Advance past a
             * matching existing record so every press programs flash. */
            test_nvm_make_settings(sequence, &next);
            if (nvm_read(&current) && test_nvm_settings_equal(&current, &next))
            {
                ++sequence;
            }

            test_nvm_verbose_save(sequence, sequence + 1);
            test_nvm_restore_defaults();
            ++sequence;
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_bringup_systick(void)
{
    char     timestamp[12];
    uint32_t last_report_ms;

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }
    if (!rfgen_clock_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
    btn_init();
    UART_SetAllowed(false);
    last_report_ms = systick_get_ms() - TEST_STATUS_INTERVAL_MS;

    for (;;)
    {
        uint32_t now         = systick_get_ms();
        bool     button_down = btn_is_down();

        UART_SetAllowed(button_down);
        if (button_down && ((uint32_t)(now - last_report_ms) >= TEST_STATUS_INTERVAL_MS))
        {
            last_report_ms = now;
            UART_Write(int_to_str((int)now, timestamp, 10, NULL));
            UART_Write("\r\n");
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_bringup_pwrlvl_min(void)
{
    bool minimum_applied = false;

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init() || !rfgen_clock_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
    btn_init();
    pwrlvl_init();
    TIM3->CCR1 = 0;

    for (;;)
    {
        bool button_down = btn_is_down();

        if (button_down && !minimum_applied)
        {
            pwrlvl_force_minimum();
            minimum_applied = true;
        }
        else if (!button_down && minimum_applied)
        {
            pwrlvl_release_minimum();
            TIM3->CCR1      = 0;
            minimum_applied = false;
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_bringup_button(void)
{
    char timestamp[12];

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }
    btn_init();
    UART_SetAllowed(false);
    btn_has_short_press(true);

    for (;;)
    {
        btn_task();

        if (btn_has_short_press(true))
        {
            UART_SetAllowed(true);
            UART_Write("BUTTON PRESSED ");
            UART_Write(int_to_str((int)systick_get_ms(), timestamp, 10, NULL));
            UART_Write("\r\n");
        }
        else if (!btn_is_down())
        {
            UART_SetAllowed(false);
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_rfgen(void)
{
    bool enabled = false;

    HAL_Init();
    rfgen_stop();
    if (!watchdog_init() || !rfgen_clock_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
    btn_init();
#if !TEST_RFGEN_BYPASS_TIP_DETECTOR
    tipdetect_init();
#endif

    for (;;)
    {
        bool button_down = btn_is_down();

        if (button_down && !enabled)
        {
            enabled = test_rfgen_try_start();
        }
        else if (!button_down && enabled)
        {
            rfgen_stop();
            enabled = false;
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_rfgen_no_power(void)
{
    bool enabled = false;

    HAL_Init();
    rfgen_stop();
    /* Initialize PWM at full attenuation and hold minimum buck voltage. */
    pwrlvl_force_minimum();
    pwrlvl_init();
    if (!watchdog_init() || !rfgen_clock_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
    btn_init();
#if !TEST_RFGEN_BYPASS_TIP_DETECTOR
    tipdetect_init();
#endif

    for (;;)
    {
        bool button_down = btn_is_down();

        if (button_down && !enabled)
        {
            enabled = test_rfgen_try_start();
        }
        else if (!button_down && enabled)
        {
            rfgen_stop();
            enabled = false;
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

#if defined(HOT_WAND_VISION_TEST) && HOT_WAND_VISION_TEST

void test_bringup_oled_fiducial(void)
{
    HAL_Init();
    rfgen_stop();
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }

    systick_init();
    btn_init();
    I2C1_Init();
    if (!OLED_Init(&oled, &i2c1))
    {
        /* Preserve SWD access and feed IWDG if the display cannot start. */
        for (;;)
        {
            watchdog_feed();
        }
    }
    OLED_ConfigureGraphics(&oled);

    /* Dedicated camera setup image: show only the calibration target until
     * a new button press or a GDB write, then leave the OLED blank. */
    vision_test_fiducial_gate();
    for (;;)
    {
        HAL_Delay(1);
        watchdog_feed();
    }
}

#endif

void test_bringup_adc(void)
{
    HAL_Init();
    rfgen_stop();
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }
    adc_init();
    btn_init();
    UART_SetAllowed(false);
    btn_has_short_press(true);

    for (;;)
    {
        btn_task();
        if (btn_has_short_press(true))
        {
            UART_SetAllowed(true);
        }

        UART_debug_task();
        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_bringup_oled(void)
{
    HAL_Init();
    rfgen_stop();
    if (!watchdog_init())
    {
        for (;;)
        {
        }
    }
    I2C1_Init();
    if (!OLED_Init(&oled, &i2c1))
    {
        /* Inspect oled.error and oled.last_i2c_status in the debugger. */
        for (;;)
        {
        }
    }
    OLED_ConfigureGraphics(&oled);

    for (;;)
    {
        /* show_fault() is terminal and services IWDG after making RF safe. */
        show_fault("HELLO\nWORLD", false);
    }
}

void test_bringup_pwrlvl(void)
{
    HAL_Init();
    rfgen_stop();
    if (!watchdog_init() || !rfgen_clock_init())
    {
        for (;;)
        {
        }
    }
    systick_init();
    btn_init();
    pwrlvl_init();
    TIM3->CCR1 = 0;

    for (;;)
    {
        /* TIM3 has 32 counts per period, so CCR1 = 24 is exactly 75%. */
        TIM3->CCR1 = btn_is_down() ? TEST_PWRLVL_75_PERCENT_CCR : 0;
        HAL_Delay(1);
        watchdog_feed();
    }
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

#if defined(HOT_WAND_VISION_TEST) && HOT_WAND_VISION_TEST

void vision_test_fiducial_gate(void)
{
    /* The SWD menu harness intentionally ignores PA7 events, but a human
     * press must still be able to dismiss this camera target in that image. */
    bool button_was_down = btn_is_down() || (HAL_GPIO_ReadPin(BTN_GPIOx, BTN_PINn) == GPIO_PIN_RESET);

    /* This is the first frame in a vision-test boot. No timer dismisses it:
     * the host can take and inspect as many stills as camera setup needs. */
    OLED_ShowFiducial();

    for (;;)
    {
        bool button_down;

        btn_task();
        button_down = btn_is_down() || (HAL_GPIO_ReadPin(BTN_GPIOx, BTN_PINn) == GPIO_PIN_RESET);

        /* A fresh physical down edge or one SWD write advances boot. An
         * already-held boot button must not hide the fiducial immediately. */
        if ((vision_test_fiducial_press != 0) || (button_down && !button_was_down))
        {
            break;
        }

        button_was_down = button_down;
        HAL_Delay(1);
        watchdog_feed();
    }

    vision_test_fiducial_press = 0;
    btn_has_short_press(true);
    btn_has_long_press(true);

    /* Remove the target before the Setup hold prompt or normal boot draws. */
    u8g2_ClearBuffer(OLED_GetGraphics(&oled));
    OLED_SendBuffer(&oled);
}

#endif

/* Restore a verified default record before a standalone test waits again.
 * Appending avoids erasing a page after every simple-test button press. */
static void test_nvm_restore_defaults(void)
{
    hotwand_setup_nvm_t defaults;
    hotwand_setup_nvm_t actual;

    nvm_apply_defaults(&defaults);
    if (!nvm_save(&defaults) || !nvm_read(&actual) || !test_nvm_settings_equal(&defaults, &actual))
    {
        UART_Write("FACTORY DEFAULT RESTORE FAILED; RETRYING\r\n");
        // Do not report completion or silently park with synthetic test settings.
        // Keep retrying while servicing IWDG, allowing transient errors to clear.
        do
        {
            watchdog_feed();
            HAL_Delay(10);
        } while (!nvm_save(&defaults) || !nvm_read(&actual) || !test_nvm_settings_equal(&defaults, &actual));
    }
    UART_Write("FACTORY DEFAULTS RESTORED AND VERIFIED\r\n");
}

static bool test_nvm_verbose_save(uint16_t sequence, uint16_t trace_number)
{
    hotwand_setup_nvm_t expected;
    hotwand_setup_nvm_t physical;
    hotwand_setup_nvm_t read_back;
    uintptr_t           write_address;
    uint16_t            after_used;
    uint16_t            before_used    = test_nvm_used_slot_count();
    uint16_t            next_slot      = test_nvm_next_slot();
    bool                erase_expected = test_nvm_erase_is_expected();
    bool                api_read_ok;
    bool                physical_read_ok;
    bool                save_ok;
    bool                erase_confirmed;

    write_address = erase_expected ? test_nvm_page_start() : test_nvm_slot_address(next_slot);
    test_nvm_make_settings(sequence, &expected);

    UART_Write("\r\n--- SAVE ");
    test_uart_write_number(trace_number);
    UART_Write(" ---\r\nBEFORE USED=");
    test_uart_write_number(before_used);
    UART_Write(" NEXT SLOT=");
    if (erase_expected)
    {
        UART_Write("FULL");
    }
    else
    {
        test_uart_write_number(next_slot);
    }
    UART_Write("\r\nTARGET ADDRESS ");
    test_uart_write_address(write_address);
    UART_Write("\r\n");
    test_nvm_report_settings(&expected);

    save_ok         = nvm_save(&expected);
    after_used      = test_nvm_used_slot_count();
    erase_confirmed = erase_expected && save_ok && (after_used == 1);

    api_read_ok = save_ok && nvm_read(&read_back) && test_nvm_settings_equal(&expected, &read_back);
    test_nvm_read_physical(write_address, &physical);
    physical_read_ok = save_ok && test_nvm_settings_equal(&expected, &physical);

    UART_Write(save_ok ? "SAVE API PASS\r\n" : "SAVE API FAIL\r\n");
    if (erase_expected)
    {
        UART_Write(erase_confirmed ? "ERASE HAPPENED AND WAS CONFIRMED\r\n" : "ERASE EXPECTED BUT NOT CONFIRMED\r\n");
    }
    else
    {
        UART_Write("ERASE NOT NEEDED\r\n");
    }
    UART_Write("AFTER USED=");
    test_uart_write_number(after_used);
    UART_Write("\r\nAPI READBACK ");
    UART_Write(api_read_ok ? "PASS\r\n" : "FAIL\r\n");
    UART_Write("PHYSICAL READBACK AT ");
    test_uart_write_address(write_address);
    UART_Write(physical_read_ok ? " PASS\r\n" : " FAIL\r\n");

    return save_ok && api_read_ok && physical_read_ok && (!erase_expected || erase_confirmed);
}

static void test_nvm_make_settings(uint16_t sequence, hotwand_setup_nvm_t* settings)
{
    uint16_t value = sequence;

    nvm_apply_defaults(settings);
    settings->startup_power_level = (uint8_t)(value % 3);
    value /= 3;
    settings->fan_mode = (uint8_t)(value % 16);
    value /= 16;
    settings->fan_sig_inv = (uint8_t)(value % 2);
    value /= 2;
    settings->auto_sleep = (uint8_t)(value % 4);
    value /= 4;
    settings->auto_dim = (uint8_t)(value % 4);
    value /= 4;
    settings->idle_detect_thresh = (uint8_t)(value % 7);
    value /= 7;
    settings->batt_mode = (uint8_t)(value % 7);
    value /= 7;
    settings->input_v_calib = (uint8_t)(value % 11);
    value /= 11;
    settings->show_splash = (uint8_t)(value % 2);
}

static bool test_nvm_erase_is_expected(void)
{
    uint16_t slot;
    bool     erased_slot_seen = false;

    for (slot = 0; slot < test_nvm_slot_count(); ++slot)
    {
        if (test_nvm_slot_is_erased(slot))
        {
            erased_slot_seen = true;
        }
        else if (erased_slot_seen)
        {
            /* This is the interrupted-journal pattern nvm_scan_page() marks
             * for erasure before the next save. */
            return true;
        }
    }

    return !erased_slot_seen;
}

#if !FAN_PWM_ENABLED

static void test_fan_pin_init(void)
{
    // the actual fan state machine won't actually initialize the pin, so we have to do it here for the test

    GPIO_InitTypeDef gpio_cfg = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, GPIO_PIN_RESET);

    gpio_cfg.Pin   = FAN_PINn;
    gpio_cfg.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_cfg.Pull  = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(FAN_GPIOx, &gpio_cfg);
}

#endif

static bool test_rfgen_try_start(void)
{
#if TEST_RFGEN_BYPASS_TIP_DETECTOR
    rfgen_start_with_tip_bypass_for_test();
#else
    /* A deliberate press may clear a startup tip-disconnect latch, but only
     * while the live TIP_DET input confirms a present tip. */
    tipdetect_reset();
    rfgen_start();
#endif

    /* Both start paths remain fail-closed for clock and emergency-stop
     * faults. Returning the real state also permits a held-button retry. */
    return rfgen_is_active();
}

static bool test_nvm_slot_is_erased(uint16_t slot)
{
    const volatile uint16_t* flash = (const volatile uint16_t*)test_nvm_slot_address(slot);
    uint16_t                 halfword;

    for (halfword = 0; halfword < (sizeof(hotwand_setup_nvm_t) / sizeof(uint16_t)); ++halfword)
    {
        if (flash[halfword] != 0xFFFF)
        {
            return false;
        }
    }

    return true;
}

static uint16_t test_nvm_used_slot_count(void)
{
    uint16_t slot;
    uint16_t used = 0;

    for (slot = 0; slot < test_nvm_slot_count(); ++slot)
    {
        if (!test_nvm_slot_is_erased(slot))
        {
            ++used;
        }
    }

    return used;
}

static uint16_t test_nvm_next_slot(void)
{
    uint16_t slot;

    for (slot = 0; slot < test_nvm_slot_count(); ++slot)
    {
        if (test_nvm_slot_is_erased(slot))
        {
            return slot;
        }
    }

    return test_nvm_slot_count();
}

static void test_nvm_read_physical(uintptr_t address, hotwand_setup_nvm_t* settings)
{
    const volatile uint8_t* source      = (const volatile uint8_t*)address;
    uint8_t*                destination = (uint8_t*)settings;
    uint16_t                index;

    for (index = 0; index < sizeof(*settings); ++index)
    {
        destination[index] = source[index];
    }
}

static bool test_ct_voltage_allows_start(uint16_t millivolts)
{
#if TEST_CT_UNATTENUATED
    (void)millivolts;
    return true;
#else
    return (millivolts >= TEST_CT_MINIMUM_MILLIVOLTS) && (millivolts <= TEST_CT_MAXIMUM_MILLIVOLTS);
#endif
}

static bool test_nvm_settings_equal(const hotwand_setup_nvm_t* left, const hotwand_setup_nvm_t* right)
{
    return (left != NULL) && (right != NULL) && (left->startup_power_level == right->startup_power_level) &&
           (left->fan_mode == right->fan_mode) && (left->auto_sleep == right->auto_sleep) &&
           (left->auto_dim == right->auto_dim) && (left->idle_detect_thresh == right->idle_detect_thresh) &&
           (left->batt_mode == right->batt_mode) && (left->input_v_calib == right->input_v_calib) &&
           (left->show_splash == right->show_splash) && (left->fan_sig_inv == right->fan_sig_inv);
}

static uintptr_t test_nvm_page_start(void)
{
    return (uintptr_t)&__nvm_page_start__;
}

static uintptr_t test_nvm_page_end(void)
{
    return (uintptr_t)&__nvm_page_end__;
}

static uint16_t test_nvm_slot_count(void)
{
    return (uint16_t)((test_nvm_page_end() - test_nvm_page_start()) / sizeof(hotwand_setup_nvm_t));
}

static uintptr_t test_nvm_slot_address(uint16_t slot)
{
    return test_nvm_page_start() + ((uintptr_t)slot * sizeof(hotwand_setup_nvm_t));
}

// -----------------------------------------------------------------------------
// Debug / Logging Helpers
// -----------------------------------------------------------------------------

static void test_ct_render(u8g2_t* graphics, test_ct_state_t state, const test_ct_result_t* result)
{
    const char* status = state == TEST_CT_COOLDOWN ? "COOL" : (state == TEST_CT_READY ? "READY" : "WAIT");

    u8g2_ClearBuffer(graphics);
    u8g2_SetFont(graphics, u8g2_font_5x7_tr);
    u8g2_DrawStr(graphics, 1, 9, "DC IN");
    test_ct_draw_voltage(graphics, 19, adc_to_millivolts(DC_SENS_IDX));
    u8g2_DrawStr(graphics, 1, 29, "BUCK");
    test_ct_draw_voltage(graphics, 39, adc_to_millivolts(BUCK_SENS_IDX));
    if (tipdetect_has_triggered() || (HAL_GPIO_ReadPin(TIP_DET_GPIOx, TIP_DET_PINn) == GPIO_PIN_RESET))
    {
        u8g2_DrawStr(graphics, 1, 49, "OPEN");
    }
    u8g2_DrawHLine(graphics, 0, 63, 32);

    if (result->valid)
    {
        char     digits[11];
        char     first_line[8] = "# ";
        char*    number        = &digits[10];
        uint32_t count         = result->count;
        size_t   length;
        size_t   first_length;
        size_t   idx;
        uint8_t  result_baseline;

        /* Unsigned decimal conversion also handles counts above INT_MAX.
         * Split after the high digits, keeping the last five on line two. */
        *number = '\0';
        do
        {
            *--number = (char)('0' + (count % 10));
            count /= 10;
        } while (count != 0);
        length       = (size_t)(&digits[10] - number);
        first_length = length > 5 ? length - 5 : length;
        for (idx = 0; idx < first_length; ++idx)
        {
            first_line[idx + 2] = number[idx];
        }
        first_line[first_length + 2] = '\0';

        /* Leave one blank row after the counter, using it for ABORT when
         * needed. A wrapped counter shifts the voltage pair down one row.
         * Seven characters fit within 32 pixels with this font. */
        result_baseline = length > 5 ? 95 : 87;
        u8g2_SetFont(graphics, u8g2_font_4x6_tr);
        u8g2_DrawStr(graphics, 1, 71, first_line);
        if (length > 5)
        {
            u8g2_DrawStr(graphics, 1, 79, number + first_length);
        }
        if (result->aborted)
        {
            u8g2_DrawStr(graphics, 1, result_baseline - 8, "ABORT");
        }
        /* Same larger font as the live voltages: before RF above, after RF
         * below. Omitting labels leaves room for both values and the status. */
        u8g2_SetFont(graphics, u8g2_font_5x7_tr);
        test_ct_draw_voltage(graphics, result_baseline, result->start_millivolts);
        test_ct_draw_voltage(graphics, result_baseline + 12, result->end_millivolts);
        u8g2_SetFont(graphics, u8g2_font_4x6_tr);
        u8g2_DrawStr(graphics, 1, 127, status);
    }
    else
    {
        u8g2_DrawStr(graphics, 1, 79, status);
    }
    OLED_SendBuffer(&oled);
}

static void test_nvm_report_settings(const hotwand_setup_nvm_t* settings)
{
    UART_Write("DATA POWER=");
    test_uart_write_number(settings->startup_power_level);
    UART_Write(" FAN=");
    test_uart_write_number(settings->fan_mode);
    UART_Write(" FPOL=");
    test_uart_write_number(settings->fan_sig_inv);
    UART_Write(" SLEEP=");
    test_uart_write_number(settings->auto_sleep);
    UART_Write(" DIM=");
    test_uart_write_number(settings->auto_dim);
    UART_Write(" IDLE=");
    test_uart_write_number(settings->idle_detect_thresh);
    UART_Write(" BATT=");
    test_uart_write_number(settings->batt_mode);
    UART_Write(" VCAL=");
    test_uart_write_number(settings->input_v_calib);
    UART_Write(" SPLASH=");
    test_uart_write_number(settings->show_splash);
    UART_Write("\r\n");
}

static void test_battery_guess_write_row(uint8_t                battery_mode,
                                         uint16_t               battery_millivolts,
                                         bool                   valid,
                                         const battery_guess_t* guess)
{
    test_uart_write_number(battery_mode);
    UART_Write(",");
    test_uart_write_number(battery_millivolts);
    UART_Write(",");
    test_uart_write_number(valid ? 1 : 0);
    UART_Write(",");
    test_uart_write_number(guess->optimistic_cell_count);
    UART_Write(",");
    test_uart_write_number(guess->pessimistic_cell_count);
    UART_Write(",");
    test_uart_write_number(guess->optimistic_millivolts_per_cell);
    UART_Write(",");
    test_uart_write_number(guess->pessimistic_millivolts_per_cell);
    UART_Write("\r\n");
}

static void test_report_tip_state(bool triggered)
{
    u8g2_t* graphics = OLED_GetGraphics(&oled);

    UART_Write(triggered ? "TIP FAULT\r\n" : "TIP OK\r\n");

    if (graphics == NULL)
    {
        return;
    }

    u8g2_ClearBuffer(graphics);
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE, "TIP");
    u8g2_DrawStr(graphics, 1, OLED_FIRST_TEXT_BASELINE + OLED_TEXT_LINE_HEIGHT, triggered ? "FAULT" : "OK");
    OLED_SendBuffer(&oled);
}

static void test_ct_draw_voltage(u8g2_t* graphics, uint8_t baseline, uint16_t millivolts)
{
    char   voltage[10];
    size_t length;

    millivolts_to_str(millivolts, voltage, 2, &length);
    voltage[length]     = 'V';
    voltage[length + 1] = '\0';
    u8g2_DrawStr(graphics, 1, baseline, voltage);
}

static void test_uart_write_address(uintptr_t address)
{
    char number[12];

    UART_Write("0x");
    UART_Write(int_to_str((int)address, number, 16, NULL));
}

static void test_uart_write_number(uint32_t value)
{
    char number[12];

    UART_Write(int_to_str((int)value, number, 10, NULL));
}
