#pragma once

#include <stdint.h>

// Voltage ranges select the upper or lower half of the six-row pattern table.
typedef enum
{
    BLINK_VOLTAGE_LOW = 0,
    BLINK_VOLTAGE_HIGH,
} blink_voltage_t;

// Power modes select one of the three patterns within a voltage range.
typedef enum
{
    BLINK_POWER_ECO = 0,
    BLINK_POWER_NORMAL,
    BLINK_POWER_SPORT,
} blink_power_t;

#ifdef __cplusplus
extern "C"
{
#endif

// Configure the active-high status LED but leave it off until a pattern is set.
void blink_init(void);

// Advance non-blocking pattern playback; call once per application loop.
void blink_task(void);

// Select and immediately restart the pattern for a confirmed system state.
void blink_set_pattern(blink_voltage_t voltage, blink_power_t power);

#ifdef __cplusplus
}
#endif
