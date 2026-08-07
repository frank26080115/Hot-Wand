#pragma once

#include <stdint.h>

/**
 * Request RF output power as a percentage. Decreases take effect immediately;
 * increases are applied gradually by rfgen_task().
 *
 * @param power_percent 0 is off, 100 is continuously on, and values
 * above 100 are treated as 100.
 * @param period_ms Burst period at partial power. The default, and an explicit zero,
 * select 10 ms.
 */
#ifdef __cplusplus
extern "C"
{
void rfgen_set(uint8_t power_percent, uint32_t period_ms = 10);
void rfgen_task(void);
}
#else
void rfgen_set(uint8_t power_percent, uint32_t period_ms);
void rfgen_task(void);
#endif
