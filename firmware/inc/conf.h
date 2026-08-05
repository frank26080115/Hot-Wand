#pragma once

/* Project-wide settings are ordered from frequently tuned behavior to implementation details. */
/* Each validation block stays immediately after the settings it protects. */

// -----------------------------------------------------------------------------
// User Interaction and Setup
// -----------------------------------------------------------------------------

#ifndef BTN_LONG_PRESS_MS
#define BTN_LONG_PRESS_MS 1000
#endif

#ifndef SETUP_MENU_TIMEOUT_MS
#define SETUP_MENU_TIMEOUT_MS 300000
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
#define TEMPERATURE_HYSTERYSIS_C         5

#if (TEMPERATURE_FAN_THRESHOLD_LOW_C > TEMPERATURE_FAN_THRESHOLD_HIGH_C) ||                                            \
    (TEMPERATURE_HYSTERYSIS_C > TEMPERATURE_FAN_THRESHOLD_LOW_C) ||                                                    \
    (TEMPERATURE_HYSTERYSIS_C > TEMPERATURE_HOT_WARNING_THRESH_C)
#error "Invalid fan thresholds or temperature hysteresis"
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

#define OLED_DIM_CONTRAST      32
#define OLED_MAX_PIXEL_SHIFT_X 3
#define OLED_MAX_PIXEL_SHIFT_Y 3

/* Portrait power graph: 28 text pixels plus a 100-pixel, 0-100 W plot. */
/* Each 100 ms column captures the interval's peak; 32 columns show 3.2 seconds. */
#define PWRMGT_GRAPH_TEXT_HEIGHT_PX     28
#define PWRMGT_GRAPH_UPDATE_INTERVAL_MS 100
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
