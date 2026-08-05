#include "rfgen.h"

#include "pins.h"
#include "stm32f0xx_hal.h"
#include "tipdetect.h"

#define RFGEN_CLOCK_HZ    27120000UL
#define RFGEN_PWM_PERIOD         1U
#define RFGEN_PWM_PULSE          1U

/*
 * TIM1 runs directly from the 27.12 MHz crystal.  With PSC = 0 and ARR = 1,
 * one PWM period is two timer ticks: 27.12 MHz / 2 = 13.56 MHz.
 */
_Static_assert(HSE_VALUE == RFGEN_CLOCK_HZ,
               "RF generator requires a 27.12 MHz external crystal");

static volatile bool rfgen_fault_logged;

static bool rfgen_tip_allows_start(void);
static void rfgen_pin_low(void);
static void rfgen_fault(void);

bool rfgen_clock_init(void)
{
    RCC_OscInitTypeDef oscillator_cfg = {0};
    RCC_ClkInitTypeDef clock_cfg = {0};

    /* Clock initialization must never make the RF output active. */
    rfgen_stop();

    if (rfgen_has_fault()) {
        return false;
    }

    /* Permit callers such as rfgen_start() to verify an initialized clock. */
    if ((__HAL_RCC_GET_FLAG(RCC_FLAG_HSERDY) != RESET) &&
        (__HAL_RCC_GET_SYSCLK_SOURCE() == RCC_SYSCLKSOURCE_STATUS_HSE)) {
        HAL_RCC_EnableCSS();
        return true;
    }

    oscillator_cfg.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator_cfg.HSEState = RCC_HSE_ON;
    oscillator_cfg.PLL.PLLState = RCC_PLL_NONE;

    if (HAL_RCC_OscConfig(&oscillator_cfg) != HAL_OK) {
        rfgen_fault();
        return false;
    }

    clock_cfg.ClockType = RCC_CLOCKTYPE_SYSCLK |
                          RCC_CLOCKTYPE_HCLK |
                          RCC_CLOCKTYPE_PCLK1;
    clock_cfg.SYSCLKSource = RCC_SYSCLKSOURCE_HSE;
    clock_cfg.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock_cfg.APB1CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clock_cfg, FLASH_LATENCY_1) != HAL_OK) {
        rfgen_fault();
        return false;
    }

    if ((__HAL_RCC_GET_FLAG(RCC_FLAG_HSERDY) == RESET) ||
        (__HAL_RCC_GET_SYSCLK_SOURCE() != RCC_SYSCLKSOURCE_STATUS_HSE)) {
        rfgen_fault();
        return false;
    }

    HAL_RCC_EnableCSS();
    return true;
}

bool rfgen_has_fault(void)
{
    return rfgen_fault_logged;
}

void rfgen_start(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};
    uint32_t interrupt_state;

    if (rfgen_has_fault()) {
        rfgen_stop();
        return;
    }

    /*
     * The tip detector powers up fail-closed.  This also means tipdetect_init()
     * must confirm a connected tip before the RF generator can be started.
     */
    if (!rfgen_tip_allows_start()) {
        rfgen_stop();
        return;
    }

    /*
     * A restart is deliberately a full reconfiguration.  Keep the external
     * circuit disabled while the clock and timer are being rebuilt.
     */
    rfgen_stop();
    if (!rfgen_clock_init()) {
        return;
    }

    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_TIM1_FORCE_RESET();
    __HAL_RCC_TIM1_RELEASE_RESET();

    /*
     * PB1 is TIM1_CH3N on this MCU.  PWM1 makes the complementary output
     * begin low at CNT = 0; CCR = 1 still gives an exact 50 percent duty cycle.
     */
    TIM1->PSC = 0U;
    TIM1->ARR = RFGEN_PWM_PERIOD;
    TIM1->RCR = 0U;
    TIM1->CCR3 = RFGEN_PWM_PULSE;
    TIM1->CCMR2 = TIM_CCMR2_OC3M_1 |
                  TIM_CCMR2_OC3M_2 |
                  TIM_CCMR2_OC3PE;
    TIM1->CCER = 0U;

    /*
     * Internal lockup and SRAM-parity faults are routed to TIM1's break input.
     * Both run and idle off-states hold the RF output inactive (low).
     */
    TIM1->BDTR = TIM_BDTR_OSSR |
                 TIM_BDTR_OSSI |
                 TIM_BDTR_LOCK_0 |
                 TIM_BDTR_LOCK_1 |
                 TIM_BDTR_BKE |
                 TIM_BDTR_BKP;

    __HAL_SYSCFG_BREAK_LOCKUP_LOCK();
    __HAL_SYSCFG_BREAK_SRAMPARITY_LOCK();

    TIM1->CNT = 0U;
    TIM1->EGR = TIM_EGR_UG;
    TIM1->SR = 0U;

    /*
     * Enable CH3N while MOE is clear so TIM1's configured idle state drives
     * low as soon as the pin is changed from GPIO to its alternate function.
     */
    SET_BIT(TIM1->CCER, TIM_CCER_CC3NE);

    gpio_cfg.Pin = RFGEN_PINn;
    gpio_cfg.Mode = GPIO_MODE_AF_PP;
    gpio_cfg.Pull = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_cfg.Alternate = GPIO_AF2_TIM1;
    HAL_GPIO_Init(RFGEN_GPIOx, &gpio_cfg);

    /*
     * Close the race where a tip-disconnect ISR stops TIM1 while this function
     * is still configuring it, after which an interrupted start would
     * otherwise resume and re-enable RF.
     */
    interrupt_state = __get_PRIMASK();
    __disable_irq();
    if (!rfgen_tip_allows_start()) {
        rfgen_stop();
        if (interrupt_state == 0U) {
            __enable_irq();
        }
        return;
    }

    SET_BIT(TIM1->BDTR, TIM_BDTR_MOE);
    SET_BIT(TIM1->CR1, TIM_CR1_CEN);

    if (interrupt_state == 0U) {
        __enable_irq();
    }
}

void rfgen_stop(void)
{
    /*
     * Preload the GPIO latch low, then take ownership away from TIM1.  This
     * avoids the high-impedance stopped state produced by HAL_TIMEx_PWMN_Stop.
     */
    rfgen_pin_low();

    if (__HAL_RCC_TIM1_IS_CLK_ENABLED()) {
        CLEAR_BIT(TIM1->BDTR, TIM_BDTR_MOE);
        CLEAR_BIT(TIM1->CCER, TIM_CCER_CC3E | TIM_CCER_CC3NE);
        CLEAR_BIT(TIM1->CR1, TIM_CR1_CEN);
    }
}

static bool rfgen_tip_allows_start(void)
{
    /*
     * A running TIM17 update interrupt means that a TIP_DET edge is still in
     * its debounce window.  Waiting also prevents the system-clock change in
     * rfgen_clock_init() from shortening an active timer interval.
     */
    if (tipdetect_has_triggered() ||
        ((TIM17->DIER & TIM_DIER_UIE) != 0U)) {
        return false;
    }

    return HAL_GPIO_ReadPin(TIP_DET_GPIOx,
                            TIP_DET_PINn) == GPIO_PIN_SET;
}

static void rfgen_pin_low(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(RFGEN_GPIOx, RFGEN_PINn, GPIO_PIN_RESET);

    gpio_cfg.Pin = RFGEN_PINn;
    gpio_cfg.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_cfg.Pull = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RFGEN_GPIOx, &gpio_cfg);
}

static void rfgen_fault(void)
{
    rfgen_fault_logged = true;
    rfgen_stop();
}

/*
 * This vector symbol is intentionally private to the module's CSS handling;
 * it is not part of the public rfgen interface.
 */
void NMI_Handler(void)
{
    bool css_fault = __HAL_RCC_GET_IT(RCC_IT_CSS) != RESET;

    HAL_RCC_NMI_IRQHandler();

    if (css_fault) {
        /* CSS has already changed SYSCLK to HSI; restore the 1 ms HAL tick. */
        SystemCoreClockUpdate();
        (void)HAL_InitTick(TICK_INT_PRIORITY);
    }
}

void HAL_RCC_CSSCallback(void)
{
    rfgen_fault();
}
