#pragma once

#include "hotwand.h"
#include "pwrlvl.h"

#include <stdint.h>

enum
{
    PWRMGT_ATTENUATION_NONE          = 0U,
    PWRMGT_ATTENUATION_TEMPERATURE   = 1U << 0,
    PWRMGT_ATTENUATION_LOW_DC_INPUT  = 1U << 1,
    PWRMGT_ATTENUATION_CURRENT_LIMIT = 1U << 2,
};

#ifdef __cplusplus
extern "C" {
#endif

void pwrmgt_set_desired_power_level(pwrlvl_mode_t mode);
void pwrmgt_task(void);
pwrlvl_mode_t pwrmgt_get_applied_power_level(void);
uint8_t pwrmgt_get_attenuation_reasons(void);

#ifdef __cplusplus
}
#endif
