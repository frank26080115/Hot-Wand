/*
 * Fan PWM generator
 *
 * PA13 cannot expose an ordinary timer channel on the STM32F030 or STM32F042,
 * but its IR_OUT alternate function combines TIM17 channel 1 with the TIM16
 * channel 1 modulation envelope. Holding TIM17 active and running TIM16 in
 * PWM2 mode turns that infrared path into an ordinary approximately 25 kHz
 * output.
 *
 * Production fan control deliberately leaves this module dormant for its
 * configured startup window, preserving SWD access before PA13 is claimed at
 * zero duty. The entire implementation is omitted when FAN_PWM_ENABLED is
 * zero.
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "fan_pwm.h"

#if FAN_PWM_ENABLED

#include "pins.h"
#include "stm32f0xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define FANPWM_TIMER_CLOCK_HZ       27120000
#define FANPWM_TARGET_FREQUENCY_HZ  25000
#define FANPWM_PERIOD_TICKS         1085
#define FANPWM_TIMER_PERIOD         (FANPWM_PERIOD_TICKS - 1)
#define FANPWM_MAX_DUTY_PERCENT     100
#define FANPWM_PERCENT_ROUNDING     (FANPWM_MAX_DUTY_PERCENT / 2)
#define FANPWM_CARRIER_TIMER_PERIOD 1

/*
 * 27.12 MHz / 1085 is approximately 24995.4 Hz. An exact 25 kHz period is not
 * possible with an integer divider from the RF crystal, and this is the
 * nearest available timer period.
 */
_Static_assert(HSE_VALUE == FANPWM_TIMER_CLOCK_HZ, "fan PWM timing requires the 27.12 MHz system clock");
_Static_assert(((FANPWM_TIMER_CLOCK_HZ + (FANPWM_TARGET_FREQUENCY_HZ / 2)) / FANPWM_TARGET_FREQUENCY_HZ) ==
                   FANPWM_PERIOD_TICKS,
               "fan PWM period is no longer the nearest value to 25 kHz");

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static bool          fanpwm_initialized;
static fanpwm_mode_t fanpwm_mode;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void     fanpwm_configure_gpio_alternate(void);
static void     fanpwm_configure_gpio_output(bool output_high);
static void     fanpwm_configure_timers(fanpwm_mode_t mode);
static uint16_t fanpwm_duty_to_compare(fanpwm_mode_t mode, uint8_t duty_percent);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool fanpwm_init(fanpwm_mode_t mode)
{
    bool previous_zero_level_high;

    if ((mode != FANPWM_MODE_DIRECT) && (mode != FANPWM_MODE_EXTERNAL_MOSFET))
    {
        return false;
    }

    if (fanpwm_initialized)
    {
        /*
         * First command zero percent using the old topology. Then hand PA13
         * to a GPIO at that same electrical level before resetting either
         * timer, so reinitialization cannot expose an uncontrolled IR_OUT.
         */
        fanpwm_set(0);
        previous_zero_level_high = fanpwm_mode == FANPWM_MODE_EXTERNAL_MOSFET;
        fanpwm_configure_gpio_output(previous_zero_level_high);
        fanpwm_initialized = false;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_TIM16_CLK_ENABLE();
    __HAL_RCC_TIM17_CLK_ENABLE();

    /*
     * A debugger may have left peripheral-freeze bits set. Keep both halves
     * of IR_OUT running during a halt so PA13 cannot freeze at an arbitrary
     * point in the PWM period. The independent watchdog still resets a halted
     * application and returns PA13 to its reset/SWD state.
     */
    __HAL_RCC_DBGMCU_CLK_ENABLE();
    __HAL_DBGMCU_UNFREEZE_TIM16();
    __HAL_DBGMCU_UNFREEZE_TIM17();

    fanpwm_configure_timers(mode);

    /*
     * Both timer outputs already represent a zero percent fan command before
     * PA13 is taken away from SWD. This prevents an initialization pulse.
     */
    fanpwm_configure_gpio_alternate();

    fanpwm_mode        = mode;
    fanpwm_initialized = true;
    return true;
}

void fanpwm_set(uint8_t duty_percent)
{
    if (!fanpwm_initialized)
    {
        return;
    }

    if (duty_percent > FANPWM_MAX_DUTY_PERCENT)
    {
        duty_percent = FANPWM_MAX_DUTY_PERCENT;
    }

    /*
     * OC1 preload makes this change take effect on the next natural update
     * boundary. Do not force an update here: preserving the current period
     * avoids a shortened output pulse during a duty change.
     */
    TIM16->CCR1 = fanpwm_duty_to_compare(fanpwm_mode, duty_percent);
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static void fanpwm_configure_gpio_alternate(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};

    gpio_cfg.Pin       = FAN_PINn;
    gpio_cfg.Mode      = GPIO_MODE_AF_PP;
    gpio_cfg.Pull      = GPIO_NOPULL;
    gpio_cfg.Speed     = GPIO_SPEED_FREQ_LOW;
    gpio_cfg.Alternate = GPIO_AF1_IR;
    HAL_GPIO_Init(FAN_GPIOx, &gpio_cfg);
}

static void fanpwm_configure_gpio_output(bool output_high)
{
    GPIO_InitTypeDef gpio_cfg = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* Preload the output latch before changing PA13 away from IR_OUT. */
    HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, output_high ? GPIO_PIN_SET : GPIO_PIN_RESET);

    gpio_cfg.Pin   = FAN_PINn;
    gpio_cfg.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_cfg.Pull  = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(FAN_GPIOx, &gpio_cfg);
}

static void fanpwm_configure_timers(fanpwm_mode_t mode)
{
    uint16_t zero_compare = fanpwm_duty_to_compare(mode, 0);

    __HAL_RCC_TIM16_FORCE_RESET();
    __HAL_RCC_TIM17_FORCE_RESET();
    __HAL_RCC_TIM16_RELEASE_RESET();
    __HAL_RCC_TIM17_RELEASE_RESET();

    /*
     * TIM17 is IR_OUT's carrier input. Forced-active OC1 holds that input high
     * continuously, leaving TIM16's inverted envelope as the only waveform.
     * The counter remains enabled because IR_OUT is defined in terms of an
     * active timer channel, even though forced-active mode does not need a
     * meaningful carrier frequency.
     */
    TIM17->CR1   = 0;
    TIM17->PSC   = 0;
    TIM17->ARR   = FANPWM_CARRIER_TIMER_PERIOD;
    TIM17->RCR   = 0;
    TIM17->CCR1  = 0;
    TIM17->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_0;
    TIM17->CCER  = TIM_CCER_CC1E;
    TIM17->BDTR  = TIM_BDTR_MOE;
    TIM17->CNT   = 0;
    TIM17->EGR   = TIM_EGR_UG;
    TIM17->SR    = 0;
    TIM17->CR1   = TIM_CR1_CEN;

    /*
     * IR_OUT is inverted relative to TIM16's modulation envelope. PWM2 makes
     * the resulting PA13 high time grow with CCR1, so direct mode can use the
     * intuitive zero-through-period compare range. OC1 preload provides
     * glitch-free changes on natural period boundaries.
     */
    TIM16->CR1   = TIM_CR1_ARPE;
    TIM16->PSC   = 0;
    TIM16->ARR   = FANPWM_TIMER_PERIOD;
    TIM16->RCR   = 0;
    TIM16->CCR1  = zero_compare;
    TIM16->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_0 | TIM_CCMR1_OC1PE;
    TIM16->CCER  = TIM_CCER_CC1E;
    TIM16->BDTR  = TIM_BDTR_MOE;
    TIM16->CNT   = 0;
    TIM16->EGR   = TIM_EGR_UG;
    TIM16->SR    = 0;
    SET_BIT(TIM16->CR1, TIM_CR1_CEN);
}

static uint16_t fanpwm_duty_to_compare(fanpwm_mode_t mode, uint8_t duty_percent)
{
    uint8_t output_high_percent = duty_percent;

    /*
     * In external-MOSFET mode, high PA13 turns Q8 on and pulls the fan PWM
     * input low. Invert the gate duty so the pulled-up drain still presents
     * the caller's requested high-time percentage to the fan.
     */
    if (mode == FANPWM_MODE_EXTERNAL_MOSFET)
    {
        output_high_percent = (uint8_t)(FANPWM_MAX_DUTY_PERCENT - duty_percent);
    }

    return (uint16_t)(((uint32_t)output_high_percent * FANPWM_PERIOD_TICKS + FANPWM_PERCENT_ROUNDING) /
                      FANPWM_MAX_DUTY_PERCENT);
}

#endif /* FAN_PWM_ENABLED */
