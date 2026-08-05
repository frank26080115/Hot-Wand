/*
Tip Detection
This code module is responsible for detecting the presence or absence of a tip connected to the device.
The system must shutdown RF power if the tip is removed, as the missing load will cause very high voltages to appear at the MOSFET
This code module uses a timer to debounce the tip detection signal, shutdown RF power when the tip is confirmed to be removed, and it latches the fault until the user resets it.
*/

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "tipdetect.h"

#include "pins.h"
#include "rfgen.h"
#include "stm32f0xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define TIPDETECT_COUNTS_PER_PERIOD 65536
#define TIPDETECT_HSE_10KHZ         (HSE_VALUE / 10000)
#define TIPDETECT_TIMER_CYCLES      (((TIPDETECT_HSE_10KHZ * TIPDETECT_DEBOUNCE_US) + 99) / 100)
#define TIPDETECT_TIMER_DIVIDER                                                                                        \
    ((TIPDETECT_TIMER_CYCLES + TIPDETECT_COUNTS_PER_PERIOD - 1) / TIPDETECT_COUNTS_PER_PERIOD)
#define TIPDETECT_TIMER_TICKS     ((TIPDETECT_TIMER_CYCLES + TIPDETECT_TIMER_DIVIDER - 1) / TIPDETECT_TIMER_DIVIDER)
#define TIPDETECT_TIMER_PRESCALER (TIPDETECT_TIMER_DIVIDER - 1)
#define TIPDETECT_TIMER_PERIOD    (TIPDETECT_TIMER_TICKS - 1)

#define TIPDETECT_TIMER_IRQ_PRIORITY 0
#define TIPDETECT_EXTI_IRQ_PRIORITY  3
#define TIPDETECT_EXTI4_15_MASK      0xFFF0

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static TIM_HandleTypeDef tipdetect_timer;
static volatile bool     tipdetect_initialized;
static volatile bool     tipdetect_tip_present;
static volatile bool     tipdetect_triggered = true;

_Static_assert((HSE_VALUE % 10000) == 0, "HSE frequency must be an exact multiple of 10 kHz");
_Static_assert((TIPDETECT_TIMER_DIVIDER >= 1) && (TIPDETECT_TIMER_DIVIDER <= TIPDETECT_COUNTS_PER_PERIOD),
               "tip-detect timer prescaler is out of range");
_Static_assert((TIPDETECT_TIMER_TICKS >= 1) && (TIPDETECT_TIMER_TICKS <= TIPDETECT_COUNTS_PER_PERIOD),
               "tip-detect timer period is out of range");

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void tipdetect_arm_timer(void);
static void tipdetect_fail_closed(void);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void tipdetect_init(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};

    if (tipdetect_initialized)
    {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM17_CLK_ENABLE();
    __HAL_RCC_TIM17_FORCE_RESET();
    __HAL_RCC_TIM17_RELEASE_RESET();

    tipdetect_timer                        = (TIM_HandleTypeDef){0};
    tipdetect_timer.Instance               = TIM17;
    tipdetect_timer.Init.Prescaler         = TIPDETECT_TIMER_PRESCALER;
    tipdetect_timer.Init.CounterMode       = TIM_COUNTERMODE_UP;
    tipdetect_timer.Init.Period            = TIPDETECT_TIMER_PERIOD;
    tipdetect_timer.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    tipdetect_timer.Init.RepetitionCounter = 0;
    tipdetect_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&tipdetect_timer) != HAL_OK)
    {
        tipdetect_fail_closed();
        return;
    }

    if (HAL_TIM_OnePulse_Init(&tipdetect_timer, TIM_OPMODE_SINGLE) != HAL_OK)
    {
        tipdetect_fail_closed();
        return;
    }

    __HAL_TIM_DISABLE(&tipdetect_timer);
    __HAL_TIM_DISABLE_IT(&tipdetect_timer, TIM_IT_UPDATE);
    __HAL_TIM_CLEAR_FLAG(&tipdetect_timer, TIM_FLAG_UPDATE);

    /*
     * The detector has an external pull-up.  A high level means that the tip
     * is present; a low level
     * means that it has disconnected.
     */
    gpio_cfg.Pin   = TIP_DET_PINn;
    gpio_cfg.Mode  = GPIO_MODE_IT_RISING_FALLING;
    gpio_cfg.Pull  = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TIP_DET_GPIOx, &gpio_cfg);

    __HAL_GPIO_EXTI_CLEAR_IT(TIP_DET_PINn);
    HAL_NVIC_ClearPendingIRQ(TIM17_IRQn);

    tipdetect_tip_present = (HAL_GPIO_ReadPin(TIP_DET_GPIOx, TIP_DET_PINn) == GPIO_PIN_SET);
    tipdetect_triggered   = !tipdetect_tip_present;
    tipdetect_initialized = true;

    HAL_NVIC_SetPriority(TIM17_IRQn, TIPDETECT_TIMER_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(TIM17_IRQn);
    HAL_NVIC_SetPriority(EXTI4_15_IRQn, TIPDETECT_EXTI_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

    if (tipdetect_triggered)
    {
        rfgen_stop();
    }
}

void tipdetect_task(void)
{
    /*
     * Edge qualification and fault latching are interrupt-driven. This hook
     * is retained so tip detection
     * fits the main-loop task interface. The
     * explicit latched-reset API replaces the legacy automatic RF
     * restart.
     */
}

void tipdetect_reset(void)
{
    uint32_t interrupt_state;

    if (!tipdetect_initialized)
    {
        return;
    }

    interrupt_state = __get_PRIMASK();
    __disable_irq();

    /*
     * Never clear the latch while the last debounced state or the current
     * electrical state says that the
     * tip is absent, or while an edge is still
     * being qualified by TIM17.
     */
    if (((TIM17->DIER & TIM_DIER_UIE) == 0) && tipdetect_tip_present &&
        (HAL_GPIO_ReadPin(TIP_DET_GPIOx, TIP_DET_PINn) == GPIO_PIN_SET))
    {
        tipdetect_triggered = false;
    }

    if (interrupt_state == 0)
    {
        __enable_irq();
    }
}

void EXTI4_15_IRQHandler_Impl(void)
{
    uint32_t pending;
    uint32_t pin;

    pending = EXTI->PR & EXTI->IMR & TIPDETECT_EXTI4_15_MASK;

    if ((pending & TIP_DET_PINn) != 0)
    {
        CLEAR_BIT(EXTI->IMR, TIP_DET_PINn);
        __HAL_GPIO_EXTI_CLEAR_IT(TIP_DET_PINn);
        pending &= ~TIP_DET_PINn;

        if (!tipdetect_initialized)
        {
            tipdetect_fail_closed();
        }
        else
        {
            tipdetect_arm_timer();
        }
    }

    /*
     * EXTI4_15 is shared.  Route any other enabled line through the standard
     * HAL callback so a future
     * button or peripheral cannot cause an
     * unhandled-interrupt storm.
     */
    for (pin = GPIO_PIN_4; pin <= GPIO_PIN_15; pin <<= 1)
    {
        if ((pending & pin) != 0)
        {
            HAL_GPIO_EXTI_IRQHandler((uint16_t)pin);
        }
    }
}

void TIM17_IRQHandler_Impl(void)
{
    bool tip_present;

    if (((TIM17->SR & TIM_SR_UIF) == 0) || ((TIM17->DIER & TIM_DIER_UIE) == 0))
    {
        return;
    }

    CLEAR_BIT(TIM17->DIER, TIM_DIER_UIE);
    CLEAR_BIT(TIM17->CR1, TIM_CR1_CEN);
    CLEAR_BIT(TIM17->SR, TIM_SR_UIF);

    /*
     * Discard edges accumulated during the debounce window, then sample.
     * Any edge racing with or
     * following the sample remains pending and starts
     * another complete debounce interval when EXTI is unmasked.

     */
    __HAL_GPIO_EXTI_CLEAR_IT(TIP_DET_PINn);
    tip_present           = (HAL_GPIO_ReadPin(TIP_DET_GPIOx, TIP_DET_PINn) == GPIO_PIN_SET);
    tipdetect_tip_present = tip_present;

    if (!tip_present)
    {
        tipdetect_triggered = true;
        rfgen_stop();
    }

    SET_BIT(EXTI->IMR, TIP_DET_PINn);
}

// -----------------------------------------------------------------------------
// Getters and Setters
// -----------------------------------------------------------------------------

bool tipdetect_has_triggered(void)
{
    return tipdetect_triggered;
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static void tipdetect_arm_timer(void)
{
    CLEAR_BIT(TIM17->CR1, TIM_CR1_CEN);
    CLEAR_BIT(TIM17->DIER, TIM_DIER_UIE);
    TIM17->PSC = TIPDETECT_TIMER_PRESCALER;
    TIM17->ARR = TIPDETECT_TIMER_PERIOD;
    TIM17->CNT = 0;
    TIM17->EGR = TIM_EGR_UG;
    CLEAR_BIT(TIM17->SR, TIM_SR_UIF);
    HAL_NVIC_ClearPendingIRQ(TIM17_IRQn);
    SET_BIT(TIM17->DIER, TIM_DIER_UIE);
    SET_BIT(TIM17->CR1, TIM_CR1_CEN);
}

static void tipdetect_fail_closed(void)
{
    tipdetect_initialized = false;
    tipdetect_tip_present = false;
    tipdetect_triggered   = true;

    CLEAR_BIT(EXTI->IMR, TIP_DET_PINn);
    if (__HAL_RCC_TIM17_IS_CLK_ENABLED())
    {
        CLEAR_BIT(TIM17->DIER, TIM_DIER_UIE);
        CLEAR_BIT(TIM17->CR1, TIM_CR1_CEN);
        CLEAR_BIT(TIM17->SR, TIM_SR_UIF);
    }

    rfgen_stop();
}
