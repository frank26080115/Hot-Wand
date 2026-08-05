#pragma once

#include "hotwand.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Initializes the saved FAN_MODE_*; invalid modes fail safe as off. */
void fan_init(uint8_t mode);
void fan_task(void);
void fan_on_wake(void);
/* Stops the fan and prevents fan_task() from restarting it. */
void fan_stop(void);

#ifdef __cplusplus
}
#endif
