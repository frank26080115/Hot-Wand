/*
 * The UART is used only to debug the device and is not required for normal
 * operation. Debug output is disabled by default and is activated only when
 * the button is held during power-on.
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "uart_debug.h"
#include "adc.h"
#include "miscutils.h"
#include "pins.h"
#include "pwrmgt.h"
#include "systick.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define UART_DEBUG_TASK_PERIOD_MS 200
#define UART_DEBUG_VALUE_CAPACITY 8

/* PA14/AF1 is not routed to the same peripheral on these pin-compatible MCUs:
 *   STM32F030F4/F6: USART1_TX
 *   STM32F042F4/F6: USART2_TX
 *
 * Configuring the wrong USART still changes PA14 from SWCLK to AF1 (and thus
 * disconnects SWD), but no UART data reaches the pin. Keep the instance,
 * peripheral clock, and GPIO alternate-function selection paired below. */
#if defined(HOT_WAND_TARGET_STM32F042)
#define UART_TX_INSTANCE USART2
#define UART_TX_GPIO_AF  GPIO_AF1_USART2
#elif defined(HOT_WAND_TARGET_STM32F030)
#define UART_TX_INSTANCE USART1
#define UART_TX_GPIO_AF  GPIO_AF1_USART1
#else
#error "Debug UART mapping is missing for the selected Hot Wand target"
#endif

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static UART_HandleTypeDef uart_tx;
static bool               uart_allowed;
static bool               uart_initialized;
static uint32_t           uart_debug_last_task_ms;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static bool UART_TX_Init(void);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void UART_Write(const char* text)
{
    size_t length;

    if (!uart_allowed || (text == NULL))
    {
        return;
    }

    length = strlen(text);
    if ((length == 0) || (length > UINT16_MAX))
    {
        return;
    }

    if (!uart_initialized && !UART_TX_Init())
    {
        return;
    }

    if (HAL_UART_Transmit(&uart_tx, (const uint8_t*)text, (uint16_t)length, HAL_MAX_DELAY) != HAL_OK)
    {
        return;
    }
}

void UART_debug_task(void)
{
    char     timestamp[12];
    char     value[UART_DEBUG_VALUE_CAPACITY];
    uint16_t reference_millivolts;
    uint32_t now;

    if (!uart_allowed)
    {
        return;
    }

    now = systick_get_ms();
    if ((uint32_t)(now - uart_debug_last_task_ms) < UART_DEBUG_TASK_PERIOD_MS)
    {
        return;
    }
    uart_debug_last_task_ms = now;

    UART_Write("[");
    UART_Write(int_to_str((int)now, timestamp, 10, NULL));
    UART_Write("]: ");

    millivolts_to_str(adc_to_millivolts(DC_SENS_IDX), value, 1, NULL);
    UART_Write(value);
    UART_Write("V ");

    millivolts_to_str(adc_to_millivolts(BUCK_SENS_IDX), value, 1, NULL);
    UART_Write(value);
    UART_Write("V ");

    UART_Write("VREFINT=");
    if (adc_get_reference_millivolts(&reference_millivolts))
    {
        millivolts_to_str(reference_millivolts, value, 2, NULL);
        UART_Write(value);
        UART_Write("V ");
    }
    else
    {
        UART_Write("NA ");
    }

    milliamps_to_str(adc_to_milliamps(CURR_SENS_IDX), value, 2, NULL);
    UART_Write(value);
    UART_Write("A ");

    celcius_to_str((int)adc_to_celcius(THERM_1_IDX), value, NULL);
    UART_Write(value);
    UART_Write("C ");

    celcius_to_str((int)adc_to_celcius(THERM_2_IDX), value, NULL);
    UART_Write(value);
    UART_Write("C ");

    celcius_to_str((int)adc_to_celcius(MCU_TEMP_IDX), value, NULL);
    UART_Write(value);
    UART_Write("C ");

    switch (pwrmgt_get_applied_power_level())
    {
    case PWRLVL_MODE_50_PERCENT:
        UART_Write("50W ");
        break;
    case PWRLVL_MODE_75_PERCENT:
        UART_Write("75W ");
        break;
    case PWRLVL_MODE_100_PERCENT:
    default:
        UART_Write("100W ");
        break;
    }

    int_to_str((int)pwrmgt_get_attenuation_reasons(), value, 16, NULL);
    UART_Write("0x");
    UART_Write(value);
    UART_Write("\r\n");
}

// -----------------------------------------------------------------------------
// Getters and Setters
// -----------------------------------------------------------------------------

void UART_SetAllowed(bool allowed)
{
#if defined(HOT_WAND_SWD_DEBUG) && HOT_WAND_SWD_DEBUG
    /* UART TX shares PA14 with SWCLK. A debugger build must never hand the
     * pin to a USART, even if a test or the boot button requests output. */
    (void)allowed;
    uart_allowed = false;
#else
    uart_allowed = allowed;
#endif
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void HAL_UART_MspInit(UART_HandleTypeDef* handle)
{
    GPIO_InitTypeDef gpio = {0};

    if (handle->Instance != UART_TX_INSTANCE)
    {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
#if defined(HOT_WAND_TARGET_STM32F042)
    __HAL_RCC_USART2_CLK_ENABLE();
#else
    __HAL_RCC_USART1_CLK_ENABLE();
#endif

    gpio.Pin       = UART_TX_PINn;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = UART_TX_GPIO_AF;
    HAL_GPIO_Init(UART_TX_GPIOx, &gpio);
}

static bool UART_TX_Init(void)
{
    uart_tx.Instance                    = UART_TX_INSTANCE;
    uart_tx.Init.BaudRate               = 115200;
    uart_tx.Init.WordLength             = UART_WORDLENGTH_8B;
    uart_tx.Init.StopBits               = UART_STOPBITS_1;
    uart_tx.Init.Parity                 = UART_PARITY_NONE;
    uart_tx.Init.Mode                   = UART_MODE_TX;
    uart_tx.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
    uart_tx.Init.OverSampling           = UART_OVERSAMPLING_16;
    uart_tx.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
    uart_tx.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&uart_tx) != HAL_OK)
    {
        return false;
    }

    uart_initialized = true;
    return true;
}

// -----------------------------------------------------------------------------
// Debug / Fault Helpers
// -----------------------------------------------------------------------------
