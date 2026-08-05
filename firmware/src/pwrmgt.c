// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

_Static_assert((THERM_2_IDX == (THERM_1_IDX + 1)) && (MCU_TEMP_IDX == (THERM_2_IDX + 1)),
               "temperature ADC indices must remain contiguous, a loop in `pwrmgt_task` depends on this");

#define PWRMGT_GRAPH_WIDTH_PX         32
#define PWRMGT_GRAPH_SCREEN_HEIGHT_PX 128
#define PWRMGT_GRAPH_HEIGHT_PX        (PWRMGT_GRAPH_SCREEN_HEIGHT_PX - PWRMGT_GRAPH_TEXT_HEIGHT_PX)

#define PWRMGT_POWER_50W_MW  50000
#define PWRMGT_POWER_75W_MW  75000
#define PWRMGT_POWER_100W_MW 100000

#define PWRMGT_BLOCKED_CHANGE_MESSAGE_MS 300

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static const char pwrmgt_low_voltage_message[]      = "CAN'T\nLOW\nVOLT";
static const char pwrmgt_too_hot_message[]          = "CAN'T\nTOO\nHOT";
static const char pwrmgt_repeated_attempt_message[] = "NOT\nNOW\nDUMB-\nASS";

static pwrlvl_mode_t pwrmgt_desired_power_level = PWRLVL_MODE_100_PERCENT;
static pwrlvl_mode_t pwrmgt_applied_power_level = PWRLVL_MODE_100_PERCENT;
static uint8_t       pwrmgt_attenuation_reasons;
static bool          pwrmgt_temperature_limited;
static bool          pwrmgt_low_dc_limited;
static bool          pwrmgt_change_direction_up; /* false is the default/downward path */
static uint8_t       pwrmgt_blocked_change_count;
static const char*   pwrmgt_pending_blocked_message;
static uint32_t      pwrmgt_blocked_release_started_ms;
static bool          pwrmgt_blocked_release_pending;
static uint8_t       pwrmgt_power_history[PWRMGT_GRAPH_WIDTH_PX];
static uint8_t       pwrmgt_history_push_idx;
static uint32_t      pwrmgt_history_maximum_mw;
static uint8_t       pwrmgt_graph_dotted_phase;
static uint32_t      pwrmgt_history_last_update_ms;
static uint32_t      pwrmgt_idle_started_ms;
static uint32_t      pwrmgt_last_active_ms;
static uint32_t      pwrmgt_idle_power_threshold_mw = 10000;
static bool          pwrmgt_is_idle;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static uint8_t  pwrmgt_history_peek(uint8_t position);
static void     pwrmgt_history_push(uint32_t power_milliwatts, bool increment_pointer);
static uint8_t  pwrmgt_power_to_pixels(uint32_t power_milliwatts);
static uint32_t pwrmgt_get_applied_limit_mw(void);
static void     pwrmgt_blocked_change_task(void);
static void     pwrmgt_clear_blocked_change(void);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void pwrmgt_task(void)
{
    pwrlvl_mode_t next_power_level;
    uint16_t      dc_input_millivolts;
    uint16_t      highest_temperature;
    uint16_t      temperature;
    uint16_t      temperature_limit;
    uint16_t      dc_limit;
    uint8_t       adc_idx;
    uint8_t       reasons = PWRMGT_ATTENUATION_NONE;
    uint32_t      now;
    uint32_t      power_milliwatts;

    pwrmgt_blocked_change_task();

    /* This task is the sole owner of periodic battery supervision.  Keep the
     * battery_check()/battery_show_fault() pair here rather than adding a
     * second check to main or another task. */
    if (!battery_check())
    {
        battery_show_fault(battery_can_override());
        return;
    }

    highest_temperature = 0;
    for (adc_idx = THERM_1_IDX; adc_idx <= MCU_TEMP_IDX; ++adc_idx)
    {
        temperature = adc_to_celcius(adc_idx);
        if (temperature > highest_temperature)
        {
            highest_temperature = temperature;
        }
    }
    temperature_limit = pwrmgt_temperature_limited ? (TEMPERATURE_HOT_WARNING_THRESH_C - TEMPERATURE_HYSTERYSIS_C)
                                                   : TEMPERATURE_HOT_WARNING_THRESH_C;
    pwrmgt_temperature_limited = highest_temperature > temperature_limit;

    dc_input_millivolts = adc_to_millivolts(DC_SENS_IDX);
    dc_limit =
        pwrmgt_low_dc_limited ? (DC_HIGH_POWER_MINIMUM_MV + DC_HIGH_POWER_HYSTERESIS_MV) : DC_HIGH_POWER_MINIMUM_MV;
    pwrmgt_low_dc_limited = dc_input_millivolts < dc_limit;

    if (!pwrmgt_temperature_limited && !pwrmgt_low_dc_limited)
    {
        pwrmgt_blocked_change_count = 0;
    }

    if (pwrmgt_temperature_limited)
    {
        reasons |= PWRMGT_ATTENUATION_TEMPERATURE;
    }
    if (pwrmgt_low_dc_limited)
    {
        reasons |= PWRMGT_ATTENUATION_LOW_DC_INPUT;
    }

    /* Low input voltage may not recover while the tool remains loaded.  The
     * buck converter's current ceiling also naturally caps the power that a
     * low input voltage can provide; this explicit 50 W cap is intentional. */
    if (pwrmgt_temperature_limited || pwrmgt_low_dc_limited)
    {
        next_power_level = PWRLVL_MODE_50_PERCENT;
    }
    else
    {
        next_power_level = pwrmgt_desired_power_level;
    }

    pwrlvl_set_mode(next_power_level);
    pwrmgt_applied_power_level = next_power_level;

    pwrlvl_task();
    if (pwrlvl_is_current_limiting())
    {
        reasons |= PWRMGT_ATTENUATION_CURRENT_LIMIT;
    }

    pwrmgt_attenuation_reasons = reasons;

    power_milliwatts = adc_get_milliwatts();
    now              = systick_get_ms();
    if (power_milliwatts < pwrmgt_idle_power_threshold_mw)
    {
        if (!pwrmgt_is_idle)
        {
            pwrmgt_idle_started_ms = now;
            pwrmgt_is_idle         = true;
        }
    }
    else
    {
        pwrmgt_is_idle        = false;
        pwrmgt_last_active_ms = now;
    }

    {
        bool increment_pointer = false;

        if ((uint32_t)(now - pwrmgt_history_last_update_ms) >= PWRMGT_GRAPH_UPDATE_INTERVAL_MS)
        {
            pwrmgt_history_last_update_ms = now;
            increment_pointer             = true;
            pwrmgt_graph_dotted_phase ^= 1;
        }

        /* Capture the peak between graph updates, not merely one sample at
         * the graph-update boundary. */
        pwrmgt_history_push(power_milliwatts, increment_pointer);
    }
}

void pwrmgt_render_graph(u8g2_t* graphics)
{
    uint32_t limit_milliwatts;
    uint8_t  limit_pixels;
    uint8_t  pixels;
    uint8_t  x;

    if (graphics == NULL)
    {
        return;
    }

    /* Preserve the text area and replace only the graph below it. */
    u8g2_SetDrawColor(graphics, U8G2_DRAW_CLEAR);
    u8g2_DrawBox(graphics, 0, PWRMGT_GRAPH_TEXT_HEIGHT_PX, PWRMGT_GRAPH_WIDTH_PX, PWRMGT_GRAPH_HEIGHT_PX);
    u8g2_SetDrawColor(graphics, U8G2_DRAW_SET);

    for (x = 0; x < PWRMGT_GRAPH_WIDTH_PX; ++x)
    {
        pixels = pwrmgt_history_peek(x);
        if (pixels != 0)
        {
            u8g2_DrawVLine(graphics, x, PWRMGT_GRAPH_SCREEN_HEIGHT_PX - pixels, pixels);
        }
    }

    /* The dotted XOR line represents the specified ceiling, whether selected
     * by the user or imposed by temperature or input voltage.  The unrestricted
     * 100 W mode has no line; the 75 W and 50 W modes always do. */
    if (pwrmgt_applied_power_level != PWRLVL_MODE_100_PERCENT)
    {
        limit_milliwatts = pwrmgt_get_applied_limit_mw();
        limit_pixels     = pwrmgt_power_to_pixels(limit_milliwatts);
        if (limit_pixels != 0)
        {
            u8g2_SetDrawColor(graphics, U8G2_DRAW_XOR);
            for (x = pwrmgt_graph_dotted_phase; x < PWRMGT_GRAPH_WIDTH_PX; x = (uint8_t)(x + 2))
            {
                u8g2_DrawPixel(graphics, x, PWRMGT_GRAPH_SCREEN_HEIGHT_PX - limit_pixels);
            }
            u8g2_SetDrawColor(graphics, U8G2_DRAW_SET);
        }
    }
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

void pwrmgt_change_pwr_lvl(void)
{
    pwrmgt_last_active_ms = systick_get_ms();

    if (pwrmgt_temperature_limited || pwrmgt_low_dc_limited)
    {
        if (pwrmgt_pending_blocked_message == NULL)
        {
            pwrmgt_pending_blocked_message =
                pwrmgt_temperature_limited ? pwrmgt_too_hot_message : pwrmgt_low_voltage_message;
            pwrmgt_blocked_release_pending = false;
        }
        return;
    }

    switch (pwrmgt_desired_power_level)
    {
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
        pwrmgt_desired_power_level = pwrmgt_change_direction_up ? PWRLVL_MODE_100_PERCENT : PWRLVL_MODE_50_PERCENT;
        break;
    }
}

// -----------------------------------------------------------------------------
// Getters and Setters
// -----------------------------------------------------------------------------

void pwrmgt_set_desired_power_level(pwrlvl_mode_t mode)
{
    if ((uint32_t)mode <= (uint32_t)PWRLVL_MODE_50_PERCENT)
    {
        pwrmgt_desired_power_level = mode;
    }
}

void pwrmgt_set_idle_power_threshold(uint8_t threshold)
{
    static const uint32_t thresholds_mw[] = {1000, 2000, 5000, 10000, 20000, 30000, 40000};

    if (threshold < (sizeof(thresholds_mw) / sizeof(thresholds_mw[0])))
    {
        pwrmgt_idle_power_threshold_mw = thresholds_mw[threshold];
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

uint32_t pwrmgt_get_idle_duration_ms(void)
{
    return pwrmgt_is_idle ? (uint32_t)(systick_get_ms() - pwrmgt_idle_started_ms) : 0;
}

uint32_t pwrmgt_get_time_since_last_activity_ms(void)
{
    return (uint32_t)(systick_get_ms() - pwrmgt_last_active_ms);
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static uint8_t pwrmgt_history_peek(uint8_t position)
{
    /* The slot after the current write slot is the oldest. Position zero is
     * therefore the left edge, while position 31 is the current/right edge. */
    uint8_t index = (uint8_t)(pwrmgt_history_push_idx + position + 1);

    if (index >= PWRMGT_GRAPH_WIDTH_PX)
    {
        index = (uint8_t)(index - PWRMGT_GRAPH_WIDTH_PX);
    }

    return pwrmgt_power_history[index];
}

static void pwrmgt_history_push(uint32_t power_milliwatts, bool increment_pointer)
{
    if (increment_pointer)
    {
        ++pwrmgt_history_push_idx;
        if (pwrmgt_history_push_idx >= PWRMGT_GRAPH_WIDTH_PX)
        {
            pwrmgt_history_push_idx = 0;
        }

        /* The recycled slot must represent zero until this interval observes
         * a nonzero maximum. */
        pwrmgt_power_history[pwrmgt_history_push_idx] = 0;
        pwrmgt_history_maximum_mw                     = 0;
    }

    if (power_milliwatts > pwrmgt_history_maximum_mw)
    {
        pwrmgt_history_maximum_mw                     = power_milliwatts;
        pwrmgt_power_history[pwrmgt_history_push_idx] = pwrmgt_power_to_pixels(power_milliwatts);
    }
}

static uint8_t pwrmgt_power_to_pixels(uint32_t power_milliwatts)
{
    if (power_milliwatts >= PWRMGT_GRAPH_MAX_POWER_MW)
    {
        return PWRMGT_GRAPH_HEIGHT_PX;
    }

    return (uint8_t)((power_milliwatts * PWRMGT_GRAPH_HEIGHT_PX) / PWRMGT_GRAPH_MAX_POWER_MW);
}

static uint32_t pwrmgt_get_applied_limit_mw(void)
{
    switch (pwrmgt_applied_power_level)
    {
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
    const char* message;
    uint32_t    now;

    if (pwrmgt_pending_blocked_message == NULL)
    {
        return;
    }

    now = systick_get_ms();

    /* Peek without clearing: main still owns and consumes the long-press
     * event that enters sleep mode. */
    if (btn_has_long_press(false))
    {
        pwrmgt_clear_blocked_change();
        return;
    }

    if (btn_is_down())
    {
        pwrmgt_blocked_release_pending = false;
        return;
    }

    if (!pwrmgt_blocked_release_pending)
    {
        pwrmgt_blocked_release_started_ms = now;
        pwrmgt_blocked_release_pending    = true;
        return;
    }

    if ((uint32_t)(now - pwrmgt_blocked_release_started_ms) < BTN_DEBOUNCE_MS)
    {
        return;
    }

    if (pwrmgt_blocked_change_count != UINT8_MAX)
    {
        ++pwrmgt_blocked_change_count;
    }

    message = pwrmgt_blocked_change_count >= 5 ? pwrmgt_repeated_attempt_message : pwrmgt_pending_blocked_message;
    show_short_msg(message, PWRMGT_BLOCKED_CHANGE_MESSAGE_MS);
    pwrmgt_clear_blocked_change();
}

static void pwrmgt_clear_blocked_change(void)
{
    pwrmgt_pending_blocked_message = NULL;
    pwrmgt_blocked_release_pending = false;
}
