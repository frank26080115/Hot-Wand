#pragma once

#include <stdint.h>

/**
 * Request RF output power as a percentage. Decreases take effect immediately;
 * increases are applied gradually by rfgen_task().
 *
 * @param power_percent 0 is off, 100 is continuously on, and values
 * above 100 are treated as 100.
 * @param period_us Burst period at partial power. The default, and an explicit zero,
 * select 10000 us.
 */

#define kMaximumPowerPercent  100
#define kDefaultBurstPeriodUs 10000
#define kPowerRampDurationMs  1000
#define kRfFrequencyHz        470000

#ifdef __cplusplus
extern "C"
{
void rfgen_set(uint8_t power_percent, uint32_t period_us = kDefaultBurstPeriodUs);
void rfgen_task(void);
}
#else
void rfgen_set(uint8_t power_percent, uint32_t period_us);
void rfgen_task(void);
#endif
