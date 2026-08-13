/*
Power Management
This code module is responsible for managing the power levels and handling power-related logic.
The user's preferred power level is used, but can be limited by temperature or input voltage.
Abrupt input-power loss and sustained excessive temperature cause terminal shutdowns.
*/

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "pwrmgt.h"

#include "adc.h"
#include "battery.h"
#include "button.h"
#include "conf.h"
#include "fault.h"
#include "miscutils.h"
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
#define PWRMGT_HISTORY_RECORD_COUNT   16
#define PWRMGT_GRAPH_CENTER_LEFT_X    ((PWRMGT_GRAPH_WIDTH_PX / 2) - 1)
#define PWRMGT_GRAPH_CENTER_RIGHT_X   (PWRMGT_GRAPH_WIDTH_PX / 2)

_Static_assert(PWRMGT_GRAPH_WIDTH_PX == (PWRMGT_HISTORY_RECORD_COUNT * 2),
               "each power-history record must map to one mirrored column pair");

#define PWRMGT_POWER_50W_MW  50000
#define PWRMGT_POWER_75W_MW  75000
#define PWRMGT_POWER_100W_MW 100000

#define PWRMGT_BLOCKED_CHANGE_MESSAGE_MS 300

#define PWRMGT_POWER_LOSS_HISTORY_COUNT       (PWRMGT_POWER_LOSS_SUSTAINED_INTERVAL_COUNT + 1)
#define PWRMGT_POWER_LOSS_EXPECTED_WINDOW_MS                                                                    \
    (PWRMGT_POWER_LOSS_SAMPLE_INTERVAL_MS * PWRMGT_POWER_LOSS_SUSTAINED_INTERVAL_COUNT)
#define PWRMGT_POWER_LOSS_STALE_GAP_MS (PWRMGT_POWER_LOSS_SAMPLE_INTERVAL_MS * 2)

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

typedef struct
{
    uint16_t millivolts;
    uint32_t timestamp_ms;
} pwrmgt_voltage_record_t;

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
static bool          pwrmgt_temperature_shutdown_pending;
static uint32_t      pwrmgt_temperature_shutdown_started_ms;
static bool          pwrmgt_low_dc_limited;
static bool          pwrmgt_change_direction_up; /* false is the default/downward path */
static uint8_t       pwrmgt_blocked_change_count;
static const char*   pwrmgt_pending_blocked_message;
static uint32_t      pwrmgt_blocked_release_started_ms;
static bool          pwrmgt_blocked_release_pending;
static uint32_t      pwrmgt_power_history_mw[PWRMGT_HISTORY_RECORD_COUNT];
static uint8_t       pwrmgt_history_push_idx;
static uint8_t       pwrmgt_graph_dotted_phase;
static uint32_t      pwrmgt_history_last_update_ms;
static uint32_t      pwrmgt_idle_started_ms;
static uint32_t      pwrmgt_last_active_ms;
static uint32_t      pwrmgt_idle_power_threshold_mw = 10000;
static bool          pwrmgt_is_idle;
static pwrmgt_voltage_record_t pwrmgt_voltage_history[PWRMGT_POWER_LOSS_HISTORY_COUNT];
static uint8_t                  pwrmgt_voltage_history_count;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static uint32_t pwrmgt_history_peek(uint8_t age);
static void     pwrmgt_history_push(uint32_t power_milliwatts, bool start_new_segment);
static void     pwrmgt_draw_power_pair(u8g2_t* graphics, uint8_t distance_from_center, uint32_t power_milliwatts);
static uint8_t  pwrmgt_power_to_pixels(uint32_t power_milliwatts);
static uint32_t pwrmgt_get_applied_limit_mw(void);
static void     pwrmgt_temperature_shutdown_task(uint16_t highest_temperature, uint32_t now);
static void     pwrmgt_blocked_change_task(void);
static void     pwrmgt_clear_blocked_change(void);
static bool     pwrmgt_power_loss_detected(uint16_t input_millivolts, uint32_t now);
static bool     pwrmgt_drop_meets_threshold(const pwrmgt_voltage_record_t* older,
                                            const pwrmgt_voltage_record_t* newer,
                                            uint16_t                        minimum_drop_mv,
                                            uint32_t                        reference_interval_ms);
static void     pwrmgt_voltage_history_reset(uint16_t input_millivolts, uint32_t now);
static void     pwrmgt_voltage_history_push(uint16_t input_millivolts, uint32_t now);

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

    now                  = systick_get_ms();
    dc_input_millivolts = adc_to_millivolts(DC_SENS_IDX);

    /* Detect the discharge signature before the absolute voltage checks. If
     * an existing battery or undervoltage threshold becomes valid first, its
     * established safety fault still takes precedence. */
    if (pwrmgt_power_loss_detected(dc_input_millivolts, now))
    {
        show_fault("POWER\nLOSS\nFAULT", true);
        return;
    }

    /* This task is the sole owner of periodic battery supervision.  Keep the
     * battery_check()/battery_show_fault() pair here rather than adding a
     * second check to main or another task. */
    if (!battery_check())
    {
        battery_show_fault(battery_can_override());
        return;
    }

    /* This terminal hardware lockout is distinct from the recoverable
     * high-power derating below DC_HIGH_POWER_MINIMUM_MV. */
    if (dc_input_millivolts < DC_UNDERVOLTAGE_FAULT_MV)
    {
        show_fault("LOW\nVOLT\nFAULT", true);
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

    /* Qualify the terminal threshold separately from the recoverable Eco-mode
     * derating. A terminal fault blocks in show_fault() until reset. */
    pwrmgt_temperature_shutdown_task(highest_temperature, now);

    temperature_limit = pwrmgt_temperature_limited ? (TEMPERATURE_HOT_WARNING_THRESH_C - TEMPERATURE_HYSTERYSIS_C)
                                                   : TEMPERATURE_HOT_WARNING_THRESH_C;
    pwrmgt_temperature_limited = highest_temperature > temperature_limit;

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
        bool start_new_segment = false;

        if ((uint32_t)(now - pwrmgt_history_last_update_ms) >= PWRMGT_GRAPH_UPDATE_INTERVAL_MS)
        {
            pwrmgt_history_last_update_ms = now;
            start_new_segment             = true;
            pwrmgt_graph_dotted_phase ^= 1;
        }

        /* Capture the peak between graph updates, not merely one sample at
         * the graph-update boundary. */
        pwrmgt_history_push(power_milliwatts, start_new_segment);
    }
}

void pwrmgt_render_graph(u8g2_t* graphics)
{
    uint32_t limit_milliwatts;
    uint8_t  limit_pixels;
    uint8_t  age;
    uint8_t  x;

    if (graphics == NULL)
    {
        return;
    }

    /* Preserve the text area and replace only the graph below it. */
    u8g2_SetDrawColor(graphics, U8G2_DRAW_CLEAR);
    u8g2_DrawBox(graphics, 0, PWRMGT_GRAPH_TEXT_HEIGHT_PX, PWRMGT_GRAPH_WIDTH_PX, PWRMGT_GRAPH_HEIGHT_PX);
    u8g2_SetDrawColor(graphics, U8G2_DRAW_SET);

    /* The center pair is sampled at display-render time, so it follows the
     * fixed fast frame rate instead of
     * showing the current segment's peak. */
    pwrmgt_draw_power_pair(graphics, 0, adc_get_milliwatts());

    /* Completed peak segments run backward in time from the center.  Each
     * value is mirrored so the oldest
     * retained segment reaches both edges. */
    for (age = 1; age < PWRMGT_HISTORY_RECORD_COUNT; ++age)
    {
        pwrmgt_draw_power_pair(graphics, age, pwrmgt_history_peek(age));
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
        btn_reset_consecutive_presses();
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

    // clang-format off
    if (btn_get_consecutive_presses() >= 6)
    {
        btn_reset_consecutive_presses();
        uint8_t r = hotwand_rand() % 5;
        switch (r)
        {
        case 0: show_short_msg("SKILL\nISSUE\n  ?  "    , 1000); break;
        case 1: show_short_msg("BRUH.\nSTOP."           , 1000); break;
        case 2: show_short_msg("GIT\nGOOD"              , 1000); break;
        case 3: show_short_msg("IT'S\nOVER\n9000\n!!!!!", 1000); break;
        case 4: show_short_msg("THE\nCAKE\nIS A\nLIE !!", 1000); break;
        }
    }
    // clang-format on
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

static uint32_t pwrmgt_history_peek(uint8_t age)
{
    uint8_t index;

    if (age >= PWRMGT_HISTORY_RECORD_COUNT)
    {
        return 0;
    }

    if (pwrmgt_history_push_idx >= age)
    {
        index = (uint8_t)(pwrmgt_history_push_idx - age);
    }
    else
    {
        index = (uint8_t)(PWRMGT_HISTORY_RECORD_COUNT + pwrmgt_history_push_idx - age);
    }

    return pwrmgt_power_history_mw[index];
}

static void pwrmgt_history_push(uint32_t power_milliwatts, bool start_new_segment)
{
    if (start_new_segment)
    {
        ++pwrmgt_history_push_idx;
        if (pwrmgt_history_push_idx >= PWRMGT_HISTORY_RECORD_COUNT)
        {
            pwrmgt_history_push_idx = 0;
        }

        /* The recycled slot is the new in-progress segment. */
        pwrmgt_power_history_mw[pwrmgt_history_push_idx] = 0;
    }

    if (power_milliwatts > pwrmgt_power_history_mw[pwrmgt_history_push_idx])
    {
        pwrmgt_power_history_mw[pwrmgt_history_push_idx] = power_milliwatts;
    }
}

static void pwrmgt_draw_power_pair(u8g2_t* graphics, uint8_t distance_from_center, uint32_t power_milliwatts)
{
    uint8_t left_x;
    uint8_t pixels;
    uint8_t right_x;

    pixels = pwrmgt_power_to_pixels(power_milliwatts);
    if (pixels == 0)
    {
        return;
    }

    left_x  = (uint8_t)(PWRMGT_GRAPH_CENTER_LEFT_X - distance_from_center);
    right_x = (uint8_t)(PWRMGT_GRAPH_CENTER_RIGHT_X + distance_from_center);

    u8g2_DrawVLine(graphics, left_x, PWRMGT_GRAPH_SCREEN_HEIGHT_PX - pixels, pixels);
    u8g2_DrawVLine(graphics, right_x, PWRMGT_GRAPH_SCREEN_HEIGHT_PX - pixels, pixels);
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

static void pwrmgt_temperature_shutdown_task(uint16_t highest_temperature, uint32_t now)
{
    /* The full qualification interval must be continuously above the
     * threshold. A safe reading immediately cancels the pending shutdown. */
    if (highest_temperature <= TEMPERATURE_SHUTDOWN_THRESH_C)
    {
        pwrmgt_temperature_shutdown_pending = false;
        return;
    }

    if (!pwrmgt_temperature_shutdown_pending)
    {
        pwrmgt_temperature_shutdown_started_ms = now;
        pwrmgt_temperature_shutdown_pending    = true;
        return;
    }

    if ((uint32_t)(now - pwrmgt_temperature_shutdown_started_ms) >= TEMPERATURE_SHUTDOWN_TIME_MS)
    {
        /* show_fault() immediately puts all controlled outputs into their
         * existing safe fault state and permits recovery only through reset. */
        show_fault("CIRKT\nTOO\nHOT\nFAULT", true);
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

static bool pwrmgt_power_loss_detected(uint16_t input_millivolts, uint32_t now)
{
    pwrmgt_voltage_record_t        current;
    const pwrmgt_voltage_record_t* newest;
    uint32_t                       elapsed_ms;
    uint8_t                        idx;

    current.millivolts   = input_millivolts;
    current.timestamp_ms = now;

    if (pwrmgt_voltage_history_count == 0)
    {
        pwrmgt_voltage_history_reset(input_millivolts, now);
        return false;
    }

    newest     = &pwrmgt_voltage_history[pwrmgt_voltage_history_count - 1];
    elapsed_ms = (uint32_t)(now - newest->timestamp_ms);

    /* A stale reference cannot establish when the voltage changed. Reset it
     * before evaluating either rate-based path. */
    if (elapsed_ms > PWRMGT_POWER_LOSS_STALE_GAP_MS)
    {
        pwrmgt_voltage_history_reset(input_millivolts, now);
        return false;
    }

    /* Check this on every supervisor pass, not only at the history cadence,
     * so a large collapse can win before the rail reaches a static cutoff. */
    if (pwrmgt_drop_meets_threshold(newest,
                                    &current,
                                    PWRMGT_POWER_LOSS_FAST_DROP_MV,
                                    PWRMGT_POWER_LOSS_SAMPLE_INTERVAL_MS))
    {
        return true;
    }

    if (elapsed_ms < PWRMGT_POWER_LOSS_SAMPLE_INTERVAL_MS)
    {
        return false;
    }

    pwrmgt_voltage_history_push(input_millivolts, now);
    if (pwrmgt_voltage_history_count < PWRMGT_POWER_LOSS_HISTORY_COUNT)
    {
        return false;
    }

    if (!pwrmgt_drop_meets_threshold(&pwrmgt_voltage_history[0],
                                     &pwrmgt_voltage_history[PWRMGT_POWER_LOSS_HISTORY_COUNT - 1],
                                     PWRMGT_POWER_LOSS_SUSTAINED_DROP_MV,
                                     PWRMGT_POWER_LOSS_EXPECTED_WINDOW_MS))
    {
        return false;
    }

    /* A genuine reservoir-capacitor discharge keeps falling. Requiring every
     * segment to decline rejects a one-time load step that settles at a new
     * voltage before the complete observation window elapses. */
    for (idx = 1; idx < PWRMGT_POWER_LOSS_HISTORY_COUNT; ++idx)
    {
        if (!pwrmgt_drop_meets_threshold(&pwrmgt_voltage_history[idx - 1],
                                         &pwrmgt_voltage_history[idx],
                                         PWRMGT_POWER_LOSS_MIN_SEGMENT_DROP_MV,
                                         PWRMGT_POWER_LOSS_SAMPLE_INTERVAL_MS))
        {
            return false;
        }
    }

    return true;
}

static bool pwrmgt_drop_meets_threshold(const pwrmgt_voltage_record_t* older,
                                        const pwrmgt_voltage_record_t* newer,
                                        uint16_t                        minimum_drop_mv,
                                        uint32_t                        reference_interval_ms)
{
    uint32_t actual_interval_ms;
    uint32_t drop_mv;

    actual_interval_ms = (uint32_t)(newer->timestamp_ms - older->timestamp_ms);
    if ((actual_interval_ms == 0) || (older->millivolts <= newer->millivolts))
    {
        return false;
    }

    drop_mv = (uint32_t)older->millivolts - newer->millivolts;
    if (drop_mv < minimum_drop_mv)
    {
        return false;
    }

    /* Compare rates without division so rounding cannot move the threshold.
     * Configuration limits and stale-gap handling keep both products within
     * uint32_t. */
    return (drop_mv * reference_interval_ms) >= ((uint32_t)minimum_drop_mv * actual_interval_ms);
}

static void pwrmgt_voltage_history_reset(uint16_t input_millivolts, uint32_t now)
{
    pwrmgt_voltage_history[0].millivolts   = input_millivolts;
    pwrmgt_voltage_history[0].timestamp_ms = now;
    pwrmgt_voltage_history_count           = 1;
}

static void pwrmgt_voltage_history_push(uint16_t input_millivolts, uint32_t now)
{
    uint8_t idx;

    if (pwrmgt_voltage_history_count < PWRMGT_POWER_LOSS_HISTORY_COUNT)
    {
        idx = pwrmgt_voltage_history_count++;
    }
    else
    {
        for (idx = 1; idx < PWRMGT_POWER_LOSS_HISTORY_COUNT; ++idx)
        {
            pwrmgt_voltage_history[idx - 1] = pwrmgt_voltage_history[idx];
        }
        idx = PWRMGT_POWER_LOSS_HISTORY_COUNT - 1;
    }

    pwrmgt_voltage_history[idx].millivolts   = input_millivolts;
    pwrmgt_voltage_history[idx].timestamp_ms = now;
}
