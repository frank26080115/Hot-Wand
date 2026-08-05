#pragma once

#include "hotwand.h"

#ifdef __cplusplus
extern "C" {
#endif

void fan_init(void);
void fan_task(void);
void fan_on_wake(void);
/* Stops the fan and prevents fan_task() from restarting it. */
void fan_stop(void);

#ifdef __cplusplus
}
#endif
