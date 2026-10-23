/* Exercise actual transient ownership/duration and saved terminal dim policy.
 * No terminal loop is entered in this
 * fixture. */
#include <stdint.h>
void HAL_Delay(uint32_t ms);
void NVIC_SystemReset(void);
#include "../../src/fault.c"
#include "../../src/miscutils.c"

OLED_Handle                oled;
static uint32_t            now_ms, sends, saved_dim;
const uint8_t              u8g2_font_6x10_tr[1] = {0}, u8g2_font_5x7_tr[1] = {0};
__declspec(dllexport) void message_reset(uint32_t now)
{
    now_ms            = now;
    sends             = 0;
    short_msg_text[0] = 0;
}
__declspec(dllexport) void message_start(uint32_t duration)
{
    show_short_msg("HELLO", duration);
}
__declspec(dllexport) uint32_t message_step(uint32_t now)
{
    now_ms = now;
    return short_msg_task();
}
__declspec(dllexport) uint32_t message_sends(void)
{
    return sends;
}
__declspec(dllexport) uint32_t message_dim_delay(uint32_t selection)
{
    saved_dim = selection;
    return fault_get_auto_dim_delay_ms();
}
uint32_t systick_get_ms(void)
{
    return now_ms;
}
void HAL_Delay(uint32_t ms)
{
    now_ms += ms;
}
void NVIC_SystemReset(void) {}
void rfgen_stop(void) {}
void pwrlvl_force_minimum(void) {}
void fan_stop(void) {}
void watchdog_feed(void) {}
void btn_init(void) {}
void btn_task(void) {}
bool btn_is_down(void)
{
    return false;
}
bool btn_has_short_press(bool clear)
{
    return false;
}
bool btn_has_long_press(bool clear)
{
    return false;
}
bool nvm_read(hotwand_setup_nvm_t* settings)
{
    settings->auto_dim = saved_dim;
    return true;
}
void nvm_apply_defaults(hotwand_setup_nvm_t* settings)
{
    settings->auto_dim = 0;
}
uint16_t adc_to_millivolts(uint8_t idx)
{
    return 24000;
}
u8g2_t* OLED_GetGraphics(OLED_Handle* handle)
{
    return &handle->graphics;
}
bool OLED_SendBuffer(OLED_Handle* handle)
{
    ++sends;
    return true;
}
bool OLED_SetDimMode(OLED_Handle* handle, bool dim)
{
    return true;
}
void        u8g2_ClearBuffer(u8g2_t* g) {}
void        u8g2_SetFont(u8g2_t* g, const uint8_t* font) {}
u8g2_uint_t u8g2_DrawStr(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y, const char* text)
{
    return 0;
}
