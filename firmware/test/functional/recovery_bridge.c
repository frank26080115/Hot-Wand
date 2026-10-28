/* Actual returning fault-screen loop with controlled engineering-value ADC
 * samples. Shutdown, timing, preemption and button ownership are observable. */
#include <stdint.h>
void HAL_Delay(uint32_t ms);
void NVIC_SystemReset(void);
#include "../../src/fault.c"
#include "../../src/miscutils.c"

OLED_Handle     oled;
const uint8_t   u8g2_font_6x10_tr[1] = {0}, u8g2_font_5x7_tr[1] = {0};
static uint32_t now_ms, start_ms, stop_ms, elapsed_ms, feeds, sends, resets, discards, inits;
static uint32_t rise_at, dip_at, dip_length, healthy_mv, fail_at, fail_kind, rearm_at, rearms;
static uint32_t mode, held, event, dimmed, reset_press;
static bool     restart_latched;
static bool     rf_stopped, buck_stopped, fan_stopped, no_graphics, returned, deny_rearm;
static void*    context[5];

__declspec(dllexport) uint32_t recovery_run(uint32_t start,
                                            uint32_t voltage,
                                            uint32_t rise,
                                            uint32_t dip,
                                            uint32_t length,
                                            uint32_t limit,
                                            uint32_t failure,
                                            uint32_t kind,
                                            uint32_t options)
{
    now_ms = start_ms = start;
    healthy_mv        = voltage;
    rise_at           = rise;
    dip_at            = dip;
    dip_length        = length;
    stop_ms           = limit;
    fail_at           = failure;
    fail_kind         = kind;
    feeds = sends = resets = discards = inits = rearms = elapsed_ms = 0;
    restart_latched                                                 = false;
    returned = rf_stopped = buck_stopped = fan_stopped = false;
    no_graphics                                        = options & 1;
    deny_rearm                                         = options & 2;
    mode                                               = 1;
    held                                               = options & 4;
    event                                              = 0;
    reset_press                                        = options & 16;
    oled.graphics.width                                = 32;
    oled.graphics.height                               = 128;
    if (__builtin_setjmp(context) == 0)
    {
        if (options & 8)
        {
            show_undervoltage_fault();
        }
        else
        {
            show_power_loss_fault();
        }
        returned   = true;
        elapsed_ms = now_ms - start_ms;
    }
    return returned;
}
__declspec(dllexport) uint32_t recovery_value(uint32_t index)
{
    switch (index)
    {
    case 0:
        return elapsed_ms;
    case 1:
        return rf_stopped && buck_stopped && fan_stopped;
    case 2:
        return feeds;
    case 3:
        return sends;
    case 4:
        return resets;
    case 5:
        return discards;
    case 6:
        return inits;
    case 7:
        return rearms;
    case 8:
        return mode;
    case 9:
        return event;
    case 10:
        return rearm_at;
    default:
        return dimmed;
    }
}
uint32_t systick_get_ms(void)
{
    return now_ms;
}
void HAL_Delay(uint32_t ms)
{
    now_ms += ms;
}
void watchdog_feed(void)
{
    ++feeds;
    elapsed_ms = now_ms - start_ms;
    if (elapsed_ms >= stop_ms)
    {
        __builtin_longjmp(context, 1);
    }
}
void NVIC_SystemReset(void)
{
    ++resets;
    __builtin_longjmp(context, 1);
}
void rfgen_stop(void)
{
    rf_stopped = true;
}
void rfgen_power_loss_stop(void)
{
    rf_stopped = restart_latched = true;
}
void pwrlvl_force_minimum(void)
{
    buck_stopped = true;
}
void fan_stop(void)
{
    fan_stopped = true;
}
bool tipdetect_has_triggered(void)
{
    return fail_kind == 1 && now_ms - start_ms >= fail_at;
}
bool rfgen_has_clock_fault(void)
{
    return fail_kind == 2 && now_ms - start_ms >= fail_at;
}
bool adc_has_buck_voltage_spike_shutdown(void)
{
    return fail_kind == 3 && now_ms - start_ms >= fail_at;
}
bool adc_rearm_power_loss(void)
{
    ++rearms;
    rearm_at = now_ms - start_ms;
    if (deny_rearm || !restart_latched)
    {
        return false;
    }
    restart_latched = false;
    return true;
}
void btn_init(void)
{
    ++inits;
    mode = 0;
}
void btn_task(void)
{
    if (reset_press && now_ms - start_ms >= 100)
    {
        held = 1;
    }
}
bool btn_is_down(void)
{
    return held != 0;
}
bool btn_has_short_press(bool clear)
{
    bool result = event;
    if (clear)
    {
        event = 0;
    }
    return result;
}
bool btn_has_long_press(bool clear)
{
    return false;
}
void btn_discard_until_release(void)
{
    ++discards;
    event = 0;
}
bool nvm_read(hotwand_setup_nvm_t* settings)
{
#if OLED_DIMMING_ENABLED
    settings->auto_dim = 1;
#endif
    return true;
}
void nvm_apply_defaults(hotwand_setup_nvm_t* settings)
{
#if OLED_DIMMING_ENABLED
    settings->auto_dim = 0;
#endif
}
uint16_t adc_to_millivolts(uint8_t index)
{
    uint32_t elapsed = now_ms - start_ms;
    if (elapsed < rise_at)
    {
        return 12000;
    }
    if (elapsed >= dip_at && elapsed - dip_at < dip_length)
    {
        return 14000;
    }
    return healthy_mv;
}
u8g2_t* OLED_GetGraphics(OLED_Handle* handle)
{
    return no_graphics ? NULL : &handle->graphics;
}
bool OLED_SendBuffer(OLED_Handle* handle)
{
    ++sends;
    return true;
}
bool OLED_SetDimMode(OLED_Handle* handle, bool dim)
{
    dimmed = dim;
    return true;
}
void        u8g2_ClearBuffer(u8g2_t* graphics) {}
void        u8g2_SetFont(u8g2_t* graphics, const uint8_t* font) {}
u8g2_uint_t u8g2_DrawStr(u8g2_t* graphics, u8g2_uint_t x, u8g2_uint_t y, const char* text)
{
    return 0;
}
