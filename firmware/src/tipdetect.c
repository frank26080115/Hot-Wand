#include "tipdetect.h"

#include "pins.h"
#include "rfgen.h"
#include "stm32f0xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#define TIPDETECT_COUNTS_PER_PERIOD 65536ULL
#define TIPDETECT_US_PER_SECOND   1000000ULL

#define TIPDETECT_TIMER_IRQ_PRIORITY 0U
#define TIPDETECT_EXTI_IRQ_PRIORITY  3U
#define TIPDETECT_EXTI4_15_MASK       0xFFF0U

static TIM_HandleTypeDef tipdetect_timer;
static volatile bool tipdetect_initialized;
static volatile bool tipdetect_tip_present;
static volatile bool tipdetect_triggered = true;

static bool tipdetect_get_timer_config(uint32_t *prescaler,
                                       uint32_t *period);
static bool tipdetect_arm_timer(void);
static void tipdetect_fail_closed(void);

void tipdetect_init(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};
    uint32_t period;
    uint32_t prescaler;

    if (tipdetect_initialized) {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM17_CLK_ENABLE();
    __HAL_RCC_TIM17_FORCE_RESET();
    __HAL_RCC_TIM17_RELEASE_RESET();

    if (!tipdetect_get_timer_config(&prescaler, &period)) {
        tipdetect_fail_closed();
        return;
    }

    tipdetect_timer = (TIM_HandleTypeDef){0};
    tipdetect_timer.Instance = TIM17;
    tipdetect_timer.Init.Prescaler = prescaler;
    tipdetect_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    tipdetect_timer.Init.Period = period;
    tipdetect_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    tipdetect_timer.Init.RepetitionCounter = 0U;
    tipdetect_timer.Init.AutoReloadPreload =
        TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&tipdetect_timer) != HAL_OK) {
        tipdetect_fail_closed();
        return;
    }

    if (HAL_TIM_OnePulse_Init(&tipdetect_timer,
                              TIM_OPMODE_SINGLE) != HAL_OK) {
        tipdetect_fail_closed();
        return;
    }

    __HAL_TIM_DISABLE(&tipdetect_timer);
    __HAL_TIM_DISABLE_IT(&tipdetect_timer, TIM_IT_UPDATE);
    __HAL_TIM_CLEAR_FLAG(&tipdetect_timer, TIM_FLAG_UPDATE);

    /*
     * The detector has an external pull-up.  A high level means that the tip
     * is present; a low level means that it has disconnected.
     */
    gpio_cfg.Pin = TIP_DET_PINn;
    gpio_cfg.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio_cfg.Pull = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TIP_DET_GPIOx, &gpio_cfg);

    __HAL_GPIO_EXTI_CLEAR_IT(TIP_DET_PINn);
    HAL_NVIC_ClearPendingIRQ(TIM17_IRQn);

    tipdetect_tip_present =
        (HAL_GPIO_ReadPin(TIP_DET_GPIOx, TIP_DET_PINn) == GPIO_PIN_SET);
    tipdetect_triggered = !tipdetect_tip_present;
    tipdetect_initialized = true;

    HAL_NVIC_SetPriority(TIM17_IRQn,
                         TIPDETECT_TIMER_IRQ_PRIORITY,
                         0U);
    HAL_NVIC_EnableIRQ(TIM17_IRQn);
    HAL_NVIC_SetPriority(EXTI4_15_IRQn,
                         TIPDETECT_EXTI_IRQ_PRIORITY,
                         0U);
    HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

    if (tipdetect_triggered) {
        rfgen_stop();
    }
}

void tipdetect_task(void)
{
    /*
     * Edge qualification and fault latching are interrupt-driven.  This hook
     * is retained so tip detection fits the main-loop task interface.  The
     * legacy 100 ms delay controlled automatic RF restart; the explicit
     * latched reset API replaces that behavior here.
     */
}

bool tipdetect_has_triggered(void)
{
    return tipdetect_triggered;
}

void tipdetect_reset(void)
{
    uint32_t interrupt_state;

    if (!tipdetect_initialized) {
        return;
    }

    interrupt_state = __get_PRIMASK();
    __disable_irq();

    /*
     * Never clear the latch while the last debounced state or the current
     * electrical state says that the tip is absent, or while an edge is still
     * being qualified by TIM17.
     */
    if (((TIM17->DIER & TIM_DIER_UIE) == 0U) &&
        tipdetect_tip_present &&
        (HAL_GPIO_ReadPin(TIP_DET_GPIOx,
                          TIP_DET_PINn) == GPIO_PIN_SET)) {
        tipdetect_triggered = false;
    }

    if (interrupt_state == 0U) {
        __enable_irq();
    }
}

void EXTI4_15_IRQHandler(void)
{
    uint32_t pending;
    uint32_t pin;

    pending = EXTI->PR & EXTI->IMR & TIPDETECT_EXTI4_15_MASK;

    if ((pending & TIP_DET_PINn) != 0U) {
        CLEAR_BIT(EXTI->IMR, TIP_DET_PINn);
        __HAL_GPIO_EXTI_CLEAR_IT(TIP_DET_PINn);
        pending &= ~TIP_DET_PINn;

        if (!tipdetect_initialized || !tipdetect_arm_timer()) {
            tipdetect_fail_closed();
        }
    }

    /*
     * EXTI4_15 is shared.  Route any other enabled line through the standard
     * HAL callback so a future button or peripheral cannot cause an
     * unhandled-interrupt storm.
     */
    for (pin = GPIO_PIN_4; pin <= GPIO_PIN_15; pin <<= 1U) {
        if ((pending & pin) != 0U) {
            HAL_GPIO_EXTI_IRQHandler((uint16_t)pin);
        }
    }
}

void TIM17_IRQHandler(void)
{
    bool tip_present;

    if (((TIM17->SR & TIM_SR_UIF) == 0U) ||
        ((TIM17->DIER & TIM_DIER_UIE) == 0U)) {
        return;
    }

    CLEAR_BIT(TIM17->DIER, TIM_DIER_UIE);
    CLEAR_BIT(TIM17->CR1, TIM_CR1_CEN);
    CLEAR_BIT(TIM17->SR, TIM_SR_UIF);

    /*
     * Discard edges accumulated during the debounce window, then sample.
     * Any edge racing with or following the sample remains pending and starts
     * another complete debounce interval when EXTI is unmasked.
     */
    __HAL_GPIO_EXTI_CLEAR_IT(TIP_DET_PINn);
    tip_present =
        (HAL_GPIO_ReadPin(TIP_DET_GPIOx, TIP_DET_PINn) == GPIO_PIN_SET);
    tipdetect_tip_present = tip_present;

    if (!tip_present) {
        tipdetect_triggered = true;
        rfgen_stop();
    }

    SET_BIT(EXTI->IMR, TIP_DET_PINn);
}

static bool tipdetect_get_timer_config(uint32_t *prescaler,
                                       uint32_t *period)
{
    uint64_t timer_cycles;
    uint64_t timer_divider;
    uint64_t timer_ticks;
    uint32_t timer_clock_hz;

    if ((prescaler == NULL) || (period == NULL)) {
        return false;
    }

    timer_clock_hz = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE) != RCC_CFGR_PPRE_DIV1) {
        timer_clock_hz *= 2U;
    }

    if (timer_clock_hz == 0U) {
        return false;
    }

    timer_cycles =
        (((uint64_t)timer_clock_hz * TIPDETECT_DEBOUNCE_US) +
         (TIPDETECT_US_PER_SECOND - 1ULL)) /
        TIPDETECT_US_PER_SECOND;
    if (timer_cycles == 0ULL) {
        timer_cycles = 1ULL;
    }

    timer_divider =
        (timer_cycles + TIPDETECT_COUNTS_PER_PERIOD - 1ULL) /
        TIPDETECT_COUNTS_PER_PERIOD;
    if ((timer_divider == 0ULL) ||
        (timer_divider > TIPDETECT_COUNTS_PER_PERIOD)) {
        return false;
    }

    timer_ticks =
        (timer_cycles + timer_divider - 1ULL) / timer_divider;
    if ((timer_ticks == 0ULL) ||
        (timer_ticks > TIPDETECT_COUNTS_PER_PERIOD)) {
        return false;
    }

    *prescaler = (uint32_t)(timer_divider - 1ULL);
    *period = (uint32_t)(timer_ticks - 1ULL);
    return true;
}

static bool tipdetect_arm_timer(void)
{
    uint32_t period;
    uint32_t prescaler;

    /*
     * Recalculate on every edge because rfgen_start() can change the APB timer
     * clock from the reset-default 8 MHz to the 27.12 MHz crystal.
     */
    if (!tipdetect_get_timer_config(&prescaler, &period)) {
        return false;
    }

    CLEAR_BIT(TIM17->CR1, TIM_CR1_CEN);
    CLEAR_BIT(TIM17->DIER, TIM_DIER_UIE);
    TIM17->PSC = prescaler;
    TIM17->ARR = period;
    TIM17->CNT = 0U;
    TIM17->EGR = TIM_EGR_UG;
    CLEAR_BIT(TIM17->SR, TIM_SR_UIF);
    HAL_NVIC_ClearPendingIRQ(TIM17_IRQn);
    SET_BIT(TIM17->DIER, TIM_DIER_UIE);
    SET_BIT(TIM17->CR1, TIM_CR1_CEN);

    return true;
}

static void tipdetect_fail_closed(void)
{
    tipdetect_initialized = false;
    tipdetect_tip_present = false;
    tipdetect_triggered = true;

    CLEAR_BIT(EXTI->IMR, TIP_DET_PINn);
    if (__HAL_RCC_TIM17_IS_CLK_ENABLED()) {
        CLEAR_BIT(TIM17->DIER, TIM_DIER_UIE);
        CLEAR_BIT(TIM17->CR1, TIM_CR1_CEN);
        CLEAR_BIT(TIM17->SR, TIM_SR_UIF);
    }

    rfgen_stop();
}
