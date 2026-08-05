#pragma once

#include "hotwand.h"
#include "pwrlvl.h"
#include "u8g2.h"

#include <stdint.h>

enum
{
    PWRMGT_ATTENUATION_NONE          = 0U,
    PWRMGT_ATTENUATION_TEMPERATURE   = 1U << 0,
    PWRMGT_ATTENUATION_LOW_DC_INPUT  = 1U << 1,
    PWRMGT_ATTENUATION_CURRENT_LIMIT = 1U << 2,
};

#ifdef __cplusplus
extern "C"
{
#endif

void     pwrmgt_set_desired_power_level(pwrlvl_mode_t mode);
void     pwrmgt_set_idle_power_threshold(uint8_t threshold);
uint32_t pwrmgt_get_idle_duration_ms(void);
uint32_t pwrmgt_get_time_since_last_activity_ms(void);
/* Advances the desired level, or defers blocked feedback until button release. */
void pwrmgt_change_pwr_lvl(void);
void pwrmgt_task(void);
/* Draws only the graph region; the caller owns the text and buffer send. */
void          pwrmgt_render_graph(u8g2_t* graphics);
pwrlvl_mode_t pwrmgt_get_applied_power_level(void);
uint8_t       pwrmgt_get_attenuation_reasons(void);

#ifdef __cplusplus
}
#endif
