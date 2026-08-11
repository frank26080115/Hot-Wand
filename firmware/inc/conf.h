#pragma once

/* Project-wide settings are ordered from frequently tuned behavior to implementation details. */
/* Each validation block stays immediately after the settings it protects. */

// -----------------------------------------------------------------------------
// User Interaction and Setup
// -----------------------------------------------------------------------------

#define SHOW_SPLASH    // the 5 splash screens use about 3 kb of memory
#define SHOW_SPLASH_MS 2000

#if defined(SHOW_SPLASH) && (SHOW_SPLASH_MS == 0)
#error "SHOW_SPLASH_MS must be nonzero when splash screens are enabled"
#endif

#ifndef BTN_LONG_PRESS_MS
#define BTN_LONG_PRESS_MS 1000
#endif

#ifndef BTN_CONSECUTIVE_PRESS_TIMEOUT_MS
#define BTN_CONSECUTIVE_PRESS_TIMEOUT_MS 5000
#endif

#if BTN_CONSECUTIVE_PRESS_TIMEOUT_MS == 0
#error "BTN_CONSECUTIVE_PRESS_TIMEOUT_MS must be nonzero"
#endif

#ifndef SETUP_MENU_TIMEOUT_MS
#define SETUP_MENU_TIMEOUT_MS (5 * 60 * 1000)
#endif

#define SETUP_HOLD_DURATION_MS 5000

#if SETUP_HOLD_DURATION_MS == 0
#error "SETUP_HOLD_DURATION_MS must be nonzero"
#endif

// -----------------------------------------------------------------------------
// Power and Thermal Limits
// -----------------------------------------------------------------------------

#define TEMPERATURE_FAN_THRESHOLD_LOW_C  50
#define TEMPERATURE_FAN_THRESHOLD_HIGH_C 80
#define TEMPERATURE_HOT_WARNING_THRESH_C 80
#define TEMPERATURE_SHUTDOWN_THRESH_C    110
#define TEMPERATURE_SHUTDOWN_TIME_MS     1000
#define TEMPERATURE_HYSTERYSIS_C         5
#define FAN_MINIMUM_ON_TIME_MS           (10 * 1000)
#define FAN_MINIMUM_OFF_TIME_MS          (5 * 1000)

#if (FAN_MINIMUM_ON_TIME_MS == 0) || (FAN_MINIMUM_OFF_TIME_MS == 0)
#error "Fan minimum on and off times must be nonzero"
#endif

/* Set to 0 if production units intentionally omit both external NTC sensors. */
#ifndef NTC_FAULT_WARNING_ENABLED
#define NTC_FAULT_WARNING_ENABLED 1
#endif

#if (NTC_FAULT_WARNING_ENABLED != 0) && (NTC_FAULT_WARNING_ENABLED != 1)
#error "NTC_FAULT_WARNING_ENABLED must be 0 or 1"
#endif

#if (TEMPERATURE_FAN_THRESHOLD_LOW_C > TEMPERATURE_FAN_THRESHOLD_HIGH_C) ||                                            \
    (TEMPERATURE_HYSTERYSIS_C > TEMPERATURE_FAN_THRESHOLD_LOW_C) ||                                                    \
    (TEMPERATURE_HYSTERYSIS_C > TEMPERATURE_HOT_WARNING_THRESH_C) ||                                                   \
    (TEMPERATURE_HOT_WARNING_THRESH_C >= TEMPERATURE_SHUTDOWN_THRESH_C) || (TEMPERATURE_SHUTDOWN_TIME_MS == 0)
#error "Invalid thermal thresholds, hysteresis, or shutdown timing"
#endif

#define DC_UNDERVOLTAGE_FAULT_MV 14000

#if (DC_UNDERVOLTAGE_FAULT_MV == 0) || (DC_UNDERVOLTAGE_FAULT_MV > 65535)
#error "DC_UNDERVOLTAGE_FAULT_MV must fit in millivolts"
#endif

#define DC_HIGH_POWER_MINIMUM_MV    19000
#define DC_HIGH_POWER_HYSTERESIS_MV 1000

#if (DC_HIGH_POWER_MINIMUM_MV <= 0) || (DC_HIGH_POWER_MINIMUM_MV > 65535) || (DC_HIGH_POWER_HYSTERESIS_MV < 0) ||      \
    ((DC_HIGH_POWER_MINIMUM_MV + DC_HIGH_POWER_HYSTERESIS_MV) > 65535)
#error "Invalid high-power DC threshold or hysteresis"
#endif

#ifndef PWRLVL_CURRENT_LIMIT_ENABLED
#define PWRLVL_CURRENT_LIMIT_ENABLED 1
#endif

#if PWRLVL_CURRENT_LIMIT_ENABLED
#define PWRLVL_CURRENT_LIMIT_MA 5200
#endif

#define PWRLVL_SHORT_CIRCUIT_CURRENT_MA 5800
#define PWRLVL_SHORT_CIRCUIT_TIME_MS    5000

#if (PWRLVL_SHORT_CIRCUIT_CURRENT_MA == 0) || (PWRLVL_SHORT_CIRCUIT_CURRENT_MA > 65535)
#error "PWRLVL_SHORT_CIRCUIT_CURRENT_MA must fit in milliamps"
#endif

#if PWRLVL_SHORT_CIRCUIT_TIME_MS == 0
#error "PWRLVL_SHORT_CIRCUIT_TIME_MS must be nonzero"
#endif

// -----------------------------------------------------------------------------
// Battery Configuration
// -----------------------------------------------------------------------------

#define BATTERY_MINIMUM_CELL_CNT_LIPO 4
#define BATTERY_MAXIMUM_CELL_CNT_LIPO 8

#define BATTERY_MINIMUM_CELL_CNT_LIHV 4
#define BATTERY_MAXIMUM_CELL_CNT_LIHV 8

#define BATTERY_MINIMUM_CELL_CNT_LIFE 6
#define BATTERY_MAXIMUM_CELL_CNT_LIFE 9

#if (BATTERY_MINIMUM_CELL_CNT_LIPO == 0) || (BATTERY_MINIMUM_CELL_CNT_LIPO > BATTERY_MAXIMUM_CELL_CNT_LIPO) ||         \
    (BATTERY_MINIMUM_CELL_CNT_LIHV == 0) || (BATTERY_MINIMUM_CELL_CNT_LIHV > BATTERY_MAXIMUM_CELL_CNT_LIHV) ||         \
    (BATTERY_MINIMUM_CELL_CNT_LIFE == 0) || (BATTERY_MINIMUM_CELL_CNT_LIFE > BATTERY_MAXIMUM_CELL_CNT_LIFE)
#error "Invalid battery chemistry cell-count limits"
#endif

#if (BATTERY_MAXIMUM_CELL_CNT_LIPO > 255) || (BATTERY_MAXIMUM_CELL_CNT_LIHV > 255) ||                                  \
    (BATTERY_MAXIMUM_CELL_CNT_LIFE > 255)
#error "Battery cell-count limits must fit in uint8_t"
#endif

#define BATTERY_OVERRIDE_MINIMUM_MV 14000

#if (BATTERY_OVERRIDE_MINIMUM_MV == 0) || (BATTERY_OVERRIDE_MINIMUM_MV > 65535)
#error "BATTERY_OVERRIDE_MINIMUM_MV must fit in millivolts"
#endif

// -----------------------------------------------------------------------------
// Display
// -----------------------------------------------------------------------------

#define OLED_DIM_CONTRAST 32

/* Horizontal burn-in shifts for the two fixed-width OLED fonts. */
#define OLED_MAX_PIXEL_SHIFT_X_SMALLFONT 7
#define OLED_MAX_PIXEL_SHIFT_X_BIGFONT   3
#define OLED_MAX_PIXEL_SHIFT_Y           3

/* Portrait power graph: 28 text pixels plus a 100-pixel, 0-100 W plot. */
/* The 16-entry ring retains each x ms interval peak, including the current interval. */
/* Rendering shows live power in the center pair and mirrors the 15 completed peaks outward. */
#define PWRMGT_GRAPH_TEXT_HEIGHT_PX     28
#define PWRMGT_GRAPH_UPDATE_INTERVAL_MS 250
#define PWRMGT_GRAPH_MAX_POWER_MW       100000

#if PWRMGT_GRAPH_TEXT_HEIGHT_PX >= 128
#error "PWRMGT_GRAPH_TEXT_HEIGHT_PX must leave room for the graph"
#endif

#if PWRMGT_GRAPH_UPDATE_INTERVAL_MS == 0
#error "PWRMGT_GRAPH_UPDATE_INTERVAL_MS must be nonzero"
#endif

#if PWRMGT_GRAPH_MAX_POWER_MW == 0
#error "PWRMGT_GRAPH_MAX_POWER_MW must be nonzero"
#endif

// -----------------------------------------------------------------------------
// Boot Power Qualification
// -----------------------------------------------------------------------------

#define BOOT_POWER_WAIT_MS    300
#define BOOT_POWER_STABLE_MS  100
#define BOOT_POWER_TIMEOUT_MS 2000

#if (BOOT_POWER_WAIT_MS == 0) || (BOOT_POWER_STABLE_MS == 0) || (BOOT_POWER_TIMEOUT_MS == 0)
#error "Boot power timing values must be nonzero"
#endif

#if (BOOT_POWER_WAIT_MS >= BOOT_POWER_TIMEOUT_MS) || (BOOT_POWER_STABLE_MS >= BOOT_POWER_TIMEOUT_MS)
#error "Boot power wait and stability times must be shorter than its timeout"
#endif
