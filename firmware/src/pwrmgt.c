#include "pwrmgt.h"

#include "adc.h"
#include "conf.h"
#include "pins.h"
#include "pwrlvl.h"

#include <stdbool.h>
#include <stdint.h>

_Static_assert((THERM_2_IDX == (THERM_1_IDX + 1U)) &&
                   (MCU_TEMP_IDX == (THERM_2_IDX + 1U)),
               "temperature ADC indices must remain contiguous");

static pwrlvl_mode_t pwrmgt_desired_power_level =
    PWRLVL_MODE_100_PERCENT;
static pwrlvl_mode_t pwrmgt_applied_power_level =
    PWRLVL_MODE_100_PERCENT;
static uint8_t pwrmgt_attenuation_reasons;
static bool pwrmgt_temperature_limited;
static bool pwrmgt_low_dc_limited;

void pwrmgt_set_desired_power_level(pwrlvl_mode_t mode)
{
    if ((uint32_t)mode <= (uint32_t)PWRLVL_MODE_50_PERCENT) {
        pwrmgt_desired_power_level = mode;
    }
}

void pwrmgt_task(void)
{
    pwrlvl_mode_t next_power_level;
    uint16_t dc_input_millivolts;
    uint16_t highest_temperature;
    uint16_t temperature;
    uint16_t temperature_limit;
    uint16_t dc_limit;
    uint8_t adc_idx;
    uint8_t reasons = PWRMGT_ATTENUATION_NONE;

    highest_temperature = 0U;
    for (adc_idx = THERM_1_IDX; adc_idx <= MCU_TEMP_IDX; ++adc_idx) {
        temperature = adc_to_celcius(adc_idx);
        if (temperature > highest_temperature) {
            highest_temperature = temperature;
        }
    }
    temperature_limit = pwrmgt_temperature_limited
                            ? (TEMPERATURE_HOT_WARNING_THRESH_C -
                               TEMPERATURE_HYSTERYSIS_C)
                            : TEMPERATURE_HOT_WARNING_THRESH_C;
    pwrmgt_temperature_limited =
        highest_temperature > temperature_limit;

    dc_input_millivolts = adc_to_millivolts(DC_SENS_IDX);
    dc_limit = pwrmgt_low_dc_limited
                   ? (DC_HIGH_POWER_MINIMUM_MV +
                      DC_HIGH_POWER_HYSTERESIS_MV)
                   : DC_HIGH_POWER_MINIMUM_MV;
    pwrmgt_low_dc_limited = dc_input_millivolts < dc_limit;

    if (pwrmgt_temperature_limited) {
        reasons |= PWRMGT_ATTENUATION_TEMPERATURE;
    }
    if (pwrmgt_low_dc_limited) {
        reasons |= PWRMGT_ATTENUATION_LOW_DC_INPUT;
    }

    /* Low input voltage may not recover while the tool remains loaded.  The
     * buck converter's current ceiling also naturally caps the power that a
     * low input voltage can provide; this explicit 50 W cap is intentional. */
    if (pwrmgt_temperature_limited || pwrmgt_low_dc_limited) {
        next_power_level = PWRLVL_MODE_50_PERCENT;
    } else {
        next_power_level = pwrmgt_desired_power_level;
    }

    pwrlvl_set_mode(next_power_level);
    pwrmgt_applied_power_level = next_power_level;

    pwrlvl_task();
    if (pwrlvl_is_current_limiting()) {
        reasons |= PWRMGT_ATTENUATION_CURRENT_LIMIT;
    }

    pwrmgt_attenuation_reasons = reasons;
}

pwrlvl_mode_t pwrmgt_get_applied_power_level(void)
{
    return pwrmgt_applied_power_level;
}

uint8_t pwrmgt_get_attenuation_reasons(void)
{
    return pwrmgt_attenuation_reasons;
}
