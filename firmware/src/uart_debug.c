#include "uart_debug.h"
#include "pins.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static UART_HandleTypeDef uart1;
static bool uart_allowed;
static bool uart_initialized;

static bool USART1_TX_Init(void);
static void UART_ErrorHandler(void);

void HAL_UART_MspInit(UART_HandleTypeDef *handle)
{
    GPIO_InitTypeDef gpio = {0};

    if (handle->Instance != USART1) {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    gpio.Pin = UART_TX_PINn;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF1_USART1;
    HAL_GPIO_Init(UART_TX_GPIOx, &gpio);
}

void UART_SetAllowed(bool allowed)
{
    uart_allowed = allowed;
}

static bool USART1_TX_Init(void)
{
    uart1.Instance = USART1;
    uart1.Init.BaudRate = 115200U;
    uart1.Init.WordLength = UART_WORDLENGTH_8B;
    uart1.Init.StopBits = UART_STOPBITS_1;
    uart1.Init.Parity = UART_PARITY_NONE;
    uart1.Init.Mode = UART_MODE_TX;
    uart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart1.Init.OverSampling = UART_OVERSAMPLING_16;
    uart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    uart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&uart1) != HAL_OK) {
        return false;
    }

    uart_initialized = true;
    return true;
}

void UART_Write(const char *text)
{
    size_t length;

    if (!uart_allowed || (text == NULL)) {
        return;
    }

    length = strlen(text);
    if ((length == 0U) || (length > UINT16_MAX)) {
        return;
    }

    if (!uart_initialized && !USART1_TX_Init()) {
        UART_ErrorHandler();
    }

    if (HAL_UART_Transmit(&uart1,
                          (const uint8_t *)text,
                          (uint16_t)length,
                          HAL_MAX_DELAY) != HAL_OK) {
        UART_ErrorHandler();
    }
}

static void UART_ErrorHandler(void)
{
    __disable_irq();

    for (;;) {
    }
}
