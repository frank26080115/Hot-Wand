/*
The circuit of the Hot Wand has a main buck converter
This particular code module is responsible for controlling the output voltage of that buck converter
The buck converter's feedback signal is connected to our PWM pin through a resistor and diode
By sending a PWM signal, a higher duty cycle will drive the feedback signal higher, which will lower the output voltage
of the buck converter. The existing regulator ramps PWM up and down; SPORT also applies immediate attenuation from
new power-rise peaks with an independent slow decay. This code module monitors buck current and limits the
output voltage if the current draw exceeds a certain threshold
*/

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "fault_test.h"
#include "pwrlvl.h"

#include "adc.h"
#include "conf.h"
#include "fault.h"
#include "pins.h"
#include "stm32f0xx_hal.h"
#include "systick.h"

#include <stdbool.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define PWRLVL_LIMIT_100_PERCENT_MW 100000
#define PWRLVL_LIMIT_75_PERCENT_MW  75000
#define PWRLVL_LIMIT_50_PERCENT_MW  50000

#define PWRLVL_PWM_MAX       31
#define PWRLVL_PWM_FULL_DUTY (PWRLVL_PWM_MAX + 1)

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static pwrlvl_mode_t pwrlvl_mode = PWRLVL_MODE_100_PERCENT;
static uint32_t      pwrlvl_last_update_ms;
#if PWRLVL_POWER_LIMIT_ENABLED
static uint32_t pwrlvl_previous_power_mw;
#endif
static uint32_t pwrlvl_short_circuit_started_ms;
#if PWRLVL_CURRENT_LIMIT_ENABLED
static uint16_t pwrlvl_previous_current_ma;
#endif
// Keep the requested regulator value separate from the value already written
// to hardware, including builds that compile out the SPORT contribution.
static uint8_t pwrlvl_pwm_value;
static uint8_t pwrlvl_regulator_value;
#if PWRLVL_POWER_RISE_ENABLED
// Each controller owns its demand and timing; the output is their maximum.
static uint8_t  pwrlvl_rise_value;
static uint8_t  pwrlvl_rise_pending;
static uint32_t pwrlvl_rise_peak_mw;
static uint32_t pwrlvl_rise_last_decay_ms;
#endif
static bool pwrlvl_initialized;
static bool pwrlvl_forced_minimum;
static bool pwrlvl_current_limiting;
static bool pwrlvl_short_circuit_timing;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

#if PWRLVL_POWER_LIMIT_ENABLED
static uint32_t pwrlvl_get_limit_mw(void);
#endif
static void pwrlvl_set_pwm(uint8_t value);
static void pwrlvl_apply_requests(void);
#if PWRLVL_POWER_RISE_ENABLED
static void pwrlvl_clear_power_rise(void);
#endif

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void pwrlvl_task(void)
{
#if PWRLVL_POWER_LIMIT_ENABLED
    uint32_t current_power_mw;
    uint32_t limit_mw;
#endif
    uint32_t now;
    uint32_t update_period_ms;
    uint16_t current_ma;
    uint8_t  next_pwm;
    bool     ramp_up;

    if (!pwrlvl_initialized)
    {
        pwrlvl_current_limiting = false;
        return;
    }

    now        = systick_get_ms();
    current_ma = adc_to_milliamps(CURR_SENS_IDX);

#if PWRLVL_CURRENT_LIMIT_ENABLED
    /* Sustained overcurrent above 5.8 A for 5 seconds causes a terminal
     * fault; any sample at or below 5.8 A cancels the pending interval. The
     * normal 5.2 A limiter reduces output before this higher threshold. Both
     * protections share this compile guard, and conf.h requires their enable
     * settings to match. Final firmware enables both protections. */
    if (current_ma > PWRLVL_SHORT_CIRCUIT_CURRENT_MA)
    {
        if (!pwrlvl_short_circuit_timing)
        {
            FT_SET(qualify_ms, now);
            pwrlvl_short_circuit_started_ms = now;
            pwrlvl_short_circuit_timing     = true;
        }
        else if ((uint32_t)(now - pwrlvl_short_circuit_started_ms) >= PWRLVL_SHORT_CIRCUIT_TIME_MS)
        {
            /* show_fault() does not return and safely shuts down all outputs. */
            show_fault("SHORT\nCIRKT\nFAULT", false);
        }
    }
    else
    {
        pwrlvl_short_circuit_timing = false;
    }
#endif

    if (pwrlvl_forced_minimum)
    {
        pwrlvl_current_limiting = false;
        return;
    }

    ramp_up = false;
#if PWRLVL_POWER_LIMIT_ENABLED
    /*
     * Sample on every task call. Keeping the previous sample in the decision
     * prevents a single over-limit reading from being discarded immediately.
     */
    current_power_mw         = adc_get_milliwatts();
    limit_mw                 = pwrlvl_get_limit_mw();
    ramp_up                  = (current_power_mw > limit_mw) || (pwrlvl_previous_power_mw > limit_mw);
    pwrlvl_previous_power_mw = current_power_mw;
#endif

#if PWRLVL_CURRENT_LIMIT_ENABLED
    /*
     * The current ceiling is independent of the selected power mode.  Keep
     * one previous reading in the decision, matching the power-limit response
     * and ensuring an over-current sample gets at least one fast ramp step.
     */
    pwrlvl_current_limiting =
        (current_ma >= PWRLVL_CURRENT_LIMIT_MA) || (pwrlvl_previous_current_ma >= PWRLVL_CURRENT_LIMIT_MA);
    ramp_up                    = ramp_up || pwrlvl_current_limiting;
    pwrlvl_previous_current_ma = current_ma;
#else
    pwrlvl_current_limiting = false;
#endif

    update_period_ms = ramp_up ? PWRLVL_RAMP_UP_PERIOD_MS : PWRLVL_UPDATE_PERIOD_MS;
    if ((uint32_t)(now - pwrlvl_last_update_ms) >= update_period_ms)
    {
        pwrlvl_last_update_ms = now;
        next_pwm              = pwrlvl_regulator_value;
        if (ramp_up)
        {
            // An existing stronger output must not weaken the current-limit
            // response. Its attack clock is never reset by spike observations.
            if (pwrlvl_pwm_value > next_pwm)
            {
                next_pwm = pwrlvl_pwm_value;
            }
            if (next_pwm < PWRLVL_PWM_JUMP_LEVEL)
            {
                next_pwm = PWRLVL_PWM_JUMP_LEVEL;
            }
            else if (next_pwm < PWRLVL_PWM_MAX)
            {
                next_pwm += PWRLVL_RAMP_UP_STEP;
            }
            if (next_pwm > PWRLVL_PWM_MAX)
            {
                next_pwm = PWRLVL_PWM_MAX;
            }
        }
        else if (next_pwm > 0)
        {
            --next_pwm;
        }
        pwrlvl_regulator_value = next_pwm;
    }

#if PWRLVL_POWER_RISE_ENABLED
    // The free-running decay is independent of power direction and regulator
    // timing. Delayed calls remove only one count, never a catch-up burst.
    if ((uint32_t)(now - pwrlvl_rise_last_decay_ms) >= PWRLVL_POWER_RISE_DECAY_PERIOD_MS)
    {
        pwrlvl_rise_last_decay_ms = now;
        if (pwrlvl_rise_value > 0)
        {
            --pwrlvl_rise_value;
        }
    }
    // Decay first: a simultaneous new peak then establishes its mapped value.
    if (pwrlvl_rise_pending > pwrlvl_rise_value)
    {
        pwrlvl_rise_value = pwrlvl_rise_pending;
    }
    pwrlvl_rise_pending = 0;
#endif
    pwrlvl_apply_requests();
}

void pwrlvl_init(void)
{
#if !HOT_WAND_TEST_HARNESS
    GPIO_InitTypeDef gpio_cfg = {0};
#endif
    uint8_t initial_pwm_value;

    if (pwrlvl_initialized)
    {
        return;
    }

#if HOT_WAND_TEST_HARNESS
    /* Reuse the physical GPIO-high path without latching a logical override:
     * limiter calculations run normally, disconnected from the power stage. */
    bool logical_override = pwrlvl_forced_minimum;
    pwrlvl_force_minimum();
    pwrlvl_forced_minimum = logical_override;
    initial_pwm_value     = logical_override ? PWRLVL_PWM_FULL_DUTY : 0;
    FT_SET(pwm, initial_pwm_value);
#else
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM3_FORCE_RESET();
    __HAL_RCC_TIM3_RELEASE_RESET();

    /*
     * Keep the GPIO at the state that matches the initial timer output while
     * TIM3 is configured.  Normally this is low/zero attenuation.  A caller
     * that requested minimum output before initialization instead gets a
     * continuously high attenuation signal with no low-going interval.
     */
    initial_pwm_value = pwrlvl_forced_minimum ? PWRLVL_PWM_FULL_DUTY : 0;
    HAL_GPIO_WritePin(PWR_ATTENU_GPIOx, PWR_ATTENU_PINn, pwrlvl_forced_minimum ? GPIO_PIN_SET : GPIO_PIN_RESET);

    gpio_cfg.Pin   = PWR_ATTENU_PINn;
    gpio_cfg.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_cfg.Pull  = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(PWR_ATTENU_GPIOx, &gpio_cfg);

    TIM3->PSC   = 0;
    TIM3->ARR   = PWRLVL_PWM_MAX;
    TIM3->CCR1  = initial_pwm_value;
    TIM3->CCMR1 = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1PE;
    TIM3->EGR   = TIM_EGR_UG;
    TIM3->SR    = 0;
    TIM3->CCER  = TIM_CCER_CC1E;
    TIM3->CR1   = TIM_CR1_CEN;

    gpio_cfg.Mode      = GPIO_MODE_AF_PP;
    gpio_cfg.Alternate = GPIO_AF1_TIM3;
    HAL_GPIO_Init(PWR_ATTENU_GPIOx, &gpio_cfg);

#endif

    pwrlvl_pwm_value       = initial_pwm_value;
    pwrlvl_regulator_value = initial_pwm_value;
#if PWRLVL_POWER_RISE_ENABLED
    pwrlvl_clear_power_rise();
#endif
#if PWRLVL_POWER_LIMIT_ENABLED
    pwrlvl_previous_power_mw = 0;
#endif
    pwrlvl_short_circuit_started_ms = 0;
    pwrlvl_short_circuit_timing     = false;
#if PWRLVL_CURRENT_LIMIT_ENABLED
    pwrlvl_previous_current_ma = 0;
#endif
    pwrlvl_last_update_ms = systick_get_ms();
    pwrlvl_initialized    = true;
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

void pwrlvl_force_minimum(void)
{
    /*
     * Puts the output of the buck converter to a minimum voltage level
     * by driving the feedback signal high through a resistor
     */

    GPIO_InitTypeDef gpio_cfg = {0};

    /*
     * A static high is electrically identical to 100 percent PWM and avoids
     * briefly requesting maximum buck output merely to initialize TIM3.  If
     * PWM was already running, first move its compare above ARR so both sides
     * of the alternate-function-to-GPIO handoff are high.
     */
    pwrlvl_forced_minimum = true;
#if PWRLVL_POWER_RISE_ENABLED
    pwrlvl_clear_power_rise();
#endif
    pwrlvl_regulator_value  = PWRLVL_PWM_FULL_DUTY;
    pwrlvl_current_limiting = false;

#if !HOT_WAND_TEST_HARNESS
    if (pwrlvl_initialized)
    {
        TIM3->CCR1 = PWRLVL_PWM_FULL_DUTY;
    }
#endif

    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(PWR_ATTENU_GPIOx, PWR_ATTENU_PINn, GPIO_PIN_SET);

    gpio_cfg.Pin   = PWR_ATTENU_PINn;
    gpio_cfg.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_cfg.Pull  = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(PWR_ATTENU_GPIOx, &gpio_cfg);

    FT_SET(pwm, PWRLVL_PWM_FULL_DUTY);
    pwrlvl_pwm_value = PWRLVL_PWM_FULL_DUTY;
}

void pwrlvl_release_minimum(void)
{
#if !HOT_WAND_TEST_HARNESS
    GPIO_InitTypeDef gpio_cfg = {0};
#endif

    if (!pwrlvl_forced_minimum)
    {
        return;
    }

    if (!pwrlvl_initialized)
    {
        pwrlvl_forced_minimum = false;
        return;
    }

    /* Resume at almost full attenuation, then let pwrlvl_task() ramp toward
     * the current limit (and the selected wattage ceiling when enabled). */
#if !HOT_WAND_TEST_HARNESS
    TIM3->CCR1         = PWRLVL_PWM_MAX;
    gpio_cfg.Pin       = PWR_ATTENU_PINn;
    gpio_cfg.Mode      = GPIO_MODE_AF_PP;
    gpio_cfg.Pull      = GPIO_NOPULL;
    gpio_cfg.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_cfg.Alternate = GPIO_AF1_TIM3;
    HAL_GPIO_Init(PWR_ATTENU_GPIOx, &gpio_cfg);
#endif
    /* In the shared test build this releases only the logical override.
     * PA6 remains GPIO-high, independent of the requested PWM below. */
    FT_SET(pwm, PWRLVL_PWM_MAX);
    pwrlvl_pwm_value       = PWRLVL_PWM_MAX;
    pwrlvl_regulator_value = PWRLVL_PWM_MAX;
#if PWRLVL_POWER_RISE_ENABLED
    pwrlvl_clear_power_rise();
#endif
    pwrlvl_last_update_ms = systick_get_ms();
    pwrlvl_forced_minimum = false;
}

void pwrlvl_set_mode(pwrlvl_mode_t mode)
{
    switch (mode)
    {
    case PWRLVL_MODE_100_PERCENT:
    case PWRLVL_MODE_75_PERCENT:
    case PWRLVL_MODE_50_PERCENT:
#if PWRLVL_POWER_RISE_ENABLED
        if (pwrlvl_mode != mode)
        {
            // Leaving SPORT clears the contribution immediately; entering
            // SPORT starts a new running peak even midway through a segment.
            pwrlvl_clear_power_rise();
        }
#endif
        pwrlvl_mode = mode;
        pwrlvl_apply_requests();
        break;

    default:
        break;
    }
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

#if PWRLVL_POWER_LIMIT_ENABLED
static uint32_t pwrlvl_get_limit_mw(void)
{
    switch (pwrlvl_mode)
    {
    case PWRLVL_MODE_50_PERCENT:
        return PWRLVL_LIMIT_50_PERCENT_MW;

    case PWRLVL_MODE_75_PERCENT:
        return PWRLVL_LIMIT_75_PERCENT_MW;

    case PWRLVL_MODE_100_PERCENT:
    default:
        return PWRLVL_LIMIT_100_PERCENT_MW;
    }
}

#endif

#if PWRLVL_POWER_RISE_ENABLED
void pwrlvl_observe_power(uint32_t current_mw, uint32_t previous_peak_mw, bool segment_started, bool reference_valid)
{
    if (!pwrlvl_initialized || pwrlvl_forced_minimum || (pwrlvl_mode != PWRLVL_MODE_100_PERCENT))
    {
        return;
    }
    if (segment_started)
    {
        pwrlvl_rise_peak_mw = 0;
    }
    if (current_mw <= pwrlvl_rise_peak_mw)
    {
        return; // Repeated/falling readings cannot undo the independent decay.
    }
    pwrlvl_rise_peak_mw = current_mw;
    if (!reference_valid || (current_mw <= previous_peak_mw))
    {
        return;
    }
    uint32_t rise = current_mw - previous_peak_mw;
    // Clamp first, avoiding overflow and making 20 W exactly full duty (32).
    uint8_t target = rise >= PWRLVL_POWER_RISE_FULL_SCALE_MW
                         ? PWRLVL_PWM_FULL_DUTY
                         : (uint8_t)((rise * PWRLVL_PWM_FULL_DUTY) / PWRLVL_POWER_RISE_FULL_SCALE_MW);
    if (target > pwrlvl_rise_pending)
    {
        pwrlvl_rise_pending = target;
    }
}

static void pwrlvl_clear_power_rise(void)
{
    pwrlvl_rise_value = pwrlvl_rise_pending = 0;
    pwrlvl_rise_peak_mw                     = 0;
    pwrlvl_rise_last_decay_ms               = systick_get_ms();
}
#endif

static void pwrlvl_apply_requests(void)
{
    if (!pwrlvl_initialized || pwrlvl_forced_minimum)
    {
        return;
    }
    uint8_t value = pwrlvl_regulator_value;
#if PWRLVL_POWER_RISE_ENABLED
    if (pwrlvl_rise_value > value)
    {
        value = pwrlvl_rise_value;
    }
#endif
    if (value != pwrlvl_pwm_value)
    {
        pwrlvl_set_pwm(value);
    }
}

static void pwrlvl_set_pwm(uint8_t value)
{
    if (value > PWRLVL_PWM_FULL_DUTY)
    {
        value = PWRLVL_PWM_FULL_DUTY;
    }

    FT_SET(pwm, value);
    pwrlvl_pwm_value = value;
#if !HOT_WAND_TEST_HARNESS
    TIM3->CCR1 = value;
#endif
}

bool pwrlvl_is_current_limiting(void)
{
    return pwrlvl_current_limiting;
}
