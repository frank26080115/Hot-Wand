// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "tests.h"

#include "battery.h"
#include "button.h"
#include "fan.h"
#include "fault.h"
#include "miscutils.h"
#include "nvm.h"
#include "oled.h"
#include "pins.h"
#include "pwrlvl.h"
#include "rfgen.h"
#include "stm32f0xx_hal.h"
#include "systick.h"
#include "tipdetect.h"
#include "uart_debug.h"
#include "watchdog.h"

#include <stdbool.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define TEST_STATUS_INTERVAL_MS               100
#define TEST_RFGEN_BURST_DURATION_MS          1000
#define TEST_PWRLVL_75_PERCENT_CCR            24
#define TEST_NVM_VERBOSE_SAVE_COUNT           4
#define TEST_BATTERY_GUESS_MINIMUM_MILLIVOLTS 14000
#define TEST_BATTERY_GUESS_MAXIMUM_MILLIVOLTS 36000
#define TEST_BATTERY_GUESS_STEP_MILLIVOLTS    250

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

extern const uint8_t __nvm_page_start__;
extern const uint8_t __nvm_page_end__;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void      test_fan_pin_init(void);
static void      test_report_tip_state(bool triggered);
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
void test_run(void)
{
    /* Only one test can be enabled at a time by uncommenting it. */
    // test_bringup_systick();
    // test_bringup_button();
    // test_bringup_adc();
    // test_bringup_fan();
    // test_bringup_pwrlvl();
    // test_bringup_pwrlvl_min();
    // test_bringup_oled();
    // test_rfgen();
    // test_rfgen_burst();
    // test_watchdog_rf_reset();
    // test_nvm_simple();
    // test_nvm_full_page();
    // test_battery_guess();
}

void test_bringup_systick(void)
{
    char     timestamp[12];
    uint32_t last_report_ms = systick_get_ms() - TEST_STATUS_INTERVAL_MS;

    UART_SetAllowed(false);

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

void test_bringup_button(void)
{
    char timestamp[12];

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

void test_bringup_adc(void)
{
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

void test_bringup_fan(void)
{
    bool fan_pin_owned = false;

    /* Prevent the production temperature task from fighting direct control. */
    fan_stop();

    for (;;)
    {
        if (btn_is_down())
        {
            if (!fan_pin_owned)
            {
                test_fan_pin_init();
                fan_pin_owned = true;
            }
            HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, GPIO_PIN_SET);
        }
        else if (fan_pin_owned)
        {
            HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, GPIO_PIN_RESET);
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_bringup_pwrlvl(void)
{
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

void test_bringup_pwrlvl_min(void)
{
    bool minimum_applied = false;

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

void test_bringup_oled(void)
{
    for (;;)
    {
        /* show_fault() is terminal and services IWDG after making RF safe. */
        show_fault("HELLO\nWORLD", false);
    }
}

void test_rfgen(void)
{
    bool enabled = false;

    UART_SetAllowed(false);
    rfgen_stop();

    for (;;)
    {
        bool button_down = btn_is_down();

        if (button_down && !enabled)
        {
            /* The first RF-enable press permanently opts in to UART for the
             * remainder of this non-returning test. */
            UART_SetAllowed(true);
            rfgen_start();
            enabled = true;
        }
        else if (!button_down && enabled)
        {
            rfgen_stop();
            enabled = false;
        }

        UART_debug_task();
        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_rfgen_burst(void)
{
    uint32_t burst_started_ms = 0;
    bool     burst_active     = false;
    bool     last_tip_state;

    UART_SetAllowed(false);
    rfgen_stop();
    btn_has_short_press(true);
    last_tip_state = tipdetect_has_triggered();
    test_report_tip_state(last_tip_state);

    for (;;)
    {
        uint32_t now;
        bool     tip_state;

        btn_task();
        tipdetect_task();
        now = systick_get_ms();

        if (btn_has_short_press(true))
        {
            UART_SetAllowed(true);
            tipdetect_reset();
            rfgen_start();
            burst_started_ms = now;
            burst_active     = true;

            /* Report even when reset leaves the detector state unchanged. */
            last_tip_state = tipdetect_has_triggered();
            test_report_tip_state(last_tip_state);
        }

        tip_state = tipdetect_has_triggered();
        if (tip_state != last_tip_state)
        {
            last_tip_state = tip_state;
            test_report_tip_state(tip_state);
        }

        if (burst_active && (tip_state || ((uint32_t)(now - burst_started_ms) >= TEST_RFGEN_BURST_DURATION_MS)))
        {
            rfgen_stop();
            burst_active = false;
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_watchdog_rf_reset(void)
{
    UART_SetAllowed(false);
    rfgen_stop();
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
}

void test_nvm_simple(void)
{
    hotwand_setup_nvm_t current;
    hotwand_setup_nvm_t next;
    uint16_t            sequence = 0;

    UART_SetAllowed(false);
    btn_has_short_press(true);
    nvm_init();

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
            ++sequence;
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

    UART_SetAllowed(false);
    btn_has_short_press(true);
    nvm_init();

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
    UART_Write("NVM FULL PAGE TEST COMPLETE\r\n");

    for (;;)
    {
        HAL_Delay(1);
        watchdog_feed();
    }
}

void test_battery_guess(void)
{
    battery_guess_t guess;
    uint16_t        battery_millivolts;
    uint8_t         battery_mode;
    bool            valid;

    UART_SetAllowed(false);
    btn_has_short_press(true);

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

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

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

static void test_nvm_make_settings(uint16_t sequence, hotwand_setup_nvm_t* settings)
{
    uint16_t value = sequence;

    nvm_apply_defaults(settings);
    settings->startup_power_level = (uint8_t)(value % 3);
    value /= 3;
    settings->fan_mode = (uint8_t)(value % 4);
    value /= 4;
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

static bool test_nvm_settings_equal(const hotwand_setup_nvm_t* left, const hotwand_setup_nvm_t* right)
{
    return (left != NULL) && (right != NULL) && (left->startup_power_level == right->startup_power_level) &&
           (left->fan_mode == right->fan_mode) && (left->auto_sleep == right->auto_sleep) &&
           (left->auto_dim == right->auto_dim) && (left->idle_detect_thresh == right->idle_detect_thresh) &&
           (left->batt_mode == right->batt_mode) && (left->input_v_calib == right->input_v_calib) &&
           (left->show_splash == right->show_splash);
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

static void test_uart_write_number(uint32_t value)
{
    char number[12];

    UART_Write(int_to_str((int)value, number, 10, NULL));
}

static void test_uart_write_address(uintptr_t address)
{
    char number[12];

    UART_Write("0x");
    UART_Write(int_to_str((int)address, number, 16, NULL));
}

static void test_nvm_report_settings(const hotwand_setup_nvm_t* settings)
{
    UART_Write("DATA POWER=");
    test_uart_write_number(settings->startup_power_level);
    UART_Write(" FAN=");
    test_uart_write_number(settings->fan_mode);
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
