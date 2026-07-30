#ifndef HOT_WAND_ADC_H
#define HOT_WAND_ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void adc_init(void);
uint16_t adc_get(uint8_t idx);

/* Unit conversions return 0 when idx does not represent that quantity. */
uint16_t adc_to_millivolts(uint8_t idx);
uint16_t adc_to_milliamps(uint8_t idx);
uint16_t adc_to_celcius(uint8_t idx);
uint32_t adc_get_milliwatts(void);

#ifdef __cplusplus
}
#endif

#endif
