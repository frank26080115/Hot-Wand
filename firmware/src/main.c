#include "oled.h"
#include "stm32f0xx_hal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(HOT_WAND_TARGET_STM32F030)
#error "This source currently supports only the STM32F030 PlatformIO environment."
#endif

#define OLED_WIDTH 128U

/* 100 kHz standard-mode I2C with the reset-default 8 MHz HSI clock. */
#define I2C_TIMING_100KHZ_AT_8MHZ 0x2000090EU

static UART_HandleTypeDef uart1;
static I2C_HandleTypeDef i2c1;
static OLED_Handle oled;

static void USART1_TX_Init(void);
static void I2C1_Init(void);
static void UART_Write(const char *text);
static void OLED_RenderDemo(void);
static void Error_Handler(void);

int main(void)
{
    /*
     * SystemInit() leaves the STM32F030 on its reset-default 8 MHz HSI clock.
     * That is intentional for this crystal-independent display smoke test.
     */
    HAL_Init();
    USART1_TX_Init();
    I2C1_Init();

    UART_Write("U8g2 SSD1306 demo starting\r\n");

    if (!OLED_Init(&oled, &i2c1)) {
        UART_Write("U8g2 SSD1306 initialization failed\r\n");
        Error_Handler();
    }

    OLED_RenderDemo();

    if (!OLED_SendBuffer(&oled)) {
        UART_Write("U8g2 SSD1306 drawing failed\r\n");
        Error_Handler();
    }

    UART_Write("U8g2 SSD1306 128x32 demo ready\r\n");

    for (;;) {
        HAL_Delay(1000U);
    }
}

void HAL_I2C_MspInit(I2C_HandleTypeDef *handle)
{
    GPIO_InitTypeDef gpio = {0};

    if (handle->Instance != I2C1) {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    /*
     * STM32F030F4P6 TSSOP-20:
     *   PA9  (pin 17) -> I2C1_SCL
     *   PA10 (pin 18) -> I2C1_SDA
     * External or OLED-module pull-up resistors are required.
     */
    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOA, &gpio);
}

void HAL_UART_MspInit(UART_HandleTypeDef *handle)
{
    GPIO_InitTypeDef gpio = {0};

    if (handle->Instance != USART1) {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF1_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static void I2C1_Init(void)
{
    __HAL_RCC_I2C1_CONFIG(RCC_I2C1CLKSOURCE_HSI);

    i2c1.Instance = I2C1;
    i2c1.Init.Timing = I2C_TIMING_100KHZ_AT_8MHZ;
    i2c1.Init.OwnAddress1 = 0U;
    i2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    i2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    i2c1.Init.OwnAddress2 = 0U;
    i2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    i2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    i2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&i2c1) != HAL_OK) {
        Error_Handler();
    }
}

static void USART1_TX_Init(void)
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
        Error_Handler();
    }
}

static void UART_Write(const char *text)
{
    size_t length;

    if (text == NULL) {
        return;
    }

    length = strlen(text);
    if ((length == 0U) || (length > UINT16_MAX)) {
        return;
    }

    if (HAL_UART_Transmit(&uart1,
                          (const uint8_t *)text,
                          (uint16_t)length,
                          HAL_MAX_DELAY) != HAL_OK) {
        Error_Handler();
    }
}

static void OLED_RenderDemo(void)
{
    u8g2_t *graphics = OLED_GetGraphics(&oled);

    if (graphics == NULL) {
        Error_Handler();
    }

    u8g2_ClearBuffer(graphics);
    u8g2_SetFont(graphics, u8g2_font_6x10_tr);
    u8g2_DrawStr(graphics, 40U, 11U, "HOT WAND");
    u8g2_DrawHLine(graphics, 0U, 15U, OLED_WIDTH);
    u8g2_DrawStr(graphics, 16U, 29U, "U8G2 + STM32F030");
}

static void Error_Handler(void)
{
    __disable_irq();

    for (;;) {
    }
}
