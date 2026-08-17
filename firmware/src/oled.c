// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "oled.h"

#include "miscutils.h"
#include "splash.h"
#include "systick.h"

#include <string.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

/* The analog filter stays enabled and the digital filter stays disabled. */
/* Values below are TIMINGR field encodings, not register bit masks. */
#define I2C_HSE_CLOCK_HZ 27120000

#define I2C_HSI_FAST_MODE_PRESCALER       0
#define I2C_HSI_FAST_MODE_DATA_SETUP_TIME 1
#define I2C_HSI_FAST_MODE_DATA_HOLD_TIME  0
#define I2C_HSI_FAST_MODE_SCL_HIGH_PERIOD 2
#define I2C_HSI_FAST_MODE_SCL_LOW_PERIOD  10

#define I2C_HSE_FAST_MODE_PRESCALER       0
#define I2C_HSE_FAST_MODE_DATA_SETUP_TIME 9
#define I2C_HSE_FAST_MODE_DATA_HOLD_TIME  0
#define I2C_HSE_FAST_MODE_SCL_HIGH_PERIOD 18
#define I2C_HSE_FAST_MODE_SCL_LOW_PERIOD  31

#define I2C_TIMING_FIELD(VALUE, FIELD) ((((uint32_t)(VALUE)) << FIELD##_Pos) & FIELD##_Msk)

/* 400 kHz fast mode using the reset-default 8 MHz HSI I2C kernel clock. */
#define I2C_TIMING_400KHZ_AT_8MHZ                                                                                      \
    (I2C_TIMING_FIELD(I2C_HSI_FAST_MODE_PRESCALER, I2C_TIMINGR_PRESC) |                                                \
     I2C_TIMING_FIELD(I2C_HSI_FAST_MODE_DATA_SETUP_TIME, I2C_TIMINGR_SCLDEL) |                                         \
     I2C_TIMING_FIELD(I2C_HSI_FAST_MODE_DATA_HOLD_TIME, I2C_TIMINGR_SDADEL) |                                          \
     I2C_TIMING_FIELD(I2C_HSI_FAST_MODE_SCL_HIGH_PERIOD, I2C_TIMINGR_SCLH) |                                           \
     I2C_TIMING_FIELD(I2C_HSI_FAST_MODE_SCL_LOW_PERIOD, I2C_TIMINGR_SCLL))

/* 400 kHz fast mode using the confirmed 27.12 MHz HSE system clock. */
#define I2C_TIMING_400KHZ_AT_27_12MHZ                                                                                  \
    (I2C_TIMING_FIELD(I2C_HSE_FAST_MODE_PRESCALER, I2C_TIMINGR_PRESC) |                                                \
     I2C_TIMING_FIELD(I2C_HSE_FAST_MODE_DATA_SETUP_TIME, I2C_TIMINGR_SCLDEL) |                                         \
     I2C_TIMING_FIELD(I2C_HSE_FAST_MODE_DATA_HOLD_TIME, I2C_TIMINGR_SDADEL) |                                          \
     I2C_TIMING_FIELD(I2C_HSE_FAST_MODE_SCL_HIGH_PERIOD, I2C_TIMINGR_SCLH) |                                           \
     I2C_TIMING_FIELD(I2C_HSE_FAST_MODE_SCL_LOW_PERIOD, I2C_TIMINGR_SCLL))

_Static_assert(HSE_VALUE == I2C_HSE_CLOCK_HZ, "I2C HSE timing must be recalculated for a different crystal");

#define OLED_I2C_TIMEOUT_MS 100

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

I2C_HandleTypeDef i2c1;
OLED_Handle       oled;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static uint8_t      OLED_I2CByteCallback(u8x8_t* u8x8, uint8_t message, uint8_t argument, void* data);
static uint8_t      OLED_GPIOAndDelayCallback(u8x8_t* u8x8, uint8_t message, uint8_t argument, void* data);
static OLED_Handle* OLED_FromU8x8(u8x8_t* u8x8);
static bool         OLED_I2CTransmit(uint16_t address, const uint8_t* data, uint8_t length);
static bool         OLED_I2CWaitFor(uint32_t flag);
static void         OLED_I2CClearErrors(void);
static void         OLED_SetTransportError(OLED_Handle* oled);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void I2C1_Init(void)
{
    uint32_t timing;

    /* HSERDY alone does not prove that the system-clock switch completed. */
    if ((__HAL_RCC_GET_FLAG(RCC_FLAG_HSERDY) != RESET) &&
        (__HAL_RCC_GET_SYSCLK_SOURCE() == RCC_SYSCLKSOURCE_STATUS_HSE))
    {
        __HAL_RCC_I2C1_CONFIG(RCC_I2C1CLKSOURCE_SYSCLK);
        timing = I2C_TIMING_400KHZ_AT_27_12MHZ;
    }
    else
    {
        __HAL_RCC_I2C1_CONFIG(RCC_I2C1CLKSOURCE_HSI);
        timing = I2C_TIMING_400KHZ_AT_8MHZ;
    }

    i2c1.Instance = I2C1;
    HAL_I2C_MspInit(&i2c1);

    CLEAR_BIT(I2C1->CR1, I2C_CR1_PE);
    I2C1->CR1     = 0;
    I2C1->CR2     = 0;
    I2C1->OAR1    = 0;
    I2C1->OAR2    = 0;
    I2C1->TIMINGR = timing;
    OLED_I2CClearErrors();
    SET_BIT(I2C1->CR1, I2C_CR1_PE);
}

bool OLED_Init(OLED_Handle* oled, I2C_HandleTypeDef* i2c)
{
    if ((oled == NULL) || (i2c == NULL))
    {
        return false;
    }

    memset(oled, 0, sizeof(*oled));
    oled->i2c          = i2c;
    oled->transport_ok = 1;

    u8g2_Setup_ssd1306_i2c_128x32_univision_f(&oled->graphics,
                                              U8G2_R0,
                                              OLED_I2CByteCallback,
                                              OLED_GPIOAndDelayCallback);
    u8g2_SetUserPtr(&oled->graphics, oled);

    /* U8g2 and STM32 HAL both use the left-shifted form of a 7-bit I2C address
     * here. The SSD1306 address 0x3C is therefore passed as 0x78. */
    u8g2_SetI2CAddress(&oled->graphics, OLED_I2C_ADDRESS_U8G2);

    u8g2_InitDisplay(&oled->graphics);
    if (oled->transport_ok == 0)
    {
        return false;
    }

    u8g2_SetPowerSave(&oled->graphics, 0);
    if (oled->transport_ok == 0)
    {
        return false;
    }

    oled->initialized = 1;
    return true;
}

void OLED_ConfigureGraphics(OLED_Handle* oled)
{
    if ((oled == NULL) || (oled->initialized == 0))
    {
        return;
    }

    /* These drawing properties persist until explicitly changed. */
    u8g2_SetDisplayRotation(&oled->graphics, U8G2_R1);
    u8g2_SetFont(&oled->graphics, u8g2_font_5x7_tr);
    u8g2_ClearBuffer(&oled->graphics);
}

bool OLED_SendBuffer(OLED_Handle* oled)
{
    if ((oled == NULL) || (oled->initialized == 0))
    {
        return false;
    }

    oled->transport_ok = 1;
    u8g2_SendBuffer(&oled->graphics);
    return oled->transport_ok != 0;
}

void show_splash(void)
{
    u8g2_t* graphics = OLED_GetGraphics(&oled);
    uint8_t splash_index;

    if (graphics == NULL)
    {
        return;
    }

    splash_index = (uint8_t)(hotwand_rand() % SPLASH_SCREEN_COUNT);

    u8g2_ClearBuffer(graphics);
    u8g2_SetDrawColor(graphics, 1);
    u8g2_SetBitmapMode(graphics, 0);
    u8g2_DrawXBMP(graphics, 0, 0, SPLASH_SCREEN_WIDTH, SPLASH_SCREEN_HEIGHT, splash_screens[splash_index]);
    OLED_SendBuffer(&oled);
}

// -----------------------------------------------------------------------------
// Getters and Setters
// -----------------------------------------------------------------------------

u8g2_t* OLED_GetGraphics(OLED_Handle* oled)
{
    if ((oled == NULL) || (oled->initialized == 0))
    {
        return NULL;
    }

    return &oled->graphics;
}

bool OLED_SetDimMode(OLED_Handle* oled, bool dimmed)
{
    uint8_t requested = dimmed ? 1 : 0;

    if ((oled == NULL) || (oled->initialized == 0))
    {
        return false;
    }

    if (oled->dimmed == requested)
    {
        return true;
    }

    oled->transport_ok = 1;
    u8g2_SetContrast(&oled->graphics, dimmed ? OLED_DIM_CONTRAST : UINT8_MAX);
    if (oled->transport_ok == 0)
    {
        return false;
    }

    oled->dimmed = requested;
    return true;
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void HAL_I2C_MspInit(I2C_HandleTypeDef* handle)
{
    GPIO_InitTypeDef gpio = {0};

    if (handle->Instance != I2C1)
    {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    /*
     * STM32F030F4P6 / STM32F042F6P6 TSSOP-20:
     *   PA9  (pin 17) -> I2C1_SCL
     *   PA10 (pin 18) -> I2C1_SDA
     * External or OLED-module pull-up resistors are required.
     */
    gpio.Pin       = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode      = GPIO_MODE_AF_OD;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOA, &gpio);
}

static uint8_t OLED_I2CByteCallback(u8x8_t* u8x8, uint8_t message, uint8_t argument, void* data)
{
    OLED_Handle* oled = OLED_FromU8x8(u8x8);

    if ((oled == NULL) || (oled->i2c == NULL))
    {
        return 0;
    }

    switch (message)
    {
    case U8X8_MSG_BYTE_INIT:
    case U8X8_MSG_BYTE_SET_DC:
        return 1;

    case U8X8_MSG_BYTE_START_TRANSFER:
        oled->transfer_length = 0;
        oled->transfer_active = 1;
        return 1;

    case U8X8_MSG_BYTE_SEND:
        if ((oled->transfer_active == 0) || ((argument > 0) && (data == NULL)) ||
            (argument > (uint8_t)(OLED_I2C_TRANSFER_CAPACITY - oled->transfer_length)))
        {
            OLED_SetTransportError(oled);
            return 0;
        }

        if (argument > 0)
        {
            memcpy(&oled->transfer_buffer[oled->transfer_length], data, argument);
            oled->transfer_length = (uint8_t)(oled->transfer_length + argument);
        }
        return 1;

    case U8X8_MSG_BYTE_END_TRANSFER:
        if (oled->transfer_active == 0)
        {
            OLED_SetTransportError(oled);
            return 0;
        }

        oled->transfer_active = 0;
        if (oled->transport_ok == 0)
        {
            oled->transfer_length = 0;
            return 0;
        }

        if ((oled->transfer_length > 0) &&
            !OLED_I2CTransmit((uint16_t)u8x8_GetI2CAddress(u8x8), oled->transfer_buffer, oled->transfer_length))
        {
            OLED_SetTransportError(oled);
            return 0;
        }

        oled->transfer_length = 0;
        return 1;

    default:
        return 0;
    }
}

static uint8_t OLED_GPIOAndDelayCallback(u8x8_t* u8x8, uint8_t message, uint8_t argument, void* data)
{
    (void)u8x8;
    (void)data;

    switch (message)
    {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        return 1;

    case U8X8_MSG_DELAY_MILLI:
        HAL_Delay(argument);
        return 1;

    /* This four-pin OLED module has no reset connection. U8g2's logical reset
     * toggles are intentionally ignored; its millisecond delays still provide
     * the controller's required power-on settling time. */
    case U8X8_MSG_GPIO_RESET:
        return 1;

    default:
        return 0;
    }
}

static OLED_Handle* OLED_FromU8x8(u8x8_t* u8x8)
{
    if (u8x8 == NULL)
    {
        return NULL;
    }

    return (OLED_Handle*)u8x8_GetUserPtr(u8x8);
}

static bool OLED_I2CTransmit(uint16_t address, const uint8_t* data, uint8_t length)
{
    OLED_I2CClearErrors();
    I2C1->CR2 = (uint32_t)address | ((uint32_t)length << I2C_CR2_NBYTES_Pos) | I2C_CR2_AUTOEND | I2C_CR2_START;

    while (length-- != 0)
    {
        if (!OLED_I2CWaitFor(I2C_ISR_TXIS))
        {
            OLED_I2CClearErrors();
            return false;
        }
        I2C1->TXDR = *data++;
    }

    if (!OLED_I2CWaitFor(I2C_ISR_STOPF))
    {
        OLED_I2CClearErrors();
        return false;
    }
    WRITE_REG(I2C1->ICR, I2C_ICR_STOPCF);
    return true;
}

static bool OLED_I2CWaitFor(uint32_t flag)
{
    uint32_t start = systick_get_ms();
    uint32_t status;

    do
    {
        status = I2C1->ISR;
        if ((status & (I2C_ISR_NACKF | I2C_ISR_BERR | I2C_ISR_ARLO | I2C_ISR_OVR)) != 0)
        {
            return false;
        }

        if ((status & flag) != 0)
        {
            return true;
        }
    } while ((uint32_t)(systick_get_ms() - start) < OLED_I2C_TIMEOUT_MS);

    return false;
}

static void OLED_I2CClearErrors(void)
{
    SET_BIT(I2C1->CR2, I2C_CR2_STOP);
    WRITE_REG(I2C1->ICR,
              I2C_ICR_ADDRCF | I2C_ICR_NACKCF | I2C_ICR_STOPCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF |
                  I2C_ICR_PECCF | I2C_ICR_TIMOUTCF | I2C_ICR_ALERTCF);
}

static void OLED_SetTransportError(OLED_Handle* oled)
{
    oled->transport_ok    = 0;
    oled->transfer_active = 0;
    oled->transfer_length = 0;
}
