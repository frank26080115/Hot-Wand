#include "fault.h"

#include "adc.h"
#include "button.h"
#include "conf.h"
#include "miscutils.h"
#include "oled.h"
#include "pins.h"
#include "stm32f0xx_hal.h"
#include "systick.h"

#include <stddef.h>
#include <stdint.h>

#define OLED_HEIGHT 32
#define FAULT_SHIFT_INTERVAL_MS 5000UL
#define FAULT_LINE_BUFFER_SIZE 32U
#define FAULT_VOLTAGE_BUFFER_SIZE 8U
#define FAULT_MAX_MESSAGE_LINES 254U

static uint16_t Fault_CountMessageLines(const char *text);
static const char *Fault_CopyLine(const char *text,
                                  char *line,
                                  size_t line_capacity);
static void Fault_DrawLine(u8g2_t *graphics,
                           const char *line,
                           uint8_t x_offset,
                           int16_t baseline,
                           int16_t ascent,
                           int16_t descent);
static void Fault_Render(u8g2_t *graphics,
                         const char *text,
                         uint16_t message_line_count,
                         uint8_t x_offset,
                         int16_t y_offset,
                         int16_t ascent,
                         int16_t descent,
                         int16_t line_height);
static uint8_t Fault_RandomX(void);
static void Fault_ResetIfButtonPressed(void);

void show_fault(const char *text)
{
    u8g2_t *graphics;
    uint16_t message_line_count;
    uint16_t total_line_count;
    uint32_t last_shift_ms;
    int16_t ascent;
    int16_t descent;
    int16_t line_height;
    int16_t text_height;
    int16_t lower_offset;
    int16_t upper_offset;
    int16_t y_offset = 0;
    int8_t direction;
    uint8_t x_offset = 0U;

    btn_init();
    (void)btn_has_short_press(true);

    graphics = OLED_GetGraphics(&oled);
    if (graphics == NULL) {
        for (;;) {
            Fault_ResetIfButtonPressed();
        }
    }

    u8g2_SetFont(graphics, u8g2_font_6x10_tr);
    ascent = (int16_t)u8g2_GetAscent(graphics);
    descent = (int16_t)u8g2_GetDescent(graphics);
    line_height = (int16_t)(ascent - descent);
    if (line_height <= 0) {
        ascent = 8;
        descent = -2;
        line_height = 10;
    }

    message_line_count = Fault_CountMessageLines(text);
    total_line_count = (uint16_t)(message_line_count + 1U);
    text_height = (int16_t)(total_line_count * (uint16_t)line_height);

    if (text_height <= OLED_HEIGHT) {
        lower_offset = 0;
        upper_offset = (int16_t)(OLED_HEIGHT - text_height);
        direction = 1;
    } else {
        lower_offset = (int16_t)(OLED_HEIGHT - text_height);
        upper_offset = 0;
        direction = -1;
    }

    Fault_Render(graphics,
                 text,
                 message_line_count,
                 x_offset,
                 y_offset,
                 ascent,
                 descent,
                 line_height);
    (void)OLED_SendBuffer(&oled);
    last_shift_ms = systick_get_ms();

    for (;;) {
        uint32_t now;

        Fault_ResetIfButtonPressed();
        now = systick_get_ms();

        if ((uint32_t)(now - last_shift_ms) >=
            FAULT_SHIFT_INTERVAL_MS) {
            last_shift_ms = now;

            if (direction > 0) {
                if (y_offset < upper_offset) {
                    ++y_offset;
                }
                if (y_offset >= upper_offset) {
                    y_offset = upper_offset;
                    direction = -1;
                    x_offset = Fault_RandomX();
                }
            } else {
                if (y_offset > lower_offset) {
                    --y_offset;
                }
                if (y_offset <= lower_offset) {
                    y_offset = lower_offset;
                    direction = 1;
                    x_offset = Fault_RandomX();
                }
            }

            Fault_Render(graphics,
                         text,
                         message_line_count,
                         x_offset,
                         y_offset,
                         ascent,
                         descent,
                         line_height);
            (void)OLED_SendBuffer(&oled);
        }

        HAL_Delay(1U);
    }
}

static uint16_t Fault_CountMessageLines(const char *text)
{
    uint16_t line_count;

    if ((text == NULL) || (*text == '\0')) {
        return 0U;
    }

    line_count = 1U;
    while ((*text != '\0') &&
           (line_count < FAULT_MAX_MESSAGE_LINES)) {
        if (*text++ == '\n') {
            ++line_count;
        }
    }

    return line_count;
}

static const char *Fault_CopyLine(const char *text,
                                  char *line,
                                  size_t line_capacity)
{
    size_t copied = 0U;

    if ((line == NULL) || (line_capacity == 0U)) {
        return text;
    }

    if (text == NULL) {
        line[0] = '\0';
        return text;
    }

    while ((*text != '\0') && (*text != '\n')) {
        if (copied < (line_capacity - 1U)) {
            line[copied++] = *text;
        }
        ++text;
    }
    line[copied] = '\0';

    if (*text == '\n') {
        ++text;
    }

    return text;
}

static void Fault_DrawLine(u8g2_t *graphics,
                           const char *line,
                           uint8_t x_offset,
                           int16_t baseline,
                           int16_t ascent,
                           int16_t descent)
{
    const int16_t top = (int16_t)(baseline - ascent);
    const int16_t bottom = (int16_t)(baseline - descent);

    if ((graphics == NULL) || (line == NULL) ||
        (bottom <= 0) || (top >= OLED_HEIGHT) ||
        (baseline < 0) || (baseline > UINT8_MAX)) {
        return;
    }

    u8g2_DrawStr(graphics,
                 (u8g2_uint_t)x_offset,
                 (u8g2_uint_t)baseline,
                 line);
}

static void Fault_Render(u8g2_t *graphics,
                         const char *text,
                         uint16_t message_line_count,
                         uint8_t x_offset,
                         int16_t y_offset,
                         int16_t ascent,
                         int16_t descent,
                         int16_t line_height)
{
    char voltage[FAULT_VOLTAGE_BUFFER_SIZE];
    char line[FAULT_LINE_BUFFER_SIZE];
    char *voltage_end;
    const char *next_line = text;
    uint16_t line_index;
    int16_t baseline;

    millivolts_to_str(adc_to_millivolts(DC_SENS_IDX),
                      voltage,
                      1U);
    voltage_end = voltage;
    while (*voltage_end != '\0') {
        ++voltage_end;
    }
    *voltage_end++ = 'V';
    *voltage_end = '\0';

    u8g2_ClearBuffer(graphics);
    baseline = (int16_t)(y_offset + ascent);
    Fault_DrawLine(graphics,
                   voltage,
                   x_offset,
                   baseline,
                   ascent,
                   descent);

    for (line_index = 0U;
         line_index < message_line_count;
         ++line_index) {
        next_line = Fault_CopyLine(next_line,
                                   line,
                                   sizeof(line));
        baseline = (int16_t)(baseline + line_height);
        Fault_DrawLine(graphics,
                       line,
                       x_offset,
                       baseline,
                       ascent,
                       descent);
    }
}

static uint8_t Fault_RandomX(void)
{
    return (uint8_t)((uint32_t)hotwand_rand() %
                     ((uint32_t)OLED_MAX_PIXEL_SHIFT_X + 1U));
}

static void Fault_ResetIfButtonPressed(void)
{
    if (btn_is_down() || btn_has_short_press(true)) {
        NVIC_SystemReset();
    }
}
