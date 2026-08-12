#pragma once

#include <stdint.h>

/*
 * RF power is selected by table row. Level zero explicitly disables the
 * output; levels one through six select rows zero through five.
 */
enum
{
    RFGEN_POWER_OFF     = 0,
    RFGEN_POWER_LEVEL_1 = 1,
    RFGEN_POWER_LEVEL_2 = 2,
    RFGEN_POWER_LEVEL_3 = 3,
    RFGEN_POWER_LEVEL_4 = 4,
    RFGEN_POWER_LEVEL_5 = 5,
    RFGEN_POWER_LEVEL_6 = 6,

    RFGEN_POWER_MINIMUM = RFGEN_POWER_LEVEL_1,
    RFGEN_POWER_MEDIUM  = RFGEN_POWER_LEVEL_3,
    RFGEN_POWER_MAXIMUM = RFGEN_POWER_LEVEL_6,
};

#define kRfFrequencyHz 470000

#ifdef __cplusplus
extern "C"
{
#endif

/* Values above level six are clamped to RFGEN_POWER_MAXIMUM. */
void rfgen_set(uint8_t power_level);

#ifdef __cplusplus
}
#endif
