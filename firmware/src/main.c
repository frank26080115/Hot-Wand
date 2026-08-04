#include "oled.h"
#include "stm32f0xx_hal.h"
#include "uart_debug.h"

#include <stddef.h>

#if !defined(HOT_WAND_TARGET_STM32F030) && \
    !defined(HOT_WAND_TARGET_STM32F042)
#error "This source supports only the STM32F030 and STM32F042 targets."
#endif

#define OLED_WIDTH 128U

static void OLED_RenderDemo(void);
static void Error_Handler(void);

int main(void)
{
    /*
     * SystemInit() leaves the STM32F030 on its reset-default 8 MHz HSI clock.
    * That is intentional for this crystal-independent display smoke test.
     */
    HAL_Init();
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
