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
    uint32_t Pin, Mode, Pull, Speed, Alternate;
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
#if NATIVE_OUTPUT_MODEL
extern uint32_t mock_pa6_mode, mock_pa6_level, mock_pb1_mode, mock_pb1_level, mock_af_calls;
#endif
static inline void HAL_GPIO_Init(void* port, GPIO_InitTypeDef* cfg)
{
#if NATIVE_OUTPUT_MODEL
    if (port == GPIOA && cfg->Pin == 64)
    {
        mock_pa6_mode = cfg->Mode;
    }
    if (port == (void*)1 && cfg->Pin == 2)
    {
        mock_pb1_mode = cfg->Mode;
    }
    if (cfg->Mode == 2)
    {
        ++mock_af_calls;
    }
#endif
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

/* PWM timer model: writes are observed, no electrical behavior is claimed. */
typedef struct
{
    uint32_t CR1, PSC, ARR, CCR1, CCMR1, EGR, SR, CCER;
} TIM_TypeDef;
extern TIM_TypeDef mock_tim3;
#define TIM3                           (&mock_tim3)
#define GPIO_PIN_6                     64
#define GPIO_PIN_SET                   1
#define GPIO_PIN_RESET                 0
#define GPIO_MODE_OUTPUT_PP            1
#define GPIO_MODE_AF_PP                2
#define GPIO_SPEED_FREQ_HIGH           1
#define GPIO_AF1_TIM3                  1
#define TIM_CCMR1_OC1M_1               1
#define TIM_CCMR1_OC1M_2               2
#define TIM_CCMR1_OC1PE                4
#define TIM_EGR_UG                     1
#define TIM_CCER_CC1E                  1
#define TIM_CR1_CEN                    1
#define __HAL_RCC_TIM3_CLK_ENABLE()    ((void)0)
#define __HAL_RCC_TIM3_FORCE_RESET()   ((void)0)
#define __HAL_RCC_TIM3_RELEASE_RESET() ((void)0)
static inline void HAL_GPIO_WritePin(void* port, uint32_t pin, uint32_t level)
{
#if NATIVE_OUTPUT_MODEL
    if (port == GPIOA && pin == 64)
    {
        mock_pa6_level = level;
    }
    if (port == (void*)1 && pin == 2)
    {
        mock_pb1_level = level;
    }
#endif
}

/* Flash and watchdog return-value models. Addresses use the MCU's low word;
 * native mocks translate it back to the DLL journal page. */
#define FLASH_PAGE_SIZE            1024
#define FLASH_TYPEPROGRAM_HALFWORD 0
#define FLASH_TYPEERASE_PAGES      0
typedef int HAL_StatusTypeDef;
typedef struct
{
    uint32_t TypeErase, PageAddress, NbPages;
} FLASH_EraseInitTypeDef;
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t value);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef* cfg, uint32_t* error);
typedef struct
{
    uint32_t Prescaler, Reload, Window;
} IWDG_InitTypeDef;
typedef struct
{
    void*            Instance;
    IWDG_InitTypeDef Init;
} IWDG_HandleTypeDef;
#define IWDG                          ((void*)0)
#define IWDG_PRESCALER_64             64
#define IWDG_WINDOW_DISABLE           4095
#define __HAL_RCC_DBGMCU_CLK_ENABLE() ((void)0)
#define __HAL_DBGMCU_UNFREEZE_IWDG()  mock_unfreeze()
void              mock_unfreeze(void);
HAL_StatusTypeDef HAL_IWDG_Init(IWDG_HandleTypeDef* handle);
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef* handle);

/* RF peripheral surface for the inhibited shared-build regression fixture. */
#if NATIVE_OUTPUT_MODEL
#define GPIOB                        ((void*)1)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define CLEAR_BIT(reg, bits)         ((reg) &= ~(bits))
#define SET_BIT(reg, bits)           ((reg) |= (bits))
typedef struct
{
    uint32_t CR1, CCER, BDTR, PSC, ARR, RCR, CCR3, CCMR2, CNT, EGR, SR;
} RF_TIM_TypeDef;
extern RF_TIM_TypeDef mock_tim1;
#define TIM1                            (&mock_tim1)
#define __HAL_RCC_TIM1_IS_CLK_ENABLED() 1
#define TIM_BDTR_MOE                    32768
#define TIM_CCER_CC3E                   256
#define TIM_CCER_CC3NE                  1024
#define RESET                           0
#define RCC_FLAG_HSERDY                 1
#define RCC_SYSCLKSOURCE_STATUS_HSE     1
#define RCC_OSCILLATORTYPE_HSE          1
#define RCC_HSE_ON                      1
#define RCC_PLL_NONE                    0
#define RCC_CLOCKTYPE_SYSCLK            1
#define RCC_CLOCKTYPE_HCLK              2
#define RCC_CLOCKTYPE_PCLK1             4
#define RCC_SYSCLKSOURCE_HSE            1
#define RCC_SYSCLK_DIV1                 1
#define RCC_HCLK_DIV1                   1
#define FLASH_LATENCY_1                 1
#define RCC_IT_CSS                      1
#define TICK_INT_PRIORITY               0
#define __HAL_RCC_GET_FLAG(flag)        1
#define __HAL_RCC_GET_SYSCLK_SOURCE()   1
#define __HAL_RCC_GET_IT(flag)          0
typedef struct
{
    uint32_t OscillatorType, HSEState;
    struct
    {
        uint32_t PLLState;
    } PLL;
} RCC_OscInitTypeDef;
typedef struct
{
    uint32_t ClockType, SYSCLKSource, AHBCLKDivider, APB1CLKDivider;
} RCC_ClkInitTypeDef;
static inline void HAL_RCC_EnableCSS(void) {}
static inline int  HAL_RCC_OscConfig(RCC_OscInitTypeDef* cfg)
{
    return HAL_OK;
}
static inline int HAL_RCC_ClockConfig(RCC_ClkInitTypeDef* cfg, uint32_t latency)
{
    return HAL_OK;
}
static inline void HAL_RCC_NMI_IRQHandler(void) {}
static inline void SystemCoreClockUpdate(void) {}
static inline void HAL_InitTick(uint32_t priority) {}
#endif
