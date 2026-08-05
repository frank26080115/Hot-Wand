#include "pwrmgt.h"

#include "adc.h"
#include "battery.h"
#include "button.h"
#include "conf.h"
#include "fault.h"
#include "pins.h"
#include "pwrlvl.h"
#include "systick.h"

#include <stdbool.h>
#include <stdint.h>

_Static_assert((THERM_2_IDX == (THERM_1_IDX + 1U)) &&
                   (MCU_TEMP_IDX == (THERM_2_IDX + 1U)),
               "temperature ADC indices must remain contiguous");

#define PWRMGT_GRAPH_WIDTH_PX          32U
#define PWRMGT_GRAPH_SCREEN_HEIGHT_PX 128U
#define PWRMGT_GRAPH_HEIGHT_PX \
    (PWRMGT_GRAPH_SCREEN_HEIGHT_PX - PWRMGT_GRAPH_TEXT_HEIGHT_PX)

#define PWRMGT_POWER_50W_MW  50000UL
#define PWRMGT_POWER_75W_MW  75000UL
#define PWRMGT_POWER_100W_MW 100000UL

#define PWRMGT_BLOCKED_CHANGE_MESSAGE_MS 300UL

static const char pwrmgt_low_voltage_message[] = "CAN'T\nLOW\nVOLT";
static const char pwrmgt_too_hot_message[] = "CAN'T\nTOO\nHOT";
static const char pwrmgt_repeated_attempt_message[] =
    "NOT\nNOW\nDUMB-\nASS";

static pwrlvl_mode_t pwrmgt_desired_power_level =
    PWRLVL_MODE_100_PERCENT;
static pwrlvl_mode_t pwrmgt_applied_power_level =
    PWRLVL_MODE_100_PERCENT;
static uint8_t pwrmgt_attenuation_reasons;
static bool pwrmgt_temperature_limited;
static bool pwrmgt_low_dc_limited;
static bool pwrmgt_change_direction_up; /* false is the default/downward path */
static uint8_t pwrmgt_blocked_change_count;
static const char *pwrmgt_pending_blocked_message;
static uint32_t pwrmgt_blocked_release_started_ms;
static bool pwrmgt_blocked_release_pending;
static uint8_t pwrmgt_power_history[PWRMGT_GRAPH_WIDTH_PX];
static uint8_t pwrmgt_history_push_idx;
static uint8_t pwrmgt_graph_dotted_phase;
static uint32_t pwrmgt_history_last_update_ms;

static uint8_t pwrmgt_history_peek(uint8_t position);
static void pwrmgt_history_push(uint32_t power_milliwatts);
static uint8_t pwrmgt_power_to_pixels(uint32_t power_milliwatts);
static uint32_t pwrmgt_get_applied_limit_mw(void);
static void pwrmgt_blocked_change_task(void);
static void pwrmgt_clear_blocked_change(void);

void pwrmgt_set_desired_power_level(pwrlvl_mode_t mode)
{
    if ((uint32_t)mode <= (uint32_t)PWRLVL_MODE_50_PERCENT) {
        pwrmgt_desired_power_level = mode;
    }
}

void pwrmgt_change_pwr_lvl(void)
{
    if (pwrmgt_temperature_limited || pwrmgt_low_dc_limited) {
        if (pwrmgt_pending_blocked_message == NULL) {
            pwrmgt_pending_blocked_message =
                pwrmgt_temperature_limited
                ? pwrmgt_too_hot_message
                : pwrmgt_low_voltage_message;
            pwrmgt_blocked_release_pending = false;
        }
        return;
    }

    switch (pwrmgt_desired_power_level) {
    case PWRLVL_MODE_100_PERCENT:
        pwrmgt_change_direction_up = false;
        pwrmgt_desired_power_level = PWRLVL_MODE_75_PERCENT;
        break;

    case PWRLVL_MODE_50_PERCENT:
        pwrmgt_change_direction_up = true;
        pwrmgt_desired_power_level = PWRLVL_MODE_75_PERCENT;
        break;

    case PWRLVL_MODE_75_PERCENT:
    default:
        pwrmgt_desired_power_level = pwrmgt_change_direction_up
                                         ? PWRLVL_MODE_100_PERCENT
                                         : PWRLVL_MODE_50_PERCENT;
        break;
    }
}

void pwrmgt_task(void)
{
    pwrlvl_mode_t next_power_level;
    uint16_t dc_input_millivolts;
    uint16_t highest_temperature;
    uint16_t temperature;
    uint16_t temperature_limit;
    uint16_t dc_limit;
    uint8_t adc_idx;
    uint8_t reasons = PWRMGT_ATTENUATION_NONE;

    pwrmgt_blocked_change_task();

    /* This task is the sole owner of periodic battery supervision.  Keep the
     * battery_check()/battery_show_fault() pair here rather than adding a
     * second check to main or another task. */
    if (!battery_check()) {
        battery_show_fault(battery_can_override());
        return;
    }

    highest_temperature = 0U;
    for (adc_idx = THERM_1_IDX; adc_idx <= MCU_TEMP_IDX; ++adc_idx) {
        temperature = adc_to_celcius(adc_idx);
        if (temperature > highest_temperature) {
            highest_temperature = temperature;
        }
    }
    temperature_limit = pwrmgt_temperature_limited
                            ? (TEMPERATURE_HOT_WARNING_THRESH_C -
                               TEMPERATURE_HYSTERYSIS_C)
                            : TEMPERATURE_HOT_WARNING_THRESH_C;
    pwrmgt_temperature_limited =
        highest_temperature > temperature_limit;

    dc_input_millivolts = adc_to_millivolts(DC_SENS_IDX);
    dc_limit = pwrmgt_low_dc_limited
                   ? (DC_HIGH_POWER_MINIMUM_MV +
                      DC_HIGH_POWER_HYSTERESIS_MV)
                   : DC_HIGH_POWER_MINIMUM_MV;
    pwrmgt_low_dc_limited = dc_input_millivolts < dc_limit;

    if (!pwrmgt_temperature_limited && !pwrmgt_low_dc_limited) {
        pwrmgt_blocked_change_count = 0U;
    }

    if (pwrmgt_temperature_limited) {
        reasons |= PWRMGT_ATTENUATION_TEMPERATURE;
    }
    if (pwrmgt_low_dc_limited) {
        reasons |= PWRMGT_ATTENUATION_LOW_DC_INPUT;
    }

    /* Low input voltage may not recover while the tool remains loaded.  The
     * buck converter's current ceiling also naturally caps the power that a
     * low input voltage can provide; this explicit 50 W cap is intentional. */
    if (pwrmgt_temperature_limited || pwrmgt_low_dc_limited) {
        next_power_level = PWRLVL_MODE_50_PERCENT;
    } else {
        next_power_level = pwrmgt_desired_power_level;
    }

    pwrlvl_set_mode(next_power_level);
    pwrmgt_applied_power_level = next_power_level;

    pwrlvl_task();
    if (pwrlvl_is_current_limiting()) {
        reasons |= PWRMGT_ATTENUATION_CURRENT_LIMIT;
    }

    pwrmgt_attenuation_reasons = reasons;

    {
        uint32_t now = systick_get_ms();

        if ((uint32_t)(now - pwrmgt_history_last_update_ms) >=
            PWRMGT_GRAPH_UPDATE_INTERVAL_MS) {
            pwrmgt_history_last_update_ms = now;
            pwrmgt_history_push(adc_get_milliwatts());
            pwrmgt_graph_dotted_phase ^= 1U;
        }
    }
}

void pwrmgt_render_graph(u8g2_t *graphics)
{
    uint32_t limit_milliwatts;
    uint8_t limit_pixels;
    uint8_t pixels;
    uint8_t x;

    if (graphics == NULL) {
        return;
    }

    /* Preserve the text area and replace only the graph below it. */
    u8g2_SetDrawColor(graphics, 0U);
    u8g2_DrawBox(graphics,
                 0U,
                 PWRMGT_GRAPH_TEXT_HEIGHT_PX,
                 PWRMGT_GRAPH_WIDTH_PX,
                 PWRMGT_GRAPH_HEIGHT_PX);
    u8g2_SetDrawColor(graphics, 1U);

    for (x = 0U; x < PWRMGT_GRAPH_WIDTH_PX; ++x) {
        pixels = pwrmgt_history_peek(x);
        if (pixels != 0U) {
            u8g2_DrawVLine(graphics,
                           x,
                           PWRMGT_GRAPH_SCREEN_HEIGHT_PX - pixels,
                           pixels);
        }
    }

    /* The dotted XOR line represents the specified ceiling, whether selected
     * by the user or imposed by temperature or input voltage.  The unrestricted
     * 100 W mode has no line; the 75 W and 50 W modes always do. */
    if (pwrmgt_applied_power_level != PWRLVL_MODE_100_PERCENT) {
        limit_milliwatts = pwrmgt_get_applied_limit_mw();
        limit_pixels = pwrmgt_power_to_pixels(limit_milliwatts);
        if (limit_pixels != 0U) {
            u8g2_SetDrawColor(graphics, 2U);
            for (x = pwrmgt_graph_dotted_phase;
                 x < PWRMGT_GRAPH_WIDTH_PX;
                 x = (uint8_t)(x + 2U)) {
                u8g2_DrawPixel(
                    graphics,
                    x,
                    PWRMGT_GRAPH_SCREEN_HEIGHT_PX - limit_pixels);
            }
            u8g2_SetDrawColor(graphics, 1U);
        }
    }
}

pwrlvl_mode_t pwrmgt_get_applied_power_level(void)
{
    return pwrmgt_applied_power_level;
}

uint8_t pwrmgt_get_attenuation_reasons(void)
{
    return pwrmgt_attenuation_reasons;
}

static uint8_t pwrmgt_history_peek(uint8_t position)
{
    /* The next slot to overwrite is also the oldest slot.  Position zero is
     * therefore the left edge, while position 31 is the newest/right edge. */
    uint8_t index = (uint8_t)(pwrmgt_history_push_idx + position);

    if (index >= PWRMGT_GRAPH_WIDTH_PX) {
        index = (uint8_t)(index - PWRMGT_GRAPH_WIDTH_PX);
    }

    return pwrmgt_power_history[index];
}

static void pwrmgt_history_push(uint32_t power_milliwatts)
{
    pwrmgt_power_history[pwrmgt_history_push_idx] =
        pwrmgt_power_to_pixels(power_milliwatts);

    ++pwrmgt_history_push_idx;
    if (pwrmgt_history_push_idx >= PWRMGT_GRAPH_WIDTH_PX) {
        pwrmgt_history_push_idx = 0U;
    }
}

static uint8_t pwrmgt_power_to_pixels(uint32_t power_milliwatts)
{
    if (power_milliwatts >= PWRMGT_GRAPH_MAX_POWER_MW) {
        return PWRMGT_GRAPH_HEIGHT_PX;
    }

    return (uint8_t)((power_milliwatts * PWRMGT_GRAPH_HEIGHT_PX) /
                     PWRMGT_GRAPH_MAX_POWER_MW);
}

static uint32_t pwrmgt_get_applied_limit_mw(void)
{
    switch (pwrmgt_applied_power_level) {
    case PWRLVL_MODE_50_PERCENT:
        return PWRMGT_POWER_50W_MW;

    case PWRLVL_MODE_75_PERCENT:
        return PWRMGT_POWER_75W_MW;

    case PWRLVL_MODE_100_PERCENT:
    default:
        return PWRMGT_POWER_100W_MW;
    }
}

static void pwrmgt_blocked_change_task(void)
{
    const char *message;
    uint32_t now;

    if (pwrmgt_pending_blocked_message == NULL) {
        return;
    }

    now = systick_get_ms();

    /* Peek without clearing: main still owns and consumes the long-press
     * event that enters sleep mode. */
    if (btn_has_long_press(false)) {
        pwrmgt_clear_blocked_change();
        return;
    }

    if (btn_is_down()) {
        pwrmgt_blocked_release_pending = false;
        return;
    }

    if (!pwrmgt_blocked_release_pending) {
        pwrmgt_blocked_release_started_ms = now;
        pwrmgt_blocked_release_pending = true;
        return;
    }

    if ((uint32_t)(now - pwrmgt_blocked_release_started_ms) <
        BTN_DEBOUNCE_MS) {
        return;
    }

    if (pwrmgt_blocked_change_count != UINT8_MAX) {
        ++pwrmgt_blocked_change_count;
    }

    message = pwrmgt_blocked_change_count >= 5U
                  ? pwrmgt_repeated_attempt_message
                  : pwrmgt_pending_blocked_message;
    show_short_msg(message, PWRMGT_BLOCKED_CHANGE_MESSAGE_MS);
    pwrmgt_clear_blocked_change();
}

static void pwrmgt_clear_blocked_change(void)
{
    pwrmgt_pending_blocked_message = NULL;
    pwrmgt_blocked_release_pending = false;
}
