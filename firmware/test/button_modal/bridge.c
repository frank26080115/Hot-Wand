/* Run the actual button and battery dialog together. Only hardware/display
 * access is mocked; the override hold and event suppression are real C. */
// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "stm32f0xx_hal.h"
#include "../../src/button.c"
#include "../../src/battery.c"

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

EXTI_TypeDef    mock_exti;
uint32_t        mock_primask, mock_pin;
static uint32_t mock_now, mock_resumes, mock_faults, mock_fan_resumes, mock_buck_releases;
static uint32_t mock_drop_at, mock_next_voltage, mock_full_frame_voltage, mock_progress;
static uint16_t mock_voltage;
OLED_Handle     oled;

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

__declspec(dllexport) void bridge_reset(uint32_t release_mode)
{
    mock_now = mock_resumes = mock_primask = 0;
    mock_faults = mock_fan_resumes = mock_buck_releases = 0;
    mock_drop_at                                        = UINT32_MAX;
    mock_next_voltage = mock_full_frame_voltage = mock_progress = 0;
    mock_voltage                                                = 20000;
    mock_pin                                                    = GPIO_PIN_SET;
    btn_initialized                                             = false;
#if BTN_SWD_TEST_HARNESS
    btn_swd_test_boot_hold = 0;
#endif
    btn_init();
    btn_set_short_press_mode(release_mode ? BTN_SHORT_PRESS_ON_RELEASE : BTN_SHORT_PRESS_ON_PRESS);
    battery_override_used = false;
    battery_set_params(7, BATT_MODE_LIPO);
}

__declspec(dllexport) void bridge_override(void)
{
    battery_show_fault(true);
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

__declspec(dllexport) void bridge_edge(uint32_t down)
{
#if BTN_SWD_TEST_HARNESS
    /* Schedule an ordinary virtual press, or its release, through the existing
     * SWD mailbox; use its boot-hold state for a host-controlled long hold. */
    if (down)
    {
        btn_swd_test_command_ms = 60000;
    }
    else
    {
        btn_swd_press_duration_ms = 0;
    }
    btn_task();
#else
    mock_pin = down ? GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_EXTI_Callback(BTN_PINn);
#endif
}

__declspec(dllexport) void bridge_tick(uint32_t elapsed)
{
    mock_now += elapsed;
    btn_task();
    btn_task(); /* Allow the SWD mailbox to acknowledge a debounced release. */
}

__declspec(dllexport) void bridge_consume(void)
{
    btn_discard_until_release();
}

__declspec(dllexport) void bridge_battery(uint32_t count, uint32_t mode, uint32_t voltage)
{
    battery_set_params((uint16_t)count, (uint8_t)mode);
    mock_voltage = (uint16_t)voltage;
}

__declspec(dllexport) void bridge_voltage_drop(uint32_t at_ms, uint32_t voltage)
{
    mock_drop_at      = at_ms;
    mock_next_voltage = voltage;
}

__declspec(dllexport) void bridge_final_frame_voltage(uint32_t voltage)
{
    mock_full_frame_voltage = voltage;
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

uint32_t systick_get_ms(void)
{
    return mock_now;
}

void HAL_Delay(uint32_t delay)
{
    mock_now += delay;
    if (mock_now >= mock_drop_at)
    {
        mock_voltage = (uint16_t)mock_next_voltage;
    }
}

void     watchdog_feed(void) {}

uint16_t adc_to_millivolts(uint8_t idx)
{
    return mock_voltage;
}

void rfgen_stop(void) {}

void rfgen_start(void)
{
    ++mock_resumes;
}

void pwrlvl_force_minimum(void) {}

void pwrlvl_release_minimum(void)
{
    ++mock_buck_releases;
}

void fan_stop(void) {}

void fan_resume(void)
{
    ++mock_fan_resumes;
}

u8g2_t* OLED_GetGraphics(OLED_Handle* handle)
{
    return &handle->graphics;
}

bool OLED_SendBuffer(OLED_Handle* handle)
{
    /* Model a new ADC reading arriving during the final full-bar I2C send. */
    if ((mock_progress == 32) && (mock_full_frame_voltage != 0))
    {
        mock_voltage = (uint16_t)mock_full_frame_voltage;
    }
    return true;
}

void fault_render(u8g2_t* graphics, const char* text, uint8_t x, int16_t y) {}

void u8g2_DrawBox(u8g2_t* graphics, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h)
{
    mock_progress = w;
}

void show_fault(const char* text, bool reset)
{
    ++mock_faults;
}

// -----------------------------------------------------------------------------
// Debug / Logging Helpers
// -----------------------------------------------------------------------------

__declspec(dllexport) uint32_t bridge_read(uint32_t selector)
{
    switch (selector)
    {
    case 0:
        return btn_has_short_press(false) | (btn_has_long_press(false) << 1);
    case 1:
        return btn_get_consecutive_presses();
    case 2:
        return btn_events_suppressed;
    case 3:
        return mock_resumes;
    case 4:
        return battery_cell_count;
    case 5:
        return mock_now;
    case 6:
        return mock_faults;
    case 7:
        return battery_override_used;
    case 8:
        return mock_fan_resumes;
    case 9:
        return mock_buck_releases;
    case 10:
        return battery_check();
    case 11:
        return mock_progress;
    default:
        return btn_is_down();
    }
}
