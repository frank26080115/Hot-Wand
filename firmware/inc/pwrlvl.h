#pragma once

#include "hotwand.h"

/* Ramp-down update period; ramp-up uses a fixed 2 ms period. */
#ifndef PWRLVL_UPDATE_PERIOD_MS
#define PWRLVL_UPDATE_PERIOD_MS 50U
#endif

#if (PWRLVL_UPDATE_PERIOD_MS < 5U) || (PWRLVL_UPDATE_PERIOD_MS > 100U)
#error "PWRLVL_UPDATE_PERIOD_MS must be between 5 and 100 milliseconds"
#endif

typedef enum
{
    PWRLVL_MODE_100_PERCENT = 0,
    PWRLVL_MODE_75_PERCENT,
    PWRLVL_MODE_50_PERCENT,
} pwrlvl_mode_t;

#ifdef __cplusplus
extern "C" {
#endif

void pwrlvl_init(void);
void pwrlvl_task(void);
void pwrlvl_set_mode(pwrlvl_mode_t mode);
void pwrlvl_force_minimum(void);

#ifdef __cplusplus
}
#endif
