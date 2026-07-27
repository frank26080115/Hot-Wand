#include "stm32f0xx_hal.h"
#include "ssd1306.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(HOT_WAND_TARGET_STM32F030)
#error "This source currently supports only the STM32F030 PlatformIO environment."
#endif

#define OLED_WIDTH 128U
#define OLED_HEIGHT 32U
#define OLED_ADDRESS_7BIT 0x3CU
#define OLED_FRAMEBUFFER_SIZE ((OLED_WIDTH * OLED_HEIGHT) / 8U)

/* 100 kHz standard-mode I2C with the reset-default 8 MHz HSI clock. */
#define I2C_TIMING_100KHZ_AT_8MHZ 0x2000090EU

typedef struct {
    char character;
    uint8_t columns[5];
} Glyph5x7;

static const Glyph5x7 font_5x7[] = {
    {' ', {0x00U, 0x00U, 0x00U, 0x00U, 0x00U}},
    {'0', {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU}},
    {'3', {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U}},
    {'A', {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU}},
    {'D', {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU}},
    {'E', {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U}},
    {'F', {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U}},
    {'H', {0x7FU, 0x08U, 0x08U, 0x08U, 0x7FU}},
    {'L', {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U}},
    {'M', {0x7FU, 0x02U, 0x0CU, 0x02U, 0x7FU}},
    {'N', {0x7FU, 0x04U, 0x08U, 0x10U, 0x7FU}},
    {'O', {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU}},
    {'T', {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U}},
    {'W', {0x3FU, 0x40U, 0x38U, 0x40U, 0x3FU}},
};

static UART_HandleTypeDef uart1;
static I2C_HandleTypeDef i2c1;
static uint8_t oled_framebuffer[OLED_FRAMEBUFFER_SIZE];

static void USART1_TX_Init(void);
static void I2C1_Init(void);
static void UART_Write(const char *text);
static int32_t OLED_SendI2C(void *user_context,
                            uint8_t i2c_address_7bit,
                            const uint8_t *data,
                            size_t length);
static void OLED_RenderDemo(void);
static void Framebuffer_DrawText(uint16_t x,
                                 uint16_t y,
                                 const char *text,
                                 uint8_t scale);
static void Framebuffer_DrawCharacter(uint16_t x,
                                      uint16_t y,
                                      char character,
                                      uint8_t scale);
static void Framebuffer_SetPixel(uint16_t x, uint16_t y);
static const uint8_t *Font_GetGlyph(char character);
static void Error_Handler(void);

static const OLED_Config oled = {
    .bus_type = OLED_BUS_I2C,
    .width = OLED_WIDTH,
    .height = OLED_HEIGHT,
    .user_context = &i2c1,
    .transport = {
        .i2c = {
            .i2c_address_7bit = OLED_ADDRESS_7BIT,
            .send_fn = OLED_SendI2C,
        },
    },
};

int main(void)
{
    /*
     * SystemInit() leaves the STM32F030 on its reset-default 8 MHz HSI clock.
     * That is intentional for this crystal-independent display smoke test.
     */
    HAL_Init();
    USART1_TX_Init();
    I2C1_Init();

    UART_Write("SSD1306 demo starting\r\n");

    if (OLED_BufferSize(&oled) != sizeof(oled_framebuffer)) {
        UART_Write("Unexpected OLED buffer size\r\n");
        Error_Handler();
    }

    /* U6 has no reset pin, so allow its power-on reset to settle. */
    HAL_Delay(100U);

    if (OLED_Init(&oled) != OLED_OK) {
        UART_Write("SSD1306 initialization failed\r\n");
        Error_Handler();
    }

    OLED_RenderDemo();

    if (OLED_DrawBitmap(&oled,
                        oled_framebuffer,
                        sizeof(oled_framebuffer)) != OLED_OK) {
        UART_Write("SSD1306 drawing failed\r\n");
        Error_Handler();
    }

    UART_Write("SSD1306 128x32 demo ready\r\n");

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

void SysTick_Handler(void)
{
    HAL_IncTick();
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

static int32_t OLED_SendI2C(void *user_context,
                            uint8_t i2c_address_7bit,
                            const uint8_t *data,
                            size_t length)
{
    I2C_HandleTypeDef *handle = (I2C_HandleTypeDef *)user_context;

    if ((handle == NULL) || (data == NULL) ||
        (length == 0U) || (length > UINT16_MAX)) {
        return OLED_ERR_INVALID_ARG;
    }

    if (HAL_I2C_Master_Transmit(handle,
                                (uint16_t)(i2c_address_7bit << 1U),
                                (uint8_t *)data,
                                (uint16_t)length,
                                100U) != HAL_OK) {
        return OLED_ERR_IO;
    }

    return OLED_OK;
}

static void OLED_RenderDemo(void)
{
    memset(oled_framebuffer, 0, sizeof(oled_framebuffer));

    Framebuffer_DrawText(16U, 1U, "HOT WAND", 2U);
    Framebuffer_DrawText(22U, 22U, "F030 OLED DEMO", 1U);
}

static void Framebuffer_DrawText(uint16_t x,
                                 uint16_t y,
                                 const char *text,
                                 uint8_t scale)
{
    if ((text == NULL) || (scale == 0U)) {
        return;
    }

    while (*text != '\0') {
        Framebuffer_DrawCharacter(x, y, *text, scale);
        x = (uint16_t)(x + (6U * scale));
        text++;
    }
}

static void Framebuffer_DrawCharacter(uint16_t x,
                                      uint16_t y,
                                      char character,
                                      uint8_t scale)
{
    const uint8_t *glyph = Font_GetGlyph(character);

    for (uint8_t column = 0U; column < 5U; column++) {
        for (uint8_t row = 0U; row < 7U; row++) {
            if ((glyph[column] & (uint8_t)(1U << row)) == 0U) {
                continue;
            }

            for (uint8_t dx = 0U; dx < scale; dx++) {
                for (uint8_t dy = 0U; dy < scale; dy++) {
                    Framebuffer_SetPixel(
                        (uint16_t)(x + ((uint16_t)column * scale) + dx),
                        (uint16_t)(y + ((uint16_t)row * scale) + dy));
                }
            }
        }
    }
}

static void Framebuffer_SetPixel(uint16_t x, uint16_t y)
{
    size_t byte_index;

    if ((x >= OLED_WIDTH) || (y >= OLED_HEIGHT)) {
        return;
    }

    byte_index = ((size_t)(y / 8U) * OLED_WIDTH) + x;
    oled_framebuffer[byte_index] |= (uint8_t)(1U << (y & 7U));
}

static const uint8_t *Font_GetGlyph(char character)
{
    for (size_t index = 0U;
         index < (sizeof(font_5x7) / sizeof(font_5x7[0]));
         index++) {
        if (font_5x7[index].character == character) {
            return font_5x7[index].columns;
        }
    }

    return font_5x7[0].columns;
}

static void Error_Handler(void)
{
    __disable_irq();

    for (;;) {
    }
}
