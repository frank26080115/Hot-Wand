#pragma once

/* Native-test peripheral model, not a hardware implementation. Only the ADC
 * register/initialization surface used by adc.c is provided here. */
#include <stdint.h>

typedef struct
{
    uint32_t ISR, CR, CHSELR, DR;
} ADC_TypeDef;
extern ADC_TypeDef mock_adc;
#define ADC1  (&mock_adc)
#define GPIOA ((void*)0)

typedef struct
{
    uint32_t ClockPrescaler, Resolution, DataAlign, ScanConvMode, EOCSelection;
    uint32_t LowPowerAutoWait, LowPowerAutoPowerOff, ContinuousConvMode, DiscontinuousConvMode;
    uint32_t ExternalTrigConv, ExternalTrigConvEdge, DMAContinuousRequests, Overrun, SamplingTimeCommon;
} ADC_InitTypeDef;
typedef struct
{
    ADC_TypeDef*    Instance;
    ADC_InitTypeDef Init;
} ADC_HandleTypeDef;
typedef struct
{
    uint32_t Channel, Rank, SamplingTime;
} ADC_ChannelConfTypeDef;
typedef struct
{
    uint32_t Pin, Mode, Pull;
} GPIO_InitTypeDef;
typedef struct
{
    uint32_t unused;
} I2C_HandleTypeDef;

#define HAL_OK                        0
#define DISABLE                       0
#define ADC_CLOCK_SYNC_PCLK_DIV4      0
#define ADC_RESOLUTION_10B            0
#define ADC_DATAALIGN_RIGHT           0
#define ADC_SCAN_DIRECTION_FORWARD    0
#define ADC_EOC_SINGLE_CONV           0
#define ADC_SOFTWARE_START            0
#define ADC_EXTERNALTRIGCONVEDGE_NONE 0
#define ADC_OVR_DATA_OVERWRITTEN      0
#define ADC_SAMPLETIME_71CYCLES_5     0
#define ADC_RANK_CHANNEL_NUMBER       0
#define ADC1_IRQn                     0
#define GPIO_MODE_ANALOG              0
#define GPIO_NOPULL                   0
#define ADC_CHANNEL_0                 0
#define ADC_CHANNEL_1                 1
#define ADC_CHANNEL_2                 2
#define ADC_CHANNEL_3                 3
#define ADC_CHANNEL_4                 4
#define ADC_CHANNEL_TEMPSENSOR        16
#define ADC_CHANNEL_VREFINT           17
#define ADC_ISR_EOC                   1
#define ADC_ISR_EOS                   2
#define ADC_ISR_OVR                   4
#define ADC_CR_ADSTART                1
#define GPIO_PIN_0                    1
#define GPIO_PIN_1                    2
#define GPIO_PIN_2                    4
#define GPIO_PIN_3                    8
#define GPIO_PIN_4                    16
#define __HAL_RCC_GPIOA_CLK_ENABLE()  ((void)0)
#define __HAL_RCC_ADC1_CLK_ENABLE()   ((void)0)

static inline void HAL_NVIC_DisableIRQ(int irq)
{
    (void)irq;
}
static inline void HAL_NVIC_EnableIRQ(int irq)
{
    (void)irq;
}
static inline void HAL_NVIC_ClearPendingIRQ(int irq)
{
    (void)irq;
}
static inline void HAL_NVIC_SetPriority(int irq, int priority, int sub)
{
    (void)irq;
    (void)priority;
    (void)sub;
}
static inline void HAL_GPIO_Init(void* port, GPIO_InitTypeDef* cfg)
{
    (void)port;
    (void)cfg;
}
static inline int HAL_ADC_Init(ADC_HandleTypeDef* handle)
{
    (void)handle;
    return HAL_OK;
}
static inline int HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef* handle)
{
    (void)handle;
    return HAL_OK;
}
static inline int HAL_ADC_ConfigChannel(ADC_HandleTypeDef* handle, ADC_ChannelConfTypeDef* cfg)
{
    (void)handle;
    (void)cfg;
    return HAL_OK;
}
static inline int HAL_ADC_Start_IT(ADC_HandleTypeDef* handle)
{
    handle->Instance->CR |= ADC_CR_ADSTART;
    return HAL_OK;
}
static inline uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef* handle)
{
    return handle->Instance->DR;
}
static inline uint32_t HAL_GetTick(void)
{
    return 0;
}
static inline uint32_t __get_PRIMASK(void)
{
    return 0;
}
static inline void __disable_irq(void) {}
static inline void __enable_irq(void) {}
