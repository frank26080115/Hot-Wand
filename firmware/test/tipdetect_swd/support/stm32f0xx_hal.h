#pragma once

/* Native model for tipdetect.c. Timer expiry and EXTI dispatch are advanced
 * explicitly by the tests; this does not model physical interrupt latency. */
#include <stdint.h>

typedef struct
{
    uint32_t unused;
} I2C_HandleTypeDef;

typedef struct
{
    uint32_t CR1, DIER, SR, PSC, ARR, CNT, EGR;
} TIM_TypeDef;
typedef struct
{
    TIM_TypeDef* Instance;
    struct
    {
        uint32_t Prescaler, CounterMode, Period, ClockDivision;
        uint32_t RepetitionCounter, AutoReloadPreload;
    } Init;
} TIM_HandleTypeDef;
typedef struct
{
    uint32_t IMR, PR, RTSR, FTSR, SWIER;
} EXTI_TypeDef;
typedef struct
{
    uint32_t Pin, Mode, Pull, Speed;
} GPIO_InitTypeDef;

extern TIM_TypeDef  mock_tim14;
extern EXTI_TypeDef mock_exti;
extern uint32_t     mock_clock, mock_primask, mock_pin, mock_timer_failure, mock_callbacks;

#define TIM14                            (&mock_tim14)
#define EXTI                             (&mock_exti)
#define GPIOA                            ((void*)0)
#define GPIO_PIN_4                       (1 << 4)
#define GPIO_PIN_5                       (1 << 5)
#define GPIO_PIN_7                       (1 << 7)
#define GPIO_PIN_15                      (1 << 15)
#define GPIO_PIN_SET                     1
#define GPIO_MODE_IT_RISING_FALLING      1
#define GPIO_NOPULL                      0
#define GPIO_SPEED_FREQ_LOW              0
#define TIM_COUNTERMODE_UP               0
#define TIM_CLOCKDIVISION_DIV1           0
#define TIM_AUTORELOAD_PRELOAD_DISABLE   0
#define TIM_OPMODE_SINGLE                0
#define TIM14_IRQn                       0
#define EXTI4_15_IRQn                    1
#define HAL_OK                           0
#define TIM_CR1_CEN                      1
#define TIM_DIER_UIE                     1
#define TIM_SR_UIF                       1
#define TIM_EGR_UG                       1
#define TIM_IT_UPDATE                    TIM_DIER_UIE
#define TIM_FLAG_UPDATE                  TIM_SR_UIF
#define CLEAR_BIT(reg, mask)             ((reg) &= ~(mask))
#define SET_BIT(reg, mask)               ((reg) |= (mask))
#define __HAL_RCC_GPIOA_CLK_ENABLE()     ((void)0)
#define __HAL_RCC_TIM14_CLK_ENABLE()     (mock_clock = 1)
#define __HAL_RCC_TIM14_FORCE_RESET()    ((void)0)
#define __HAL_RCC_TIM14_RELEASE_RESET()  ((void)0)
#define __HAL_RCC_TIM14_IS_CLK_ENABLED() mock_clock
#define __HAL_TIM_DISABLE(h)             CLEAR_BIT((h)->Instance->CR1, TIM_CR1_CEN)
#define __HAL_TIM_DISABLE_IT(h, it)      CLEAR_BIT((h)->Instance->DIER, it)
#define __HAL_TIM_CLEAR_FLAG(h, flag)    CLEAR_BIT((h)->Instance->SR, flag)

/* Model write-one-to-clear PR and SWIER's corresponding hardware clearing. */
#define __HAL_GPIO_EXTI_CLEAR_IT(pin)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        CLEAR_BIT(EXTI->PR, pin);                                                                                      \
        CLEAR_BIT(EXTI->SWIER, pin);                                                                                   \
    } while (0)
#define __HAL_GPIO_EXTI_GENERATE_SWIT(pin)                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        SET_BIT(EXTI->SWIER, pin);                                                                                     \
        SET_BIT(EXTI->PR, pin);                                                                                        \
    } while (0)

/* Shared with the native battery/button fixture. */
void HAL_Delay(uint32_t delay);

static inline uint32_t __get_PRIMASK(void)
{
    return mock_primask;
}
static inline void __disable_irq(void)
{
    mock_primask = 1;
}
static inline void __enable_irq(void)
{
    mock_primask = 0;
}
static inline void HAL_NVIC_ClearPendingIRQ(int irq) {}
static inline void HAL_NVIC_SetPriority(int irq, int priority, int sub) {}
static inline void HAL_NVIC_EnableIRQ(int irq) {}
static inline int  HAL_TIM_Base_Init(TIM_HandleTypeDef* timer)
{
    return mock_timer_failure == 1 ? 1 : HAL_OK;
}
static inline int HAL_TIM_OnePulse_Init(TIM_HandleTypeDef* timer, int mode)
{
    return mock_timer_failure == 2 ? 1 : HAL_OK;
}
static inline uint32_t HAL_GPIO_ReadPin(void* port, uint32_t pin)
{
    return mock_pin;
}
static inline void HAL_GPIO_Init(void* port, GPIO_InitTypeDef* cfg)
{
    EXTI->IMR |= cfg->Pin;
    EXTI->RTSR |= cfg->Pin;
    EXTI->FTSR |= cfg->Pin;
}
static inline void HAL_GPIO_EXTI_IRQHandler(uint16_t pin)
{
    __HAL_GPIO_EXTI_CLEAR_IT(pin);
    ++mock_callbacks;
}
