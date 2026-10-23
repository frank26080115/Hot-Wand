#pragma once
/* Extra peripheral declarations for functional component tests. Real
 * registers are never mapped; all outputs terminate in native RAM. */
#include "../../faults/support/stm32f0xx_hal.h"

typedef struct
{
    uint32_t CR1, PSC, ARR, RCR, CCR1, CCMR1, CCER, BDTR, CNT, EGR, SR;
} FunctionalTimer;
extern FunctionalTimer mock_tim16, mock_tim17;
#define TIM16            (&mock_tim16)
#define TIM17            (&mock_tim17)
#define HSE_VALUE        27120000
#define TIM_CCMR1_OC1M_0 8
#define TIM_CCER_CC1P    2
#define TIM_CR1_ARPE     128
#ifndef TIM_BDTR_MOE
#define TIM_BDTR_MOE 32768
#endif
#define GPIO_AF1_IR                     1
#define __HAL_RCC_SYSCFG_CLK_ENABLE()   ((void)0)
#define __HAL_RCC_TIM16_CLK_ENABLE()    ((void)0)
#define __HAL_RCC_TIM17_CLK_ENABLE()    ((void)0)
#define __HAL_DBGMCU_UNFREEZE_TIM16()   ((void)0)
#define __HAL_DBGMCU_UNFREEZE_TIM17()   ((void)0)
#define __HAL_RCC_TIM16_FORCE_RESET()   ((void)0)
#define __HAL_RCC_TIM17_FORCE_RESET()   ((void)0)
#define __HAL_RCC_TIM16_RELEASE_RESET() ((void)0)
#define __HAL_RCC_TIM17_RELEASE_RESET() ((void)0)

typedef struct
{
    void* Instance;
    struct
    {
        uint32_t BaudRate, WordLength, StopBits, Parity, Mode, HwFlowCtl, OverSampling, OneBitSampling;
    } Init;
    struct
    {
        uint32_t AdvFeatureInit;
    } AdvancedInit;
} UART_HandleTypeDef;
#define USART2                        ((void*)2)
#define GPIO_AF1_USART2               1
#define UART_WORDLENGTH_8B            8
#define UART_STOPBITS_1               1
#define UART_PARITY_NONE              0
#define UART_MODE_TX                  1
#define UART_HWCONTROL_NONE           0
#define UART_OVERSAMPLING_16          16
#define HAL_MAX_DELAY                 UINT32_MAX
#define __HAL_RCC_USART2_CLK_ENABLE() ((void)0)
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef* handle);
HAL_StatusTypeDef
HAL_UART_Transmit(UART_HandleTypeDef* handle, const uint8_t* bytes, uint16_t length, uint32_t timeout);

/* Production RF driver registers/clock side effects. */
#define __HAL_DBGMCU_UNFREEZE_TIM1()         ((void)0)
#define __HAL_RCC_TIM1_CLK_ENABLE()          ((void)0)
#define __HAL_RCC_TIM1_FORCE_RESET()         ((void)0)
#define __HAL_RCC_TIM1_RELEASE_RESET()       ((void)0)
#define __HAL_SYSCFG_BREAK_LOCKUP_LOCK()     ((void)0)
#define __HAL_SYSCFG_BREAK_SRAMPARITY_LOCK() ((void)0)
#define TIM_CCMR2_OC3M_1                     1
#define TIM_CCMR2_OC3M_2                     2
#define TIM_CCMR2_OC3PE                      4
#define TIM_BDTR_OSSR                        1
#define TIM_BDTR_OSSI                        2
#define TIM_BDTR_LOCK_0                      4
#define TIM_BDTR_LOCK_1                      8
#define TIM_BDTR_BKE                         16
#define TIM_BDTR_BKP                         32
#define GPIO_AF2_TIM1                        2

#define GPIO_PIN_13                 8192
#define GPIO_PIN_14                 16384
#define GPIO_SPEED_FREQ_LOW         0
#define UART_ONE_BIT_SAMPLE_DISABLE 0
#define UART_ADVFEATURE_NO_INIT     0
