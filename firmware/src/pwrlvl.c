#include "pwrlvl.h"

#include "adc.h"
#include "pins.h"
#include "stm32f0xx_hal.h"
#include "systick.h"

#include <stdbool.h>
#include <stdint.h>

#define PWRLVL_LIMIT_100_PERCENT_MW 100000UL
#define PWRLVL_LIMIT_75_PERCENT_MW   75000UL
#define PWRLVL_LIMIT_50_PERCENT_MW   50000UL

#define PWRLVL_PWM_MAX               31U
#define PWRLVL_PWM_JUMP_LEVEL        16U
#define PWRLVL_RAMP_UP_PERIOD_MS      2U

#ifndef PWRLVL_CURRENT_LIMIT_ENABLED
#define PWRLVL_CURRENT_LIMIT_ENABLED  1
#endif

#if (PWRLVL_CURRENT_LIMIT_ENABLED != 0) && \
    (PWRLVL_CURRENT_LIMIT_ENABLED != 1)
#error "PWRLVL_CURRENT_LIMIT_ENABLED must be 0 or 1"
#endif

#if PWRLVL_CURRENT_LIMIT_ENABLED
#define PWRLVL_CURRENT_LIMIT_MA       5200U
#endif

static TIM_HandleTypeDef pwrlvl_timer;
static pwrlvl_mode_t pwrlvl_mode = PWRLVL_MODE_100_PERCENT;
static uint32_t pwrlvl_last_update_ms;
static uint32_t pwrlvl_previous_power_mw;
#if PWRLVL_CURRENT_LIMIT_ENABLED
static uint16_t pwrlvl_previous_current_ma;
#endif
static uint8_t pwrlvl_pwm_value;
static bool pwrlvl_initialized;

static uint32_t pwrlvl_get_limit_mw(void);
static void pwrlvl_set_pwm(uint8_t value);
static void pwrlvl_fault(void);

void pwrlvl_init(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};
    TIM_OC_InitTypeDef pwm_cfg = {0};

    if (pwrlvl_initialized) {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    /*
     * Hold the attenuation signal low until TIM3 is configured and running
     * with a compare value of zero.
     */
    HAL_GPIO_WritePin(PWR_ATTENU_GPIOx, PWR_ATTENU_PINn, GPIO_PIN_RESET);

    gpio_cfg.Pin = PWR_ATTENU_PINn;
    gpio_cfg.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_cfg.Pull = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(PWR_ATTENU_GPIOx, &gpio_cfg);

    pwrlvl_timer.Instance = TIM3;
    pwrlvl_timer.Init.Prescaler = 0U;
    pwrlvl_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    pwrlvl_timer.Init.Period = PWRLVL_PWM_MAX;
    pwrlvl_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    pwrlvl_timer.Init.RepetitionCounter = 0U;
    pwrlvl_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_PWM_Init(&pwrlvl_timer) != HAL_OK) {
        pwrlvl_fault();
    }

    pwm_cfg.OCMode = TIM_OCMODE_PWM1;
    pwm_cfg.Pulse = 0U;
    pwm_cfg.OCPolarity = TIM_OCPOLARITY_HIGH;
    pwm_cfg.OCFastMode = TIM_OCFAST_DISABLE;

    if (HAL_TIM_PWM_ConfigChannel(&pwrlvl_timer,
                                  &pwm_cfg,
                                  TIM_CHANNEL_1) != HAL_OK) {
        pwrlvl_fault();
    }

    if (HAL_TIM_PWM_Start(&pwrlvl_timer, TIM_CHANNEL_1) != HAL_OK) {
        pwrlvl_fault();
    }

    gpio_cfg.Mode = GPIO_MODE_AF_PP;
    gpio_cfg.Alternate = GPIO_AF1_TIM3;
    HAL_GPIO_Init(PWR_ATTENU_GPIOx, &gpio_cfg);

    pwrlvl_pwm_value = 0U;
    pwrlvl_previous_power_mw = 0U;
#if PWRLVL_CURRENT_LIMIT_ENABLED
    pwrlvl_previous_current_ma = 0U;
#endif
    pwrlvl_last_update_ms = systick_get_ms();
    pwrlvl_initialized = true;
}

void pwrlvl_task(void)
{
    uint32_t current_power_mw;
    uint32_t limit_mw;
    uint32_t now;
    uint32_t update_period_ms;
    uint8_t next_pwm;
    bool ramp_up;
#if PWRLVL_CURRENT_LIMIT_ENABLED
    uint16_t current_ma;
#endif

    if (!pwrlvl_initialized) {
        return;
    }

    /*
     * Sample on every task call. Keeping the previous sample in the decision
     * prevents a single over-limit reading from being discarded immediately.
     */
    current_power_mw = adc_get_milliwatts();
    limit_mw = pwrlvl_get_limit_mw();
    ramp_up = (current_power_mw > limit_mw) ||
              (pwrlvl_previous_power_mw > limit_mw);
    pwrlvl_previous_power_mw = current_power_mw;

#if PWRLVL_CURRENT_LIMIT_ENABLED
    /*
     * The current ceiling is independent of the selected power mode.  Keep
     * one previous reading in the decision, matching the power-limit response
     * and ensuring an over-current sample gets at least one fast ramp step.
     */
    current_ma = adc_to_milliamps(CURR_SENS_IDX);
    ramp_up = ramp_up ||
              (current_ma >= PWRLVL_CURRENT_LIMIT_MA) ||
              (pwrlvl_previous_current_ma >= PWRLVL_CURRENT_LIMIT_MA);
    pwrlvl_previous_current_ma = current_ma;
#endif

    update_period_ms = ramp_up ? PWRLVL_RAMP_UP_PERIOD_MS
                               : PWRLVL_UPDATE_PERIOD_MS;
    now = systick_get_ms();
    if ((uint32_t)(now - pwrlvl_last_update_ms) <
        update_period_ms) {
        return;
    }
    pwrlvl_last_update_ms = now;

    next_pwm = pwrlvl_pwm_value;

    if (ramp_up) {
        if (next_pwm < PWRLVL_PWM_JUMP_LEVEL) {
            next_pwm = PWRLVL_PWM_JUMP_LEVEL;
        } else if (next_pwm < PWRLVL_PWM_MAX) {
            ++next_pwm;
        }
    } else if (next_pwm > 0U) {
        --next_pwm;
    }

    if (next_pwm != pwrlvl_pwm_value) {
        pwrlvl_set_pwm(next_pwm);
    }
}

void pwrlvl_set_mode(pwrlvl_mode_t mode)
{
    switch (mode) {
    case PWRLVL_MODE_100_PERCENT:
    case PWRLVL_MODE_75_PERCENT:
    case PWRLVL_MODE_50_PERCENT:
        pwrlvl_mode = mode;
        break;

    default:
        break;
    }
}

static uint32_t pwrlvl_get_limit_mw(void)
{
    switch (pwrlvl_mode) {
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
    if (value > PWRLVL_PWM_MAX) {
        value = PWRLVL_PWM_MAX;
    }

    pwrlvl_pwm_value = value;
    __HAL_TIM_SET_COMPARE(&pwrlvl_timer, TIM_CHANNEL_1, value);
}

static void pwrlvl_fault(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};

    /*
     * A high attenuation signal lowers the buck output, making this the
     * safer static state if PWM setup fails.
     */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(PWR_ATTENU_GPIOx, PWR_ATTENU_PINn, GPIO_PIN_SET);

    gpio_cfg.Pin = PWR_ATTENU_PINn;
    gpio_cfg.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_cfg.Pull = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(PWR_ATTENU_GPIOx, &gpio_cfg);

    for (;;) {
    }
}
