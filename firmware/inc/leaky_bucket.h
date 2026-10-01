#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Update the power-energy bucket at the PWRLVL_UPDATE_PERIOD_MS cadence. */
void leaky_bucket_task(void);

/* Virtual temperature follows accumulated energy up to the configured
 * LEAKY_BUCKET_MAX_TEMPERATURE_C. */
uint16_t leaky_bucket_get_temperature_c(void);

#ifdef __cplusplus
}
#endif
