/*
Tip Detection
This code module is responsible for detecting the presence or absence of a tip connected to the device.
The system must shutdown RF power if the tip is removed, as the missing load will cause very high voltages to appear at
the MOSFET This code module uses a timer to debounce the tip detection signal, shutdown RF power when the tip is
confirmed to be removed, and it latches the fault until the user resets it.
*/

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "fault_test.h"
#include "tipdetect.h"

#include "fault.h"
#include "pins.h"
#include "rfgen.h"
#include "stm32f0xx_hal.h"
#include "systick.h"

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
#define TIPDETECT_EXTI_IRQ_PRIORITY  0
#define TIPDETECT_EXTI4_15_MASK      0xFFF0
#define TIPDETECT_EDGE_DECAY_MS      100
#define TIPDETECT_EDGE_FAULT_COUNT   10

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static TIM_HandleTypeDef tipdetect_timer;
static volatile bool     tipdetect_initialized;
static volatile bool     tipdetect_tip_present;
static volatile bool     tipdetect_triggered = true;
static volatile uint32_t tipdetect_edge_count;
static uint32_t          tipdetect_edge_decay_ms;

#if TIP_SWD_TEST_HARNESS
/* Keep aligned command/status words visible to SWD even in the LTO image. */
volatile uint32_t tip_swd_test_command __attribute__((used, externally_visible)) = TIP_SWD_NO_COMMAND;
volatile uint32_t tip_swd_test_state __attribute__((used, externally_visible))   = TIP_SWD_NOT_READY;
volatile uint32_t tip_swd_test_completed __attribute__((used, externally_visible));
static bool     tip_swd_virtual_present = true;
#endif

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
#if TIP_SWD_TEST_HARNESS
static void tipdetect_swd_task(void);
#endif

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
    __HAL_RCC_TIM14_CLK_ENABLE();
    __HAL_RCC_TIM14_FORCE_RESET();
    __HAL_RCC_TIM14_RELEASE_RESET();

    tipdetect_timer                        = (TIM_HandleTypeDef){0};
    tipdetect_timer.Instance               = TIM14;
    tipdetect_timer.Init.Prescaler         = TIPDETECT_TIMER_PRESCALER;
    tipdetect_timer.Init.CounterMode       = TIM_COUNTERMODE_UP;
    tipdetect_timer.Init.Period            = TIPDETECT_TIMER_PERIOD;
    tipdetect_timer.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    tipdetect_timer.Init.RepetitionCounter = 0;
    tipdetect_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (fault_test_inject(FT_TIP_BASE) || HAL_TIM_Base_Init(&tipdetect_timer) != HAL_OK)
    {
        tipdetect_fail_closed();
        return;
    }

    if (fault_test_inject(FT_TIP_PULSE) || HAL_TIM_OnePulse_Init(&tipdetect_timer, TIM_OPMODE_SINGLE) != HAL_OK)
    {
        tipdetect_fail_closed();
        return;
    }

    __HAL_TIM_DISABLE(&tipdetect_timer);
    __HAL_TIM_DISABLE_IT(&tipdetect_timer, TIM_IT_UPDATE);
    __HAL_TIM_CLEAR_FLAG(&tipdetect_timer, TIM_FLAG_UPDATE);

    /* The detector has an external pull-up. A high level means that the tip is
     * present; a low level means that it has disconnected. */
    gpio_cfg.Pin   = TIP_DET_PINn;
    gpio_cfg.Mode  = GPIO_MODE_IT_RISING_FALLING;
    gpio_cfg.Pull  = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TIP_DET_GPIOx, &gpio_cfg);

#if TIP_SWD_TEST_HARNESS
    /* Retain EXTI's interrupt route for software injection, but prevent real
     * PA5 transitions from perturbing the selected virtual input. Other lines
     * on the shared button/tip IRQ keep their normal configuration. */
    CLEAR_BIT(EXTI->RTSR, TIP_DET_PINn);
    CLEAR_BIT(EXTI->FTSR, TIP_DET_PINn);
#if HOT_WAND_TEST_HARNESS
    tip_swd_virtual_present = (fault_test.setup & (1 << 21)) == 0;
#else
    tip_swd_virtual_present = true;
#endif
    tip_swd_test_command    = TIP_SWD_NO_COMMAND;
    tip_swd_test_state      = tip_swd_virtual_present ? TIP_SWD_PRESENT : TIP_SWD_ABSENT;
    tip_swd_test_completed  = 0;
#endif

    __HAL_GPIO_EXTI_CLEAR_IT(TIP_DET_PINn);
    HAL_NVIC_ClearPendingIRQ(TIM14_IRQn);

    tipdetect_tip_present   = tipdetect_is_present();
    tipdetect_triggered     = !tipdetect_tip_present;
    tipdetect_edge_count    = 0;
    tipdetect_edge_decay_ms = systick_get_ms();
    tipdetect_initialized   = true;

    HAL_NVIC_SetPriority(TIM14_IRQn, TIPDETECT_TIMER_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(TIM14_IRQn);
    HAL_NVIC_SetPriority(EXTI4_15_IRQn, TIPDETECT_EXTI_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

    if (tipdetect_triggered)
    {
        rfgen_stop();
    }
}

void tipdetect_task(void)
{
    uint32_t edge_count;
    uint32_t interrupt_state;
    uint32_t now;

    now             = systick_get_ms();
    interrupt_state = __get_PRIMASK();
    __disable_irq();

#if TIP_SWD_TEST_HARNESS
    tipdetect_swd_task();
#if HOT_WAND_TEST_HARNESS
    if ((fault_test.reserved[0] != 0) && !tipdetect_is_qualifying())
    {
        --fault_test.reserved[0];
        __HAL_GPIO_EXTI_GENERATE_SWIT(TIP_DET_PINn);
    }
#endif
#endif

    edge_count = tipdetect_edge_count;
    if ((uint32_t)(now - tipdetect_edge_decay_ms) >= TIPDETECT_EDGE_DECAY_MS)
    {
        tipdetect_edge_decay_ms = now;
        if (tipdetect_edge_count > 0)
        {
            tipdetect_edge_count--;
        }
    }

    if (interrupt_state == 0)
    {
        __enable_irq();
    }

    if (edge_count > TIPDETECT_EDGE_FAULT_COUNT)
    {
        show_fault("TIP\nDETEC\nMALFU", true);
    }
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

void TIM14_IRQHandler_Impl(void)
{
    bool tip_present;

    if (((TIM14->SR & TIM_SR_UIF) == 0) || ((TIM14->DIER & TIM_DIER_UIE) == 0))
    {
        return;
    }

    CLEAR_BIT(TIM14->DIER, TIM_DIER_UIE);
    CLEAR_BIT(TIM14->CR1, TIM_CR1_CEN);
    CLEAR_BIT(TIM14->SR, TIM_SR_UIF);

    /* Discard edges accumulated during the debounce window, then sample. Any
     * edge racing with or following the sample remains pending and starts
     * another complete debounce interval when EXTI is unmasked. */
    __HAL_GPIO_EXTI_CLEAR_IT(TIP_DET_PINn);
    tip_present           = tipdetect_is_present();
    tipdetect_tip_present = tip_present;

    if (!tip_present)
    {
        tipdetect_triggered = true;
        rfgen_stop();
    }

    SET_BIT(EXTI->IMR, TIP_DET_PINn);

#if TIP_SWD_TEST_HARNESS
    if (tip_swd_test_state == TIP_SWD_QUALIFYING)
    {
        tip_swd_test_state = tip_present ? TIP_SWD_PRESENT : TIP_SWD_ABSENT;
        ++tip_swd_test_completed;
    }
#endif
}

void EXTI4_15_IRQHandler_Impl(void)
{
    uint32_t pending;
    uint32_t pin;

    pending = EXTI->PR & EXTI->IMR & TIPDETECT_EXTI4_15_MASK;

    if ((pending & TIP_DET_PINn) != 0)
    {
        tipdetect_edge_count++;
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

    /* EXTI4_15 is shared. Route any other enabled line through the standard HAL
     * callback so a future button or peripheral cannot cause an
     * unhandled-interrupt storm. */
    for (pin = GPIO_PIN_4; pin <= GPIO_PIN_15; pin <<= 1)
    {
        if ((pending & pin) != 0)
        {
            HAL_GPIO_EXTI_IRQHandler((uint16_t)pin);
        }
    }
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

    /* Never clear the latch while the last debounced state or the current
     * selected input says that the tip is absent, or while an edge is still
     * being qualified by TIM14. */
    if (!tipdetect_is_qualifying() && tipdetect_tip_present && tipdetect_is_present())
    {
        tipdetect_triggered = false;
    }

    if (interrupt_state == 0)
    {
        __enable_irq();
    }
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

#if TIP_SWD_TEST_HARNESS

static void tipdetect_swd_task(void)
{
    uint32_t command;

    /* The caller holds PRIMASK. Do not overwrite an in-flight request or
     * restart its timer; a stalled qualification remains fail-closed. */
    if (!tipdetect_initialized || (tip_swd_test_state == TIP_SWD_QUALIFYING) || tipdetect_is_qualifying())
    {
        return;
    }
    command = tip_swd_test_command;
    if (command == TIP_SWD_NO_COMMAND)
    {
        return;
    }
    tip_swd_test_command = TIP_SWD_NO_COMMAND;
    if (command > TIP_SWD_PRESENT)
    {
        return;
    }
    if (tip_swd_virtual_present == (command == TIP_SWD_PRESENT))
    {
        ++tip_swd_test_completed;
        return;
    }

    tip_swd_virtual_present = command == TIP_SWD_PRESENT;
    tip_swd_test_state      = TIP_SWD_QUALIFYING;
    /* Exercise the existing shared interrupt, edge counter and one-shot
     * timer, rather than setting the disconnect latch from the task. */
    __HAL_GPIO_EXTI_GENERATE_SWIT(TIP_DET_PINn);
}

#endif

static void tipdetect_arm_timer(void)
{
    CLEAR_BIT(TIM14->CR1, TIM_CR1_CEN);
    CLEAR_BIT(TIM14->DIER, TIM_DIER_UIE);
    TIM14->PSC = TIPDETECT_TIMER_PRESCALER;
    TIM14->ARR = TIPDETECT_TIMER_PERIOD;
    TIM14->CNT = 0;
    TIM14->EGR = TIM_EGR_UG;
    CLEAR_BIT(TIM14->SR, TIM_SR_UIF);
    HAL_NVIC_ClearPendingIRQ(TIM14_IRQn);
    SET_BIT(TIM14->DIER, TIM_DIER_UIE);
    SET_BIT(TIM14->CR1, TIM_CR1_CEN);
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

bool tipdetect_is_present(void)
{
#if TIP_SWD_TEST_HARNESS
    return tip_swd_virtual_present;
#else
    return HAL_GPIO_ReadPin(TIP_DET_GPIOx, TIP_DET_PINn) == GPIO_PIN_SET;
#endif
}

bool tipdetect_is_qualifying(void)
{
    /*
     * UIE is set together with CEN when an edge begins qualification. The ISR
     * clears UIE before sampling, so it also covers the interval where the
     * update is pending but its handler has not run yet.
     */
    return (TIM14->DIER & TIM_DIER_UIE) != 0;
}

bool tipdetect_has_triggered(void)
{
    return tipdetect_triggered;
}

// -----------------------------------------------------------------------------
// Debug / Fault Helpers
// -----------------------------------------------------------------------------

static void tipdetect_fail_closed(void)
{
    tipdetect_initialized = false;
    tipdetect_tip_present = false;
    tipdetect_triggered   = true;

#if TIP_SWD_TEST_HARNESS
    tip_swd_test_state = TIP_SWD_NOT_READY;
#endif

    CLEAR_BIT(EXTI->IMR, TIP_DET_PINn);
    if (__HAL_RCC_TIM14_IS_CLK_ENABLED())
    {
        CLEAR_BIT(TIM14->DIER, TIM_DIER_UIE);
        CLEAR_BIT(TIM14->CR1, TIM_CR1_CEN);
        CLEAR_BIT(TIM14->SR, TIM_SR_UIF);
    }

    rfgen_stop();
}
