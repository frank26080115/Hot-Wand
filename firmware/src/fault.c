/*
Ordinary fault displays are terminal until reset. Power-loss and undervoltage
displays can return after qualified input recovery. Both stop outputs, animate to avoid OLED
burn-in, and follow the saved dimming policy.
*/

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "fault_test.h"
#include "fault.h"

#include "adc.h"
#include "button.h"
#include "conf.h"
#include "fan.h"
#include "miscutils.h"
#include "nvm.h"
#include "oled.h"
#include "pins.h"
#include "pwrlvl.h"
#include "rfgen.h"
#include "stm32f0xx_hal.h"
#include "systick.h"
#include "tipdetect.h"
#include "watchdog.h"

#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define FAULT_FONT_ASCENT           8
#define FAULT_REFRESH_INTERVAL_MS   200
#define FAULT_SHIFT_INTERVAL_MS     5000
#define FAULT_BIG_FONT_MAX_CHARS    5
#define FAULT_LINE_BUFFER_SIZE      7
#define FAULT_VOLTAGE_BUFFER_SIZE   8
#define SHORT_MSG_BUFFER_SIZE       72
#define SHORT_MSG_FRAME_INTERVAL_MS 100

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static char     short_msg_text[SHORT_MSG_BUFFER_SIZE];
static uint32_t short_msg_start_ms;
static uint32_t short_msg_duration_ms;
static uint32_t short_msg_last_frame_ms;
static uint32_t fault_button_release_started_ms;
static bool     fault_button_reset_armed;
static bool     fault_button_release_pending;
static bool     input_recovery_pending;
static uint32_t input_recovered_since_ms;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void     fault_show_recoverable_input(const char* text);
static bool     fault_input_recovered(uint32_t now);
static void     fault_show(const char* text, bool allow_button_reset, bool recover_input);
static uint8_t  fault_count_message_lines(const char* text);
static size_t   fault_get_max_line_length(const char* text);
static bool     fault_uses_big_font(const char* text);
static void     fault_draw_line(u8g2_t* graphics, const char* line, uint8_t x_offset, int16_t baseline);
static void     fault_draw_text(u8g2_t* graphics, const char* text, uint8_t x_offset, int16_t first_baseline);
static uint8_t  fault_random_x(const char* text);
static void     fault_reset_if_button_pressed(void);
static uint32_t fault_get_auto_dim_delay_ms(void);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

__attribute__((noreturn)) void show_fault(const char* text, bool allow_button_reset)
{
    fault_show(text, allow_button_reset, false);
    __builtin_unreachable(); // Only dedicated input-supply fault entry points can return.
}

void show_power_loss_fault(void)
{
    fault_show_recoverable_input("POWER\nLOSS\nFAULT");
}

void show_undervoltage_fault(void)
{
    fault_show_recoverable_input("LOW\nVOLT\nFAULT");
}

static void fault_show_recoverable_input(const char* text)
{
    // Both input-fault causes share this selective RF restart gate.
    rfgen_power_loss_stop();
    input_recovery_pending = false;
    fault_show(text, true, true);
}

static void fault_show(const char* text, bool allow_button_reset, bool recover_input)
{
    // One fault screen owns this state; terminal preemption never returns.
    static u8g2_t*  graphics;
    static uint32_t auto_dim_delay_ms;
    static uint32_t last_activity_ms;
    static uint32_t last_refresh_ms;
    static uint32_t last_shift_ms;
    static int16_t  text_height;
    static int16_t  display_height;
    static int16_t  lower_offset;
    static int16_t  upper_offset;
    static int16_t  y_offset;
    static int8_t   direction;
    static uint8_t  x_offset;

    x_offset = y_offset = 0;

    /* Stop every output before UI work. Recovery returns with these outputs
     * still stopped; the supervisor owns their subsequent release. */
    rfgen_stop();
    pwrlvl_force_minimum();
    fan_stop();

    fault_test_fault(text, allow_button_reset);
    if (!recover_input)
    {
        btn_init();
    }
    btn_has_short_press(true);
    btn_has_long_press(true);

    if (allow_button_reset)
    {
        fault_button_reset_armed     = !btn_is_down();
        fault_button_release_pending = false;
    }

    auto_dim_delay_ms = fault_get_auto_dim_delay_ms();
    OLED_SetDimMode(&oled, false);

    graphics = OLED_GetGraphics(&oled);
    /* Graphics coordinates follow the configured OLED rotation. Use its
     * logical height (128 in portrait mode), not the controller's native 32. */
    if (graphics != NULL)
    {
        display_height = (int16_t)u8g2_GetDisplayHeight(graphics);
        text_height    = (int16_t)((fault_count_message_lines(text) + 1) * OLED_TEXT_LINE_HEIGHT);
        if (text_height <= display_height)
        {
            lower_offset = 0;
            upper_offset = (int16_t)(display_height - text_height);
            direction    = 1;
        }
        else
        {
            lower_offset = (int16_t)(display_height - text_height);
            upper_offset = 0;
            direction    = -1;
        }

        fault_render(graphics, text, x_offset, y_offset);
        OLED_SendBuffer(&oled);
    }
    last_shift_ms    = systick_get_ms();
    last_refresh_ms  = last_shift_ms;
    last_activity_ms = last_shift_ms;

    for (;;)
    {
        uint32_t now;

        if (allow_button_reset)
        {
            fault_reset_if_button_pressed();
        }
        else
        {
            btn_task();
            if (btn_has_short_press(true))
            {
                last_activity_ms = systick_get_ms();
            }
        }
        now = systick_get_ms();

        if (recover_input && fault_input_recovered(now))
        {
            // Preserve debug button classification and consume the current gesture.
            btn_discard_until_release();
            show_short_msg(NULL, 0);
#if HOT_WAND_TEST_HARNESS
            fault_test.message  = 0;
            fault_test.event_ms = fault_test.event_round = fault_test.event_raw = 0;
            for (uint8_t i = 0; i < 7; ++i)
            {
                fault_test.raw[i] = 0;
            }
            fault_test.flags &= ~(1 | 16 | 32);
            // Runtime is published only by main after supervision/output release.
            fault_test.stage = FT_RESUMING;
#endif
            return;
        }

        OLED_SetDimMode(&oled, (auto_dim_delay_ms != 0) && ((uint32_t)(now - last_activity_ms) >= auto_dim_delay_ms));

        if (graphics != NULL && (uint32_t)(now - last_shift_ms) >= FAULT_SHIFT_INTERVAL_MS)
        {
            last_shift_ms = now;
            y_offset      = (int16_t)(y_offset + direction);

            if (y_offset >= upper_offset)
            {
                y_offset  = upper_offset;
                direction = -1;
                x_offset  = fault_random_x(text);
            }
            else if (y_offset <= lower_offset)
            {
                y_offset  = lower_offset;
                direction = 1;
                x_offset  = fault_random_x(text);
            }
        }

        if (graphics != NULL && (uint32_t)(now - last_refresh_ms) >= FAULT_REFRESH_INTERVAL_MS)
        {
            last_refresh_ms = now;
            fault_render(graphics, text, x_offset, y_offset);
            OLED_SendBuffer(&oled);
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

bool short_msg_task(void)
{
    u8g2_t*  graphics;
    uint32_t now;

    if (short_msg_text[0] == '\0')
    {
        return false;
    }

    now = systick_get_ms();
    if ((uint32_t)(now - short_msg_start_ms) >= short_msg_duration_ms)
    {
        FT_SET(message, 0);
#if HOT_WAND_TEST_HARNESS
        fault_test.flags &= ~32;
#endif
        short_msg_text[0]     = '\0';
        short_msg_duration_ms = 0;
        return false;
    }

    /* Retain display ownership so the normal screen cannot overwrite the message between frames. */
    if ((uint32_t)(now - short_msg_last_frame_ms) < SHORT_MSG_FRAME_INTERVAL_MS)
    {
        return true;
    }

    graphics = OLED_GetGraphics(&oled);
    if (graphics != NULL)
    {
        short_msg_last_frame_ms = now;
        u8g2_ClearBuffer(graphics);
        u8g2_SetFont(graphics, fault_uses_big_font(short_msg_text) ? u8g2_font_6x10_tr : u8g2_font_5x7_tr);
        fault_draw_text(graphics, short_msg_text, 1, FAULT_FONT_ASCENT);
        OLED_SendBuffer(&oled);
    }

    return true;
}

void show_short_msg(const char* text, uint32_t duration_ms)
{
    uint8_t copied = 0;

    short_msg_text[0]     = '\0';
    short_msg_duration_ms = 0;

    if ((text == NULL) || (*text == '\0') || (duration_ms == 0))
    {
        return;
    }

    while ((*text != '\0') && (copied < (SHORT_MSG_BUFFER_SIZE - 1)))
    {
        short_msg_text[copied++] = *text++;
    }
    FT_SET(message, (uint32_t)short_msg_text);
#if HOT_WAND_TEST_HARNESS
    fault_test.flags |= 32;
#endif
    short_msg_text[copied] = '\0';
    short_msg_start_ms     = systick_get_ms();
    short_msg_duration_ms  = duration_ms;

    /* Backdate the frame timestamp so the next task call draws immediately. */
    short_msg_last_frame_ms = short_msg_start_ms - SHORT_MSG_FRAME_INTERVAL_MS;
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

/* Keep recovery qualification out of the animation loop to avoid carrying
 * its state through graphics/I2C calls on the small Cortex-M0 register set. */
static bool fault_input_recovered(uint32_t now)
{
    const char* terminal_fault = tipdetect_has_triggered()               ? "TIP\nFAULT"
                                 : rfgen_has_clock_fault()               ? "CLOCK\nFAULT"
                                 : adc_has_buck_voltage_spike_shutdown() ? "VOLT\nSPIKE\nFAULT"
                                                                         : NULL;
    if (terminal_fault != NULL)
    {
        FT_SET(stage, FT_RESUMING); // Permit a new, higher-priority diagnosis.
        show_fault(terminal_fault, true);
    }

    if (adc_to_millivolts(DC_SENS_IDX) <= DC_UNDERVOLTAGE_FAULT_MV)
    {
        input_recovery_pending = false;
    }
    else if (!input_recovery_pending)
    {
        input_recovered_since_ms = now;
        input_recovery_pending   = true;
    }
    else if ((uint32_t)(now - input_recovered_since_ms) >= PWRMGT_POWER_LOSS_RECOVERY_MS)
    {
        return adc_rearm_power_loss();
    }
    return false;
}

static void fault_reset_if_button_pressed(void)
{
    uint32_t now;

    btn_task();

    /* A held button may be the long press that entered sleep.  Require its
     * debounced release before a subsequent press is allowed to reset. */
    if (!fault_button_reset_armed)
    {
        if (btn_is_down())
        {
            fault_button_release_pending = false;
            return;
        }

        now = systick_get_ms();
        if (!fault_button_release_pending)
        {
            fault_button_release_started_ms = now;
            fault_button_release_pending    = true;
            return;
        }

        if ((uint32_t)(now - fault_button_release_started_ms) < BTN_DEBOUNCE_MS)
        {
            return;
        }

        btn_has_short_press(true);
        btn_has_long_press(true);
        fault_button_reset_armed     = true;
        fault_button_release_pending = false;
        return;
    }

    if (btn_is_down() || btn_has_short_press(true))
    {
        NVIC_SystemReset();

        /* Deliberately expire IWDG if a debugger suppresses the reset. */
        for (;;)
        {
        }
    }
}

// -----------------------------------------------------------------------------
// Debug / Logging Helpers
// -----------------------------------------------------------------------------

void fault_render(u8g2_t* graphics, const char* text, uint8_t x_offset, int16_t y_offset)
{
    char    voltage[FAULT_VOLTAGE_BUFFER_SIZE];
    size_t  voltage_length;
    int16_t baseline;

    if (graphics == NULL)
    {
        return;
    }

    millivolts_to_str(adc_to_millivolts(DC_SENS_IDX), voltage, 1, &voltage_length);
    voltage[voltage_length++] = 'V';
    voltage[voltage_length]   = '\0';

    u8g2_ClearBuffer(graphics);
    u8g2_SetFont(graphics, fault_uses_big_font(text) ? u8g2_font_6x10_tr : u8g2_font_5x7_tr);
    baseline = (int16_t)(y_offset + FAULT_FONT_ASCENT);
    fault_draw_line(graphics, voltage, x_offset, baseline);
    fault_draw_text(graphics, text, x_offset, (int16_t)(baseline + OLED_TEXT_LINE_HEIGHT));
}

static void fault_draw_text(u8g2_t* graphics, const char* text, uint8_t x_offset, int16_t first_baseline)
{
    char    line[FAULT_LINE_BUFFER_SIZE];
    int16_t baseline = first_baseline;
    uint8_t copied;

    while ((text != NULL) && (*text != '\0'))
    {
        copied = 0;
        while ((*text != '\0') && (*text != '\n'))
        {
            if (copied < (FAULT_LINE_BUFFER_SIZE - 1))
            {
                line[copied++] = *text;
            }
            ++text;
        }
        line[copied] = '\0';
        if (*text == '\n')
        {
            ++text;
        }

        fault_draw_line(graphics, line, x_offset, baseline);
        baseline = (int16_t)(baseline + OLED_TEXT_LINE_HEIGHT);
    }
}

static size_t fault_get_max_line_length(const char* text)
{
    size_t current_length = 0;
    size_t maximum_length = 0;

    while ((text != NULL) && (*text != '\0'))
    {
        if (*text++ == '\n')
        {
            if (current_length > maximum_length)
            {
                maximum_length = current_length;
            }
            current_length = 0;
        }
        else
        {
            ++current_length;
        }
    }

    return current_length > maximum_length ? current_length : maximum_length;
}

static uint8_t fault_count_message_lines(const char* text)
{
    uint8_t count = 0;

    if ((text != NULL) && (*text != '\0'))
    {
        count = 1;
        while (*text != '\0')
        {
            if (*text++ == '\n')
            {
                ++count;
            }
        }
    }

    return count;
}

static bool fault_uses_big_font(const char* text)
{
    // The uint16_t millivolt conversion cannot reach 100 V, so the voltage
    // line including its one decimal and V always fits five characters.
    return fault_get_max_line_length(text) <= FAULT_BIG_FONT_MAX_CHARS;
}

static void fault_draw_line(u8g2_t* graphics, const char* line, uint8_t x_offset, int16_t baseline)
{
    if (baseline >= 0)
    {
        u8g2_DrawStr(graphics, (u8g2_uint_t)x_offset, (u8g2_uint_t)baseline, line);
    }
}

static uint8_t fault_random_x(const char* text)
{
    uint8_t maximum_shift =
        fault_uses_big_font(text) ? OLED_MAX_PIXEL_SHIFT_X_BIGFONT : OLED_MAX_PIXEL_SHIFT_X_SMALLFONT;

    return (uint8_t)((uint32_t)hotwand_rand() % ((uint32_t)maximum_shift + 1));
}

static uint32_t fault_get_auto_dim_delay_ms(void)
{
    hotwand_setup_nvm_t settings;

    if (!nvm_read(&settings))
    {
        nvm_apply_defaults(&settings);
    }

    switch (settings.auto_dim)
    {
    case AUTO_DIM_15SEC:
        return 15 * 1000;

    case AUTO_DIM_30SEC:
        return 30 * 1000;

    case AUTO_DIM_60SEC:
        return 60 * 1000;

    case AUTO_DIM_OFF:
    default:
        return 0;
    }
}
