#pragma once

#include "hotwand.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void adc_init(void);
uint16_t adc_get(uint8_t idx);
void adc_set_input_voltage_calibration(uint8_t calibration);

/* Unit conversions return 0 when idx does not represent that quantity. */
uint16_t adc_to_millivolts(uint8_t idx);
uint16_t adc_to_milliamps(uint8_t idx);
uint16_t adc_to_celcius(uint8_t idx);
uint32_t adc_get_milliwatts(void);

uint32_t adc_get_rand_seed(void);

#ifdef __cplusplus
}
#endif
