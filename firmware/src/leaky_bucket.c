// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "leaky_bucket.h"

#include "adc.h"
#include "conf.h"
#include "pwrlvl.h"
#include "systick.h"

#include <stdbool.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

/* mW * ms = microjoules. Heat loss grows with stored energy, so continuous
 * power approaches an equilibrium without an artificial energy cap. */
static uint64_t leaky_bucket_energy_uj;
static uint32_t leaky_bucket_last_update_ms;
static uint16_t leaky_bucket_temperature_c = LEAKY_BUCKET_EMPTY_TEMPERATURE_C;
static bool     leaky_bucket_time_initialized;

_Static_assert(LEAKY_BUCKET_COOLING_TIME_MS > PWRLVL_UPDATE_PERIOD_MS,
               "bucket cooling time must exceed its update period");

/* exp(x) >= 1 + x + x^2/2 gives a conservative lower bound on accumulated
 * energy by time t. The discrete update accumulates slightly faster. */
#define LEAKY_BUCKET_MIN_ACCUMULATED_UJ(power_mw, time_ms)                                                       \
    (((uint64_t)((power_mw) - LEAKY_BUCKET_LEAK_MW) * LEAKY_BUCKET_COOLING_TIME_MS * (time_ms) *                 \
      (2ULL * LEAKY_BUCKET_COOLING_TIME_MS + (time_ms))) /                                                        \
     (2ULL * LEAKY_BUCKET_COOLING_TIME_MS * LEAKY_BUCKET_COOLING_TIME_MS +                                        \
      2ULL * LEAKY_BUCKET_COOLING_TIME_MS * (time_ms) + (uint64_t)(time_ms) * (time_ms)))
_Static_assert(LEAKY_BUCKET_WARNING_ENERGY_UJ <= LEAKY_BUCKET_MIN_ACCUMULATED_UJ(80000UL, 30000UL),
               "80 W must reach warning within 30 s");
_Static_assert(LEAKY_BUCKET_WARNING_ENERGY_UJ <= LEAKY_BUCKET_MIN_ACCUMULATED_UJ(100000UL, 9000UL),
               "100 W must reach warning before 10 s");
_Static_assert(LEAKY_BUCKET_EMPTY_TEMPERATURE_C +
                       (((uint64_t)(100000UL - LEAKY_BUCKET_LEAK_MW) * LEAKY_BUCKET_COOLING_TIME_MS *
                         (TEMPERATURE_HOT_WARNING_THRESH_C + 1U - LEAKY_BUCKET_EMPTY_TEMPERATURE_C)) /
                        LEAKY_BUCKET_WARNING_ENERGY_UJ) ==
                   200U,
               "uncapped virtual equilibrium at 100 W must be 200 C");

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void leaky_bucket_task(void)
{
    uint32_t now = systick_get_ms();
    uint32_t periods;
    uint32_t power_mw;
    uint32_t period;

    if (!leaky_bucket_time_initialized)
    {
        leaky_bucket_last_update_ms = now;
        leaky_bucket_time_initialized = true;
        return;
    }

    periods = (uint32_t)(now - leaky_bucket_last_update_ms) / PWRLVL_UPDATE_PERIOD_MS;
    if (periods == 0)
    {
        return;
    }
    leaky_bucket_last_update_ms += periods * PWRLVL_UPDATE_PERIOD_MS;

    /* Apply each missed fixed-rate update using the latest power sample. */
    power_mw = adc_get_milliwatts();
    for (period = 0; period < periods; ++period)
    {
        uint64_t passive_loss_uj;
        uint64_t loss_uj;
        uint32_t input_uj;

        /* E / cooling_time is a power in mW. Split the product so even a
         * representationally full uint64_t bucket cannot overflow here. */
        passive_loss_uj = (leaky_bucket_energy_uj / LEAKY_BUCKET_COOLING_TIME_MS) * PWRLVL_UPDATE_PERIOD_MS;
        passive_loss_uj +=
            ((leaky_bucket_energy_uj % LEAKY_BUCKET_COOLING_TIME_MS) * PWRLVL_UPDATE_PERIOD_MS) /
            LEAKY_BUCKET_COOLING_TIME_MS;
        loss_uj  = (LEAKY_BUCKET_LEAK_MW * PWRLVL_UPDATE_PERIOD_MS) + passive_loss_uj;
        input_uj = power_mw * PWRLVL_UPDATE_PERIOD_MS;

        if (input_uj >= loss_uj)
        {
            uint64_t gain_uj = input_uj - loss_uj;
            if (gain_uj > UINT64_MAX - leaky_bucket_energy_uj)
            {
                leaky_bucket_energy_uj = UINT64_MAX;
            }
            else
            {
                leaky_bucket_energy_uj += gain_uj;
            }
        }
        else
        {
            uint64_t drain_uj = loss_uj - input_uj;
            leaky_bucket_energy_uj = drain_uj >= leaky_bucket_energy_uj ? 0 : leaky_bucket_energy_uj - drain_uj;
        }
    }

    /* The normal temperature hysteresis in pwrmgt_task() controls release.
     * The configured maximum affects only the reported temperature. */
    {
        const uint32_t temperature_span_c =
            TEMPERATURE_HOT_WARNING_THRESH_C + 1U - LEAKY_BUCKET_EMPTY_TEMPERATURE_C;
        const uint64_t max_temperature_energy_uj =
            ((uint64_t)(LEAKY_BUCKET_MAX_TEMPERATURE_C - LEAKY_BUCKET_EMPTY_TEMPERATURE_C) *
                 LEAKY_BUCKET_WARNING_ENERGY_UJ +
             temperature_span_c - 1U) /
            temperature_span_c;

        /* Compare before scaling so the multiply cannot overflow. */
        if (leaky_bucket_energy_uj >= max_temperature_energy_uj)
        {
            leaky_bucket_temperature_c = LEAKY_BUCKET_MAX_TEMPERATURE_C;
        }
        else
        {
            leaky_bucket_temperature_c =
                LEAKY_BUCKET_EMPTY_TEMPERATURE_C +
                (uint16_t)((leaky_bucket_energy_uj * temperature_span_c) / LEAKY_BUCKET_WARNING_ENERGY_UJ);
        }
    }
}

uint16_t leaky_bucket_get_temperature_c(void)
{
    return leaky_bucket_temperature_c;
}
