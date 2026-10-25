/* Reuse the established actual-C power fixture, adding normal-operation
 * observations and a pixel model for the real
 * graph renderer. */
#define NATIVE_FUNCTIONAL_GRAPH  1
#define NATIVE_FUNCTIONAL_BUTTON 1
#include "../faults/power_bridge.c"

void HAL_Delay(uint32_t ms);
int  HAL_Init(void);
#define main static unused_firmware_main
#include "../../src/main.c"
#undef main

static uint8_t     pixels[128][32], color;
static bool        down, long_event;
static uint32_t    consecutive, message_duration;
static const char* message;
static const char* warning_text[3];
static uint32_t    warning_rows, warning_y[3];

__declspec(dllexport) void normal_reset(uint32_t now)
{
    bridge_reset(now);
    down = long_event = false;
    consecutive = message_duration = 0;
    message                        = 0;
    debug_view_enabled             = false;
    debug_entry_release_pending    = false;
    main_reset_debug_peaks();
    debug_press_count = debug_last_press_ms = 0;
    pwrmgt_high_voltage_warning_active = pwrmgt_high_voltage_warning_visible = false;
#ifdef SHOW_SPLASH
    splash_visible = false;
#endif
    pwrmgt_blocked_change_count = 0;
    pwrmgt_change_direction_up  = false;
    pwrmgt_last_active_ms = pwrmgt_idle_started_ms = pwrmgt_history_last_update_ms = now;
    pwrmgt_is_idle                                                                 = false;
    pwrmgt_history_push_idx                                                        = 0;
    pwrmgt_idle_power_threshold_mw                                                 = 10000;
    for (uint32_t i = 0; i < 16; ++i)
    {
        pwrmgt_power_history_mw[i] = 0;
    }
}
__declspec(dllexport) void normal_mode(uint32_t mode)
{
    pwrmgt_set_desired_power_level(mode);
}
__declspec(dllexport) void normal_press(void)
{
    pwrmgt_change_pwr_lvl();
}
__declspec(dllexport) void normal_threshold(uint32_t selection)
{
    pwrmgt_set_idle_power_threshold(selection);
}
__declspec(dllexport) uint32_t normal_value(uint32_t selector)
{
    switch (selector)
    {
    case 0:
        return pwrmgt_desired_power_level;
    case 1:
        return pwrmgt_get_time_since_last_activity_ms();
    case 2:
        return pwrmgt_get_idle_duration_ms();
    case 3:
        return pwrmgt_history_push_idx;
    case 4:
        return consecutive;
    }
    return 0;
}
__declspec(dllexport) uint32_t normal_peak(uint32_t age)
{
    return pwrmgt_history_peek(age);
}
__declspec(dllexport) const uint8_t* normal_graph(void)
{
    for (uint32_t y = 0; y < 128; ++y)
    {
        for (uint32_t x = 0; x < 32; ++x)
        {
            pixels[y][x] = 0;
        }
    }
    u8g2_t graphics;
    pwrmgt_render_graph(&graphics);
    return &pixels[0][0];
}
void u8g2_SetDrawColor(u8g2_t* g, uint8_t value)
{
    color = value;
}
void u8g2_DrawPixel(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y)
{
    if (x < 32 && y < 128)
    {
        if (color == 2)
        {
            pixels[y][x] ^= 1;
        }
        else
        {
            pixels[y][x] = color != 0;
        }
    }
}
void u8g2_DrawVLine(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t height)
{
    for (uint32_t i = 0; i < height; ++i)
    {
        u8g2_DrawPixel(g, x, y + i);
    }
}
void u8g2_DrawBox(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t width, u8g2_uint_t height)
{
    for (uint32_t i = 0; i < width; ++i)
    {
        u8g2_DrawVLine(g, x + i, y, height);
    }
}

void        u8g2_SetFont(u8g2_t* g, const uint8_t* font) {}
u8g2_uint_t u8g2_DrawStr(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y, const char* text)
{
    if (warning_rows < 3)
    {
        warning_text[warning_rows] = text;
        warning_y[warning_rows++]  = y;
    }
    return 0;
}

__declspec(dllexport) void normal_button(uint32_t held, uint32_t long_press, uint32_t count)
{
    down        = held != 0;
    long_event  = long_press != 0;
    consecutive = count;
}
__declspec(dllexport) const char* normal_message(void)
{
    return message;
}
__declspec(dllexport) uint32_t normal_duration(void)
{
    return message_duration;
}
bool btn_has_long_press(bool clear)
{
    return long_event;
}
bool btn_is_down(void)
{
    return down;
}
void btn_reset_consecutive_presses(void)
{
    consecutive = 0;
}
uint32_t btn_get_consecutive_presses(void)
{
    return consecutive;
}
void show_short_msg(const char* text, uint32_t duration)
{
    message          = text;
    message_duration = duration;
}

/* Couple the actual runtime gesture with the actual power-selection/Easter-egg code. */
__declspec(dllexport) uint32_t normal_debug_press(uint32_t now)
{
    mock_now = now;
    ++consecutive;
    main_handle_short_press(now);
    return debug_view_enabled;
}

__declspec(dllexport) uint32_t normal_warning(uint32_t now, uint32_t mv)
{
    u8g2_t graphics = {0};
    graphics.width  = 32;
    graphics.height = 128;
    mock_now        = now;
    warning_rows    = 0;
    // Seed the model to prove the warning only clears the bottom overlay.
    for (uint32_t y = 0; y < 128; ++y)
    {
        for (uint32_t x = 0; x < 32; ++x)
        {
            pixels[y][x] = 1;
        }
    }
    pwrmgt_render_high_voltage_warning(&graphics, mv);
    return warning_rows;
}
__declspec(dllexport) const char* normal_warning_text(uint32_t row)
{
    return warning_text[row];
}
__declspec(dllexport) uint32_t normal_warning_value(uint32_t row)
{
    return warning_y[row];
}
__declspec(dllexport) uint32_t normal_warning_pixel(uint32_t y)
{
    return pixels[y][0];
}
