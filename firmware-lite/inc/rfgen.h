#pragma once

#include <stdbool.h>
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

#ifdef HOTWANDLITE_MCU_ESP32C3

// these two special build paths fixes the problem of the RMT cutting off some carrier pulses
#define RFGEN_ESP32C3_RMT_EXPLICIT_PULSES
#define RFGEN_ESP32C3_MACRO_PULSE_BURSTS

#endif

#ifdef __cplusplus
extern "C"
{
#endif

/* Request RF output power. Values above 100 are clamped to 100. */
void rfgen_set(uint8_t power_percent);

/* Set carrier frequency in Hz, rounded to whole timer clocks. Preserves power.
 * Returns false for unsupported frequencies or a failed restart (output off).
 * A running output is briefly stopped; the setting lasts until reset.
 */
bool rfgen_set_freq(uint32_t frequency_hz);

/* Service optional macro bursts on every loop, including serial test mode. */
void rfgen_task(void);

/* Print the currently generated DMA period table to Serial. */
void rfgen_print_table(void);

#ifdef __cplusplus
}
#endif
