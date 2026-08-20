#pragma once

#include <stdint.h>

/*
 * RF waveform configuration. These values are intentionally kept together so
 * the modeled power behavior can be
 * tuned without touching the generator.
 */
#define RFGEN_MAXIMUM_POWER_PERCENT      100
#define RFGEN_MINIMUM_POWER_PERCENT      30
#define RFGEN_CONTINUOUS_POWER_PERCENT   100
#define RFGEN_STARTUP_POWER_PERCENT      80
#define RFGEN_STARTUP_PERIOD_COUNT       12
#define RFGEN_MINIMUM_BLANK_PERIOD_COUNT 8
#define RFGEN_TABLE_CAPACITY             512
#define RFGEN_FREQUENCY_HZ               470000

#ifdef __cplusplus
extern "C"
{
#endif

/* Request RF output power. Values above 100 are clamped to 100. */
void rfgen_set(uint8_t power_percent);

/* Print the currently generated DMA period table to Serial. */
void rfgen_print_table(void);

#ifdef __cplusplus
}
#endif
