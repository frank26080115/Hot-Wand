#pragma once

#include "hotwand.h"

/* Ramp-down update period; ramp-up uses a fixed 2 ms period. */
#ifndef PWRLVL_UPDATE_PERIOD_MS
#define PWRLVL_UPDATE_PERIOD_MS 50
#endif

#if (PWRLVL_UPDATE_PERIOD_MS < 5) || (PWRLVL_UPDATE_PERIOD_MS > 100)
#error "PWRLVL_UPDATE_PERIOD_MS must be between 5 and 100 milliseconds"
#endif

typedef enum
{
    PWRLVL_MODE_100_PERCENT = 0,
    PWRLVL_MODE_75_PERCENT,
    PWRLVL_MODE_50_PERCENT,
} pwrlvl_mode_t;

#ifdef __cplusplus
extern "C"
{
#endif

void pwrlvl_init(void);
void pwrlvl_task(void);
void pwrlvl_set_mode(pwrlvl_mode_t mode);
bool pwrlvl_is_current_limiting(void);
void pwrlvl_force_minimum(void);
/* Releases a forced minimum and resumes regulation in the selected mode. */
void pwrlvl_release_minimum(void);

#ifdef __cplusplus
}
#endif
