#ifndef HOT_WAND_OLED_H
#define HOT_WAND_OLED_H

#include "stm32f0xx_hal.h"
#include "u8g2.h"

#include <stdbool.h>
#include <stdint.h>

#define OLED_I2C_ADDRESS_7BIT 0x3CU
#define OLED_I2C_ADDRESS_U8G2 ((uint8_t)(OLED_I2C_ADDRESS_7BIT << 1U))
#define OLED_I2C_TRANSFER_CAPACITY 32U

typedef struct {
    u8g2_t graphics;
    I2C_HandleTypeDef *i2c;
    uint8_t transfer_buffer[OLED_I2C_TRANSFER_CAPACITY];
    uint8_t transfer_length;
    uint8_t transfer_active;
    uint8_t transport_ok;
    uint8_t initialized;
} OLED_Handle;

bool OLED_Init(OLED_Handle *oled, I2C_HandleTypeDef *i2c);
u8g2_t *OLED_GetGraphics(OLED_Handle *oled);
bool OLED_SendBuffer(OLED_Handle *oled);

#endif
