#pragma once

#include "hotwand.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Initializes the saved FAN_MODE_*; invalid modes fail safe as off. */
void fan_init(uint8_t mode);
/* Performs the optional delayed, one-shot external NTC health warning. */
void ntc_task(void);
void fan_task(void);
void fan_on_wake(void);
/* Stops the fan and prevents fan_task() from restarting it. */
void fan_stop(void);
/* Re-enables fan_task() after a recoverable stop, preserving the configured mode. */
void fan_resume(void);

#ifdef __cplusplus
}
#endif
