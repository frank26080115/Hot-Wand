#pragma once

#include "hotwand.h"

#include <stdbool.h>
#include <stdint.h>

/* One averaged DC-input record is published after this many complete ADC
 * round-robin passes.  At the configured ADC
 * cadence this is about 10 ms. */
#define ADC_DC_VOLTAGE_HISTORY_COUNT            64
#define ADC_DC_VOLTAGE_HISTORY_ROUNDS_PER_ENTRY 128
#define ADC_DC_VOLTAGE_HISTORY_INTERVAL_MS      10

#ifdef __cplusplus
extern "C"
{
#endif

void     adc_init(void);
uint16_t adc_get(uint8_t idx);
void     adc_set_input_voltage_calibration(uint8_t calibration);

/* Unit conversions return 0 when idx does not represent that quantity. */
uint16_t adc_to_millivolts(uint8_t idx);
uint16_t adc_to_milliamps(uint8_t idx);
uint16_t adc_to_celcius(uint8_t idx);
uint32_t adc_get_milliwatts(void);

/* Copies newest-first averaged raw DC-input ADC records.  The ADC interrupt
 * may keep writing while this takes a
 *
 * coherent snapshot. */
uint8_t  adc_copy_dc_voltage_history(uint16_t* history, uint8_t capacity);
uint32_t adc_get_completed_round_count(void);
bool     adc_has_power_loss_shutdown(void);
bool     adc_has_buck_voltage_spike_shutdown(void);

uint32_t adc_get_rand_seed(void);

#ifdef __cplusplus
}
#endif
