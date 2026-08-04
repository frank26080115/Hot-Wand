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

