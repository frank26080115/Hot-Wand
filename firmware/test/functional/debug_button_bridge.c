/* Actual button classification and runtime debug handling, with hardware and
 * power-selection boundaries mocked.
 * Release/long-event timing is real C. */
#include "../button_modal/bridge.c"
int HAL_Init(void);
#define main static unused_firmware_main
#include "../../src/main.c"
#undef main

static uint32_t selections;

__declspec(dllexport) void debug_button_reset(void)
{
    bridge_reset(0);
    selections         = 0;
    debug_view_enabled = debug_entry_release_pending = false;
    debug_press_count = debug_last_press_ms = 0;
    main_reset_debug_peaks();
#ifdef SHOW_SPLASH
    splash_visible = false;
#endif
}

__declspec(dllexport) void debug_button_poll(void)
{
    main_handle_runtime_button(mock_now);
}

__declspec(dllexport) uint32_t debug_button_value(uint32_t index)
{
    switch (index)
    {
    case 0:
        return debug_view_enabled;
    case 1:
        return selections;
    case 2:
        return debug_peaks[0];
    case 3:
        return btn_short_press_mode;
    case 4:
        return debug_entry_release_pending;
    default:
        return btn_get_consecutive_presses();
    }
}

void pwrmgt_change_pwr_lvl(void)
{
    ++selections;
}

uint16_t adc_to_milliamps(uint8_t index)
{
    return 1000;
}

uint32_t adc_get_milliwatts(void)
{
    return mock_voltage;
}

uint16_t adc_to_celcius(uint8_t index)
{
    return 25;
}
