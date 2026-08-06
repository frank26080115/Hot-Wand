#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Read the divided supply input and return the reconstructed millivolts.
uint32_t pwrmgt_read_voltage_mv(void);

// Monitor inputs, confirm stable changes, and update RF and LED state.
void pwrmgt_task(void);

#ifdef __cplusplus
}
#endif
