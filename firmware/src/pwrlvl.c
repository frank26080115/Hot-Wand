#include "pwrlvl.h"

#include "adc.h"
#include "conf.h"
#include "fault.h"
#include "pins.h"
#include "stm32f0xx_hal.h"
#include "systick.h"

#include <stdbool.h>
#include <stdint.h>

#define PWRLVL_LIMIT_100_PERCENT_MW 100000UL
#define PWRLVL_LIMIT_75_PERCENT_MW 75000UL
#define PWRLVL_LIMIT_50_PERCENT_MW 50000UL

#define PWRLVL_PWM_MAX 31U
#define PWRLVL_PWM_FULL_DUTY (PWRLVL_PWM_MAX + 1U)
#define PWRLVL_PWM_JUMP_LEVEL 16U
#define PWRLVL_RAMP_UP_PERIOD_MS 2U

#ifndef PWRLVL_CURRENT_LIMIT_ENABLED
#define PWRLVL_CURRENT_LIMIT_ENABLED 1
#endif

#if (PWRLVL_CURRENT_LIMIT_ENABLED != 0) && (PWRLVL_CURRENT_LIMIT_ENABLED != 1)
#error "PWRLVL_CURRENT_LIMIT_ENABLED must be 0 or 1"
#endif

#if PWRLVL_CURRENT_LIMIT_ENABLED
#define PWRLVL_CURRENT_LIMIT_MA 5200U
#endif

static pwrlvl_mode_t pwrlvl_mode = PWRLVL_MODE_100_PERCENT;
static uint32_t      pwrlvl_last_update_ms;
static uint32_t      pwrlvl_previous_power_mw;
static uint32_t      pwrlvl_short_circuit_started_ms;
#if PWRLVL_CURRENT_LIMIT_ENABLED
static uint16_t pwrlvl_previous_current_ma;
#endif
static uint8_t pwrlvl_pwm_value;
static bool    pwrlvl_initialized;
static bool    pwrlvl_forced_minimum;
static bool    pwrlvl_current_limiting;
static bool    pwrlvl_short_circuit_timing;

static uint32_t pwrlvl_get_limit_mw(void);
static void     pwrlvl_set_pwm(uint8_t value);

void pwrlvl_init(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};
    uint8_t          initial_pwm_value;

    if (pwrlvl_initialized)
    {
        return;
    }

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
    initial_pwm_value = pwrlvl_forced_minimum ? PWRLVL_PWM_FULL_DUTY : 0U;
    HAL_GPIO_WritePin(PWR_ATTENU_GPIOx, PWR_ATTENU_PINn, pwrlvl_forced_minimum ? GPIO_PIN_SET : GPIO_PIN_RESET);

    gpio_cfg.Pin   = PWR_ATTENU_PINn;
    gpio_cfg.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_cfg.Pull  = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(PWR_ATTENU_GPIOx, &gpio_cfg);

    TIM3->PSC   = 0U;
    TIM3->ARR   = PWRLVL_PWM_MAX;
    TIM3->CCR1  = initial_pwm_value;
    TIM3->CCMR1 = TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1PE;
    TIM3->EGR   = TIM_EGR_UG;
    TIM3->SR    = 0U;
    TIM3->CCER  = TIM_CCER_CC1E;
    TIM3->CR1   = TIM_CR1_CEN;

    gpio_cfg.Mode      = GPIO_MODE_AF_PP;
    gpio_cfg.Alternate = GPIO_AF1_TIM3;
    HAL_GPIO_Init(PWR_ATTENU_GPIOx, &gpio_cfg);

    pwrlvl_pwm_value                = initial_pwm_value;
    pwrlvl_previous_power_mw        = 0U;
    pwrlvl_short_circuit_started_ms = 0U;
    pwrlvl_short_circuit_timing     = false;
#if PWRLVL_CURRENT_LIMIT_ENABLED
    pwrlvl_previous_current_ma = 0U;
#endif
    pwrlvl_last_update_ms = systick_get_ms();
    pwrlvl_initialized    = true;
}

void pwrlvl_task(void)
{
    uint32_t current_power_mw;
    uint32_t limit_mw;
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

    /* This is a continuous-duration test: any sample at or below 5.8 A
     * cancels the pending fault interval.  Keep it independent of the normal
     * current limiter so disabling that feature cannot disable protection. */
    if (current_ma > PWRLVL_SHORT_CIRCUIT_CURRENT_MA)
    {
        if (!pwrlvl_short_circuit_timing)
        {
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

    if (pwrlvl_forced_minimum)
    {
        pwrlvl_current_limiting = false;
        return;
    }

    /*
     * Sample on every task call. Keeping the previous sample in the decision
     * prevents a single over-limit reading from being discarded immediately.
     */
    current_power_mw         = adc_get_milliwatts();
    limit_mw                 = pwrlvl_get_limit_mw();
    ramp_up                  = (current_power_mw > limit_mw) || (pwrlvl_previous_power_mw > limit_mw);
    pwrlvl_previous_power_mw = current_power_mw;

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
    if ((uint32_t)(now - pwrlvl_last_update_ms) < update_period_ms)
    {
        return;
    }
    pwrlvl_last_update_ms = now;

    next_pwm = pwrlvl_pwm_value;

    if (ramp_up)
    {
        if (next_pwm < PWRLVL_PWM_JUMP_LEVEL)
        {
            next_pwm = PWRLVL_PWM_JUMP_LEVEL;
        }
        else if (next_pwm < PWRLVL_PWM_MAX)
        {
            ++next_pwm;
        }
    }
    else if (next_pwm > 0U)
    {
        --next_pwm;
    }

    if (next_pwm != pwrlvl_pwm_value)
    {
        pwrlvl_set_pwm(next_pwm);
    }
}

void pwrlvl_set_mode(pwrlvl_mode_t mode)
{
    switch (mode)
    {
    case PWRLVL_MODE_100_PERCENT:
    case PWRLVL_MODE_75_PERCENT:
    case PWRLVL_MODE_50_PERCENT:
        pwrlvl_mode = mode;
        break;

    default:
        break;
    }
}

bool pwrlvl_is_current_limiting(void)
{
    return pwrlvl_current_limiting;
}

void pwrlvl_force_minimum(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};

    /*
     * A static high is electrically identical to 100 percent PWM and avoids
     * briefly requesting maximum buck output merely to initialize TIM3.  If
     * PWM was already running, first move its compare above ARR so both sides
     * of the alternate-function-to-GPIO handoff are high.
     */
    pwrlvl_forced_minimum   = true;
    pwrlvl_current_limiting = false;

    if (pwrlvl_initialized)
    {
        TIM3->CCR1 = PWRLVL_PWM_FULL_DUTY;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(PWR_ATTENU_GPIOx, PWR_ATTENU_PINn, GPIO_PIN_SET);

    gpio_cfg.Pin   = PWR_ATTENU_PINn;
    gpio_cfg.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_cfg.Pull  = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(PWR_ATTENU_GPIOx, &gpio_cfg);

    pwrlvl_pwm_value = PWRLVL_PWM_FULL_DUTY;
}

void pwrlvl_release_minimum(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};

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
     * the power limit selected before the forced-minimum request. */
    TIM3->CCR1 = PWRLVL_PWM_MAX;

    gpio_cfg.Pin       = PWR_ATTENU_PINn;
    gpio_cfg.Mode      = GPIO_MODE_AF_PP;
    gpio_cfg.Pull      = GPIO_NOPULL;
    gpio_cfg.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_cfg.Alternate = GPIO_AF1_TIM3;
    HAL_GPIO_Init(PWR_ATTENU_GPIOx, &gpio_cfg);

    pwrlvl_pwm_value      = PWRLVL_PWM_MAX;
    pwrlvl_last_update_ms = systick_get_ms();
    pwrlvl_forced_minimum = false;
}

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

static void pwrlvl_set_pwm(uint8_t value)
{
    if (value > PWRLVL_PWM_MAX)
    {
        value = PWRLVL_PWM_MAX;
    }

    pwrlvl_pwm_value = value;
    TIM3->CCR1       = value;
}
