/* Actual main.c helpers and application loop with mocked time/OLED/inputs.
 * A native non-local return ends loop observations at the watchdog boundary. */
#include <stdint.h>
void HAL_Delay(uint32_t ms);
int  HAL_Init(void);
#define main static unused_firmware_main
#include "../../src/main.c"
#undef main
#include "../../src/miscutils.c"

OLED_Handle                oled;
I2C_HandleTypeDef          i2c1;
static uint32_t            now_ms, scenario, reasons, sends, graph_calls, feeds, setup_calls, release_at;
static uint32_t            text_x, text_y;
static char                drawn_text[32];
static const char*         sleep_text;
static void*               loop_context[5];
static bool                running_main, short_event, long_event;
static uint32_t            stop_at, quantum, dim_commands, mode_changes, runtime_start, runtime_mark;
static uint32_t            event_at, second_event_at, last_activity, transient_until;
static hotwand_setup_nvm_t boot_settings;
const uint8_t              u8g2_font_6x10_tr[1] = {0};

__declspec(dllexport) void main_reset(uint32_t now);

/* Execute the unchanged infinite main loop. A native non-local return at
 * watchdog_feed terminates the fixture after the requested observation time;
 * no loop condition, timer, or production branch is rewritten. */
__declspec(dllexport) uint32_t main_run(uint32_t duration,
                                        uint32_t dim,
                                        uint32_t sleep,
                                        uint32_t splash,
                                        uint32_t first_press,
                                        uint32_t next_press,
                                        uint32_t message_until)
{
    main_reset(0);
    release_at  = 0;
    sleep_text  = 0;
    short_event = long_event = false;
    dim_commands = mode_changes = runtime_mark = 0;
    boot_settings                              = (hotwand_setup_nvm_t){0};
    boot_settings.auto_dim                     = dim;
    boot_settings.auto_sleep                   = sleep;
    boot_settings.show_splash                  = splash;
    stop_at                                    = duration;
    quantum                                    = duration > 10000 ? 1000 : 1;
    event_at                                   = first_press;
    second_event_at                            = next_press;
    transient_until                            = message_until;
    running_main                               = true;
    if (__builtin_setjmp(loop_context) == 0)
    {
        unused_firmware_main();
    }
    running_main = false;
    return sleep_text != 0;
}
__declspec(dllexport) void main_reset(uint32_t now)
{
    now_ms  = now;
    reasons = sends = graph_calls = feeds = setup_calls = scenario = 0;
    release_at                                                     = UINT32_MAX;
    drawn_text[0]                                                  = 0;
    pixshift_x = pixshift_y = 0;
#ifdef SHOW_SPLASH
    splash_visible = false;
#endif
}

__declspec(dllexport) void main_render(uint32_t now, uint32_t attenuation)
{
    now_ms  = now;
    reasons = attenuation;
    main_render_display();
}

__declspec(dllexport) void main_splash(uint32_t started, uint32_t visible)
{
#ifdef SHOW_SPLASH
    splash_started_ms = started;
    splash_visible    = visible != 0;
#endif
}

__declspec(dllexport) uint32_t main_wait(uint32_t voltage_scenario)
{
    scenario       = voltage_scenario;
    uint32_t start = now_ms;
    boot_wait_for_power_stable();
    return now_ms - start;
}

__declspec(dllexport) void main_hold(uint32_t release_ms)
{
    release_at = now_ms + release_ms;
    boot_check_for_setup();
}

__declspec(dllexport) const char* main_text(void)
{
    return drawn_text;
}

__declspec(dllexport) uint32_t main_value(uint32_t selector)
{
    switch (selector)
    {
    case 0:
        return sends;
    case 1:
        return graph_calls;
    case 2:
        return feeds;
    case 3:
        return setup_calls;
    case 4:
        return now_ms;
    case 5:
        return text_x;
    case 6:
        return text_y;
    }
    return 0;
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
    if (running_main && runtime_mark)
    {
        now_ms += quantum;
        if ((uint32_t)(now_ms - runtime_start) >= stop_at)
        {
            __builtin_longjmp(loop_context, 1);
        }
    }
}
uint16_t adc_to_millivolts(uint8_t index)
{
    if (scenario == 1)
    {
        return 24000 + now_ms;
    }
    if (scenario == 2)
    {
        return 24000 - now_ms;
    }
    return 24000;
}
uint8_t pwrmgt_get_attenuation_reasons(void)
{
    return reasons;
}
void pwrmgt_render_graph(u8g2_t* g)
{
    ++graph_calls;
}
void    pwrmgt_render_high_voltage_warning(u8g2_t* g, uint16_t mv) {}
u8g2_t* OLED_GetGraphics(OLED_Handle* handle)
{
    return &handle->graphics;
}
bool OLED_SendBuffer(OLED_Handle* handle)
{
    ++sends;
    return true;
}
void        u8g2_ClearBuffer(u8g2_t* g) {}
void        u8g2_SetFont(u8g2_t* g, const uint8_t* font) {}
void        u8g2_SetDrawColor(u8g2_t* g, uint8_t color) {}
void        u8g2_DrawBox(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h) {}
u8g2_uint_t u8g2_DrawStr(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y, const char* text)
{
    uint32_t i = 0;
    while (text[i] && i < sizeof(drawn_text) - 1)
    {
        drawn_text[i] = text[i];
        ++i;
    }
    drawn_text[i] = 0;
    text_x        = x;
    text_y        = y;
    return i * 6;
}
void btn_task(void)
{
    if (running_main && runtime_mark)
    {
        uint32_t elapsed = now_ms - runtime_start;
        if (elapsed == event_at || elapsed == second_event_at)
        {
            short_event = true;
        }
    }
}
bool btn_is_down(void)
{
    return now_ms < release_at;
}
bool btn_has_short_press(bool clear)
{
    bool event = short_event;
    if (clear)
    {
        short_event = false;
    }
    return event;
}
bool btn_has_long_press(bool clear)
{
    return false;
}
void setup_menu(void)
{
    ++setup_calls;
}

/* Configuration and terminal delegation are tested without entering the
 * production infinite supervisor loop. Target cases cover that integration. */
void show_fault(const char* text, bool allow_reset)
{
    sleep_text = text;
    if (running_main)
    {
        __builtin_longjmp(loop_context, 1);
    }
}
__declspec(dllexport) const char* main_sleep(void)
{
    enter_sleep_mode();
    return sleep_text;
}
__declspec(dllexport) uint32_t main_delay(uint32_t dim, uint32_t selection)
{
    return dim ? auto_dim_delay_ms[selection] : auto_sleep_delay_ms[selection];
}
__declspec(dllexport) const char* format_voltage(uint32_t value)
{
    static char formatted[32];
    millivolts_to_str(value, formatted, 1, NULL);
    return formatted;
}
__declspec(dllexport) uint32_t utility_checksum(const uint8_t* bytes, uint32_t length)
{
    return fletcher16(bytes, length);
}
__declspec(dllexport) uint32_t utility_random(uint32_t seed)
{
    hotwand_srand(seed);
    return hotwand_rand();
}

__declspec(dllexport) uint32_t main_loop_value(uint32_t selection)
{
    if (selection == 0)
    {
        return oled.dimmed;
    }
    if (selection == 1)
    {
        return dim_commands;
    }
    if (selection == 2)
    {
        return mode_changes;
    }
    return now_ms - runtime_start;
}
int HAL_Init(void)
{
    return 0;
}
void test_run(void) {}
void rfgen_stop(void) {}
void rfgen_emergency_stop(void) {}
bool rfgen_clock_init(void)
{
    return true;
}
bool watchdog_init(void)
{
    return true;
}
void systick_init(void) {}
void adc_init(void) {}
void btn_init(void) {}
void I2C1_Init(void) {}
bool OLED_Init(OLED_Handle* handle, I2C_HandleTypeDef* i2c)
{
    handle->dimmed = 0;
    return true;
}
void OLED_ConfigureGraphics(OLED_Handle* handle) {}
void nvm_init(void) {}
bool nvm_read(hotwand_setup_nvm_t* settings)
{
    *settings = boot_settings;
    return true;
}
void nvm_apply_defaults(hotwand_setup_nvm_t* settings)
{
    *settings = boot_settings;
}
void     adc_set_input_voltage_calibration(uint8_t selection) {}
uint32_t adc_get_rand_seed(void)
{
    return 1;
}
void show_splash(void) {}
void UART_SetAllowed(bool allowed) {}
bool battery_guess(uint16_t voltage, uint8_t mode, battery_guess_t* guess)
{
    return false;
}
bool battery_set_params(uint16_t count, uint8_t mode)
{
    return true;
}
void tipdetect_init(void) {}
void tipdetect_task(void) {}
bool tipdetect_has_triggered(void)
{
    return false;
}
bool rfgen_has_clock_fault(void)
{
    return false;
}
void    pwrlvl_init(void) {}
void    pwrmgt_set_desired_power_level(pwrlvl_mode_t mode) {}
void    pwrmgt_set_idle_power_threshold(uint8_t selection) {}
uint8_t fan_normalize_mode(uint8_t mode)
{
    return mode;
}
void fan_init(uint8_t mode, bool inverted)
{
    runtime_mark  = 1;
    runtime_start = now_ms;
    last_activity = now_ms;
}
void pwrmgt_task(void) {}
bool rfgen_is_active(void)
{
    return false;
}
bool rfgen_tip_allows_start(void)
{
    return true;
}
void rfgen_start(void) {}
void ntc_task(void) {}
void fan_task(void) {}
void UART_debug_task(void) {}
void pwrmgt_change_pwr_lvl(void)
{
    ++mode_changes;
    last_activity = now_ms;
}
uint32_t pwrmgt_get_time_since_last_activity_ms(void)
{
    return now_ms - last_activity;
}
bool OLED_SetDimMode(OLED_Handle* handle, bool dimmed)
{
    if (handle->dimmed != dimmed)
    {
        ++dim_commands;
    }
    handle->dimmed = dimmed;
    return true;
}
bool short_msg_task(void)
{
    return (uint32_t)(now_ms - runtime_start) < transient_until;
}

__declspec(dllexport) const char* format_integer(int value, int base)
{
    static char formatted[40];
    int_to_str(value, formatted, base, NULL);
    return formatted;
}
