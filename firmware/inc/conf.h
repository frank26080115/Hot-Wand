#pragma once

/*
use this file for compile time configurable options
especially if subsequent definitions of types depend on them
*/

#define TEMPERATURE_HYSTERYSIS_C 5
#define TEMPERATURE_FAN_THRESHOLD_LOW_C 50
#define TEMPERATURE_FAN_THRESHOLD_HIGH_C 80
#define TEMPERATURE_HOT_WARNING_THRESH_C 80

#define OLED_MAX_PIXEL_SHIFT_X 3
#define OLED_MAX_PIXEL_SHIFT_Y 3

#define OLED_DIM_CONTRAST 123 // I don't actually know what the range is

#ifndef BOOT_DC_READY_MV
#define BOOT_DC_READY_MV 20000U
#endif

#if (BOOT_DC_READY_MV == 0U) || (BOOT_DC_READY_MV > 65535U)
#error "BOOT_DC_READY_MV must be between 1 and 65535 millivolts"
#endif

#define BATTERY_MINIMUM_CELL_CNT_LIPO    4
#define BATTERY_MINIMUM_CELL_CNT_LIHV    4
#define BATTERY_MINIMUM_CELL_CNT_LIFE    6

#define BATTERY_MAXIMUM_CELL_CNT_LIPO    8
#define BATTERY_MAXIMUM_CELL_CNT_LIHV    8
#define BATTERY_MAXIMUM_CELL_CNT_LIFE    9

#define DC_HIGH_POWER_MINIMUM_MV 19000
#define DC_HIGH_POWER_HYSTERESIS_MV 1000

#if TEMPERATURE_HYSTERYSIS_C > TEMPERATURE_HOT_WARNING_THRESH_C
#error "Temperature hysteresis exceeds the hot-warning threshold"
#endif

#if (DC_HIGH_POWER_MINIMUM_MV <= 0) || \
    (DC_HIGH_POWER_MINIMUM_MV > 65535) || \
    (DC_HIGH_POWER_HYSTERESIS_MV < 0) || \
    ((DC_HIGH_POWER_MINIMUM_MV + DC_HIGH_POWER_HYSTERESIS_MV) > 65535)
#error "Invalid high-power DC threshold or hysteresis"
#endif
