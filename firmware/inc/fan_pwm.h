#pragma once

#include "conf.h"
#include "hotwand.h"

#if FAN_PWM_ENABLED

typedef enum
{
    /* PA13 drives the fan's PWM input directly. */
    FANPWM_MODE_DIRECT = 0,
    /* PA13 drives Q8's gate; Q8's pulled-up drain drives the fan PWM input. */
    FANPWM_MODE_EXTERNAL_MOSFET
} fanpwm_mode_t;

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Claims PA13 from SWD and starts the 25 kHz PWM hardware at a zero-percent
 * fan command. Invalid modes leave PA13 and both timers untouched. Calling
 * this again safely reinitializes the hardware at zero percent.
 */
bool fanpwm_init(fanpwm_mode_t mode);

/*
 * Sets fan PWM-input high time from 0 through 100 percent. Values above 100
 * are clamped. Calls made before successful initialization have no effect.
 */
void fanpwm_set(uint8_t duty_percent);

#ifdef __cplusplus
}
#endif /* FAN_PWM_ENABLED */

#endif
