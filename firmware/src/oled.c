#include "oled.h"

#include <string.h>

static uint8_t OLED_I2CByteCallback(u8x8_t *u8x8,
                                    uint8_t message,
                                    uint8_t argument,
                                    void *data);
static uint8_t OLED_GPIOAndDelayCallback(u8x8_t *u8x8,
                                         uint8_t message,
                                         uint8_t argument,
                                         void *data);
static OLED_Handle *OLED_FromU8x8(u8x8_t *u8x8);
static void OLED_SetTransportError(OLED_Handle *oled);

bool OLED_Init(OLED_Handle *oled, I2C_HandleTypeDef *i2c)
{
    if ((oled == NULL) || (i2c == NULL)) {
        return false;
    }

    memset(oled, 0, sizeof(*oled));
    oled->i2c = i2c;
    oled->transport_ok = 1U;

    u8g2_Setup_ssd1306_i2c_128x32_univision_f(
        &oled->graphics,
        U8G2_R0,
        OLED_I2CByteCallback,
        OLED_GPIOAndDelayCallback);
    u8g2_SetUserPtr(&oled->graphics, oled);

    /*
     * U8g2 and STM32 HAL both use the left-shifted form of a 7-bit I2C
     * address here. The SSD1306 address 0x3C is therefore passed as 0x78.
     */
    u8g2_SetI2CAddress(&oled->graphics, OLED_I2C_ADDRESS_U8G2);

    u8g2_InitDisplay(&oled->graphics);
    if (oled->transport_ok == 0U) {
        return false;
    }

    u8g2_SetPowerSave(&oled->graphics, 0U);
    if (oled->transport_ok == 0U) {
        return false;
    }

    oled->initialized = 1U;
    return true;
}

u8g2_t *OLED_GetGraphics(OLED_Handle *oled)
{
    if ((oled == NULL) || (oled->initialized == 0U)) {
        return NULL;
    }

    return &oled->graphics;
}

bool OLED_SendBuffer(OLED_Handle *oled)
{
    if ((oled == NULL) || (oled->initialized == 0U)) {
        return false;
    }

    oled->transport_ok = 1U;
    u8g2_SendBuffer(&oled->graphics);
    return oled->transport_ok != 0U;
}

static uint8_t OLED_I2CByteCallback(u8x8_t *u8x8,
                                    uint8_t message,
                                    uint8_t argument,
                                    void *data)
{
    OLED_Handle *oled = OLED_FromU8x8(u8x8);

    if ((oled == NULL) || (oled->i2c == NULL)) {
        return 0U;
    }

    switch (message) {
    case U8X8_MSG_BYTE_INIT:
    case U8X8_MSG_BYTE_SET_DC:
        return 1U;

    case U8X8_MSG_BYTE_START_TRANSFER:
        oled->transfer_length = 0U;
        oled->transfer_active = 1U;
        return 1U;

    case U8X8_MSG_BYTE_SEND:
        if ((oled->transfer_active == 0U) ||
            ((argument > 0U) && (data == NULL)) ||
            (argument >
             (uint8_t)(OLED_I2C_TRANSFER_CAPACITY -
                       oled->transfer_length))) {
            OLED_SetTransportError(oled);
            return 0U;
        }

        if (argument > 0U) {
            memcpy(&oled->transfer_buffer[oled->transfer_length],
                   data,
                   argument);
            oled->transfer_length =
                (uint8_t)(oled->transfer_length + argument);
        }
        return 1U;

    case U8X8_MSG_BYTE_END_TRANSFER:
        if (oled->transfer_active == 0U) {
            OLED_SetTransportError(oled);
            return 0U;
        }

        oled->transfer_active = 0U;
        if (oled->transport_ok == 0U) {
            oled->transfer_length = 0U;
            return 0U;
        }

        if ((oled->transfer_length > 0U) &&
            (HAL_I2C_Master_Transmit(
                 oled->i2c,
                 (uint16_t)u8x8_GetI2CAddress(u8x8),
                 oled->transfer_buffer,
                 oled->transfer_length,
                 100U) != HAL_OK)) {
            OLED_SetTransportError(oled);
            return 0U;
        }

        oled->transfer_length = 0U;
        return 1U;

    default:
        return 0U;
    }
}

static uint8_t OLED_GPIOAndDelayCallback(u8x8_t *u8x8,
                                         uint8_t message,
                                         uint8_t argument,
                                         void *data)
{
    (void)u8x8;
    (void)data;

    switch (message) {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        return 1U;

    case U8X8_MSG_DELAY_MILLI:
        HAL_Delay(argument);
        return 1U;

    /*
     * This four-pin OLED module has no reset connection. U8g2's logical
     * reset toggles are intentionally ignored; its millisecond delays still
     * provide the controller's required power-on settling time.
     */
    case U8X8_MSG_GPIO_RESET:
        return 1U;

    default:
        return 0U;
    }
}

static OLED_Handle *OLED_FromU8x8(u8x8_t *u8x8)
{
    if (u8x8 == NULL) {
        return NULL;
    }

    return (OLED_Handle *)u8x8_GetUserPtr(u8x8);
}

static void OLED_SetTransportError(OLED_Handle *oled)
{
    oled->transport_ok = 0U;
    oled->transfer_active = 0U;
    oled->transfer_length = 0U;
}
