#pragma once

/*
use this file for compile time configurable options
especially if subsequent definitions of types depend on them
*/

#ifndef BTN_LONG_PRESS_MS
#define BTN_LONG_PRESS_MS 1000
#endif

#define PWRLVL_SHORT_CIRCUIT_CURRENT_MA 5800U
#define PWRLVL_SHORT_CIRCUIT_TIME_MS 5000UL

#if (PWRLVL_SHORT_CIRCUIT_CURRENT_MA == 0U) || (PWRLVL_SHORT_CIRCUIT_CURRENT_MA > 65535U)
#error "PWRLVL_SHORT_CIRCUIT_CURRENT_MA must fit in milliamps"
#endif

#if PWRLVL_SHORT_CIRCUIT_TIME_MS == 0UL
#error "PWRLVL_SHORT_CIRCUIT_TIME_MS must be nonzero"
#endif

#define TEMPERATURE_HYSTERYSIS_C 5
#define TEMPERATURE_FAN_THRESHOLD_LOW_C 50
#define TEMPERATURE_FAN_THRESHOLD_HIGH_C 80
#define TEMPERATURE_HOT_WARNING_THRESH_C 80

#define OLED_MAX_PIXEL_SHIFT_X 3
#define OLED_MAX_PIXEL_SHIFT_Y 3

#define OLED_DIM_CONTRAST 32U

/* Portrait power graph: 28 text pixels plus a 100-pixel, 0-100 W plot.
 * Each 100 ms column captures the peak power observed during its interval,
 * so the 32 columns show 3.2 seconds of history. */
#define PWRMGT_GRAPH_TEXT_HEIGHT_PX 28U
#define PWRMGT_GRAPH_UPDATE_INTERVAL_MS 100UL
#define PWRMGT_GRAPH_MAX_POWER_MW 100000UL

#if PWRMGT_GRAPH_TEXT_HEIGHT_PX >= 128U
#error "PWRMGT_GRAPH_TEXT_HEIGHT_PX must leave room for the graph"
#endif

#if PWRMGT_GRAPH_UPDATE_INTERVAL_MS == 0UL
#error "PWRMGT_GRAPH_UPDATE_INTERVAL_MS must be nonzero"
#endif

#if PWRMGT_GRAPH_MAX_POWER_MW == 0UL
#error "PWRMGT_GRAPH_MAX_POWER_MW must be nonzero"
#endif

#define BOOT_POWER_WAIT_MS 300UL
#define BOOT_POWER_STABLE_MS 100UL
#define BOOT_POWER_TIMEOUT_MS 2000UL
#define SETUP_HOLD_DURATION_MS 5000UL

#if (BOOT_POWER_WAIT_MS == 0UL) || (BOOT_POWER_STABLE_MS == 0UL) || (BOOT_POWER_TIMEOUT_MS == 0UL)
#error "Boot power timing values must be nonzero"
#endif

#if (BOOT_POWER_WAIT_MS >= BOOT_POWER_TIMEOUT_MS) || (BOOT_POWER_STABLE_MS >= BOOT_POWER_TIMEOUT_MS)
#error "Boot power wait and stability times must be shorter than its timeout"
#endif

#if SETUP_HOLD_DURATION_MS == 0UL
#error "Setup hold duration must be nonzero"
#endif

#ifndef BOOT_DC_READY_MV
#define BOOT_DC_READY_MV 20000U
#endif

#if (BOOT_DC_READY_MV == 0U) || (BOOT_DC_READY_MV > 65535U)
#error "BOOT_DC_READY_MV must be between 1 and 65535 millivolts"
#endif

#ifndef SETUP_MENU_TIMEOUT_MS
#define SETUP_MENU_TIMEOUT_MS 300000
#endif

#define BATTERY_MINIMUM_CELL_CNT_LIPO 4
#define BATTERY_MINIMUM_CELL_CNT_LIHV 4
#define BATTERY_MINIMUM_CELL_CNT_LIFE 6

#define BATTERY_MAXIMUM_CELL_CNT_LIPO 8
#define BATTERY_MAXIMUM_CELL_CNT_LIHV 8
#define BATTERY_MAXIMUM_CELL_CNT_LIFE 9

#define BATTERY_OVERRIDE_MINIMUM_MV 14000U

#if (BATTERY_MINIMUM_CELL_CNT_LIPO == 0) || (BATTERY_MINIMUM_CELL_CNT_LIPO > BATTERY_MAXIMUM_CELL_CNT_LIPO) ||         \
    (BATTERY_MINIMUM_CELL_CNT_LIHV == 0) || (BATTERY_MINIMUM_CELL_CNT_LIHV > BATTERY_MAXIMUM_CELL_CNT_LIHV) ||         \
    (BATTERY_MINIMUM_CELL_CNT_LIFE == 0) || (BATTERY_MINIMUM_CELL_CNT_LIFE > BATTERY_MAXIMUM_CELL_CNT_LIFE)
#error "Invalid battery chemistry cell-count limits"
#endif

#if (BATTERY_MAXIMUM_CELL_CNT_LIPO > 255) || (BATTERY_MAXIMUM_CELL_CNT_LIHV > 255) ||                                  \
    (BATTERY_MAXIMUM_CELL_CNT_LIFE > 255)
#error "Battery cell-count limits must fit in uint8_t"
#endif

#if (BATTERY_OVERRIDE_MINIMUM_MV == 0U) || (BATTERY_OVERRIDE_MINIMUM_MV > 65535U)
#error "BATTERY_OVERRIDE_MINIMUM_MV must fit in millivolts"
#endif

#define DC_HIGH_POWER_MINIMUM_MV 19000
#define DC_HIGH_POWER_HYSTERESIS_MV 1000

#if TEMPERATURE_HYSTERYSIS_C > TEMPERATURE_HOT_WARNING_THRESH_C
#error "Temperature hysteresis exceeds the hot-warning threshold"
#endif

#if (DC_HIGH_POWER_MINIMUM_MV <= 0) || (DC_HIGH_POWER_MINIMUM_MV > 65535) || (DC_HIGH_POWER_HYSTERESIS_MV < 0) ||      \
    ((DC_HIGH_POWER_MINIMUM_MV + DC_HIGH_POWER_HYSTERESIS_MV) > 65535)
#error "Invalid high-power DC threshold or hysteresis"
#endif
