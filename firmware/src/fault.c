// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "fault.h"

#include "adc.h"
#include "button.h"
#include "conf.h"
#include "fan.h"
#include "miscutils.h"
#include "oled.h"
#include "pins.h"
#include "pwrlvl.h"
#include "rfgen.h"
#include "stm32f0xx_hal.h"
#include "systick.h"

#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define FAULT_DISPLAY_HEIGHT        32
#define FAULT_FONT_ASCENT           8
#define FAULT_LINE_HEIGHT           10
#define FAULT_REFRESH_INTERVAL_MS   200
#define FAULT_SHIFT_INTERVAL_MS     5000
#define FAULT_LINE_BUFFER_SIZE      6
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

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static uint8_t fault_count_message_lines(const char* text);
static void    fault_draw_line(u8g2_t* graphics, const char* line, uint8_t x_offset, int16_t baseline);
static void    fault_draw_text(u8g2_t* graphics, const char* text, uint8_t x_offset, int16_t first_baseline);
static uint8_t fault_random_x(void);
static void    fault_reset_if_button_pressed(void);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void show_fault(const char* text, bool allow_button_reset)
{
    u8g2_t*  graphics;
    uint32_t last_refresh_ms;
    uint32_t last_shift_ms;
    int16_t  text_height;
    int16_t  lower_offset;
    int16_t  upper_offset;
    int16_t  y_offset = 0;
    int8_t   direction;
    uint8_t  x_offset = 0;

    /* A fault screen is terminal until reset.  Put every power-producing or
     * moving output into its safe state before attempting any UI work. */
    rfgen_stop();
    pwrlvl_force_minimum();
    fan_stop();

    if (allow_button_reset)
    {
        btn_init();
        btn_has_short_press(true);
        btn_has_long_press(true);
        fault_button_reset_armed     = !btn_is_down();
        fault_button_release_pending = false;
    }

    graphics = OLED_GetGraphics(&oled);
    if (graphics == NULL)
    {
        for (;;)
        {
            if (allow_button_reset)
            {
                fault_reset_if_button_pressed();
            }
            HAL_Delay(1);
        }
    }

    text_height = (int16_t)((fault_count_message_lines(text) + 1) * FAULT_LINE_HEIGHT);
    if (text_height <= FAULT_DISPLAY_HEIGHT)
    {
        lower_offset = 0;
        upper_offset = (int16_t)(FAULT_DISPLAY_HEIGHT - text_height);
        direction    = 1;
    }
    else
    {
        lower_offset = (int16_t)(FAULT_DISPLAY_HEIGHT - text_height);
        upper_offset = 0;
        direction    = -1;
    }

    fault_render(graphics, text, x_offset, y_offset);
    OLED_SendBuffer(&oled);
    last_shift_ms   = systick_get_ms();
    last_refresh_ms = last_shift_ms;

    for (;;)
    {
        uint32_t now;

        if (allow_button_reset)
        {
            fault_reset_if_button_pressed();
        }
        now = systick_get_ms();

        if ((uint32_t)(now - last_shift_ms) >= FAULT_SHIFT_INTERVAL_MS)
        {
            last_shift_ms = now;
            y_offset      = (int16_t)(y_offset + direction);

            if (y_offset >= upper_offset)
            {
                y_offset  = upper_offset;
                direction = -1;
                x_offset  = fault_random_x();
            }
            else if (y_offset <= lower_offset)
            {
                y_offset  = lower_offset;
                direction = 1;
                x_offset  = fault_random_x();
            }
        }

        if ((uint32_t)(now - last_refresh_ms) >= FAULT_REFRESH_INTERVAL_MS)
        {
            last_refresh_ms = now;
            fault_render(graphics, text, x_offset, y_offset);
            OLED_SendBuffer(&oled);
        }

        HAL_Delay(1);
    }
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
    short_msg_text[copied] = '\0';
    short_msg_start_ms     = systick_get_ms();
    short_msg_duration_ms  = duration_ms;

    /* Backdate the frame timestamp so the next task call draws immediately. */
    short_msg_last_frame_ms = short_msg_start_ms - SHORT_MSG_FRAME_INTERVAL_MS;
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
        fault_draw_text(graphics, short_msg_text, 1, FAULT_FONT_ASCENT);
        OLED_SendBuffer(&oled);
    }

    return true;
}

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
    baseline = (int16_t)(y_offset + FAULT_FONT_ASCENT);
    fault_draw_line(graphics, voltage, x_offset, baseline);
    fault_draw_text(graphics, text, x_offset, (int16_t)(baseline + FAULT_LINE_HEIGHT));
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

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

static void fault_draw_line(u8g2_t* graphics, const char* line, uint8_t x_offset, int16_t baseline)
{
    if (baseline >= 0)
    {
        u8g2_DrawStr(graphics, (u8g2_uint_t)x_offset, (u8g2_uint_t)baseline, line);
    }
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
        baseline = (int16_t)(baseline + FAULT_LINE_HEIGHT);
    }
}

static uint8_t fault_random_x(void)
{
    return (uint8_t)((uint32_t)hotwand_rand() % ((uint32_t)OLED_MAX_PIXEL_SHIFT_X + 1));
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
    }
}
