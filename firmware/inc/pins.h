#pragma once

#include "stm32f0xx_hal.h"

#define DC_SENS_GPIOx    GPIOA
#define DC_SENS_PINn     GPIO_PIN_0
#define DC_SENS_ADCCHANn ADC_CHANNEL_0
#define DC_SENS_IDX      0

#define THERM_1_GPIOx    GPIOA
#define THERM_1_PINn     GPIO_PIN_1
#define THERM_1_ADCCHANn ADC_CHANNEL_1
#define THERM_1_IDX      3

#define THERM_2_GPIOx    GPIOA
#define THERM_2_PINn     GPIO_PIN_2
#define THERM_2_ADCCHANn ADC_CHANNEL_2
#define THERM_2_IDX      4

#define BUCK_SENS_GPIOx    GPIOA
#define BUCK_SENS_PINn     GPIO_PIN_3
#define BUCK_SENS_ADCCHANn ADC_CHANNEL_3
#define BUCK_SENS_IDX      1

#define CURR_SENS_GPIOx    GPIOA
#define CURR_SENS_PINn     GPIO_PIN_4
#define CURR_SENS_ADCCHANn ADC_CHANNEL_4
#define CURR_SENS_IDX      2

#define MCU_TEMP_ADCCHANn ADC_CHANNEL_TEMPSENSOR
#define MCU_TEMP_IDX      5

#define TIP_DET_GPIOx GPIOA
#define TIP_DET_PINn  GPIO_PIN_5

#define PWR_ATTENU_GPIOx GPIOA
#define PWR_ATTENU_PINn  GPIO_PIN_6

#define RFGEN_GPIOx GPIOB
#define RFGEN_PINn  GPIO_PIN_1

#define BTN_GPIOx GPIOA
#define BTN_PINn  GPIO_PIN_7

#define FAN_GPIOx GPIOA
#define FAN_PINn  GPIO_PIN_13

#define SCL_GPIOx GPIOA
#define SCL_PINn  GPIO_PIN_9

#define SDA_GPIOx GPIOA
#define SDA_PINn  GPIO_PIN_10

#define SWDIO_GPIOx GPIOA
#define SWDIO_PINn  GPIO_PIN_13

#define SWCLK_GPIOx GPIOA
#define SWCLK_PINn  GPIO_PIN_14

#define UART_TX_GPIOx GPIOA
#define UART_TX_PINn  GPIO_PIN_14

#define OSC_IN_GPIOx GPIOF
#define OSC_IN_PINn  GPIO_PIN_0

#define OSC_OUT_GPIOx GPIOF
#define OSC_OUT_PINn  GPIO_PIN_1
