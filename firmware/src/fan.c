// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "fan.h"

#include "adc.h"
#include "pins.h"
#include "stm32f0xx_hal.h"
#include "systick.h"

#include <stdbool.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define FAN_START_DELAY_MS    5000
#define FAN_RUN_TEMPERATURE_C 50

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static bool     fan_enabled;
static bool     fan_pin_initialized;
static uint32_t fan_last_wake_ms;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void fan_pin_init(void);
static bool fan_should_run(void);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void fan_init(void)
{
    fan_enabled = true;
    fan_on_wake();
}

void fan_task(void)
{
    bool should_run;

    if (!fan_enabled)
    {
        return;
    }

    if ((uint32_t)(systick_get_ms() - fan_last_wake_ms) < FAN_START_DELAY_MS)
    {
        return;
    }

    should_run = fan_should_run();

    /*
     * PA13 is also SWDIO. Leave it completely untouched until the fan
     * actually needs to run.
     */
    if (should_run && !fan_pin_initialized)
    {
        fan_pin_init();
    }

    if (!fan_pin_initialized)
    {
        return;
    }

    HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, should_run ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void fan_on_wake(void)
{
    fan_last_wake_ms = systick_get_ms();
}

void fan_stop(void)
{
    fan_enabled = false;

    /* An uninitialized PA13 still belongs to SWD and the fan has never been
     * started, so do not take ownership of the pin merely to stop it. */
    if (fan_pin_initialized)
    {
        HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, GPIO_PIN_RESET);
    }
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static void fan_pin_init(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*
     * Preload the output latch low before taking PA13 away from SWD. This
     * prevents a transient pulse at the fan MOSFET gate.
     */
    HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, GPIO_PIN_RESET);

    gpio_cfg.Pin   = FAN_PINn;
    gpio_cfg.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_cfg.Pull  = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(FAN_GPIOx, &gpio_cfg);

    fan_pin_initialized = true;
}

static bool fan_should_run(void)
{
    return (adc_to_celcius(THERM_1_IDX) > FAN_RUN_TEMPERATURE_C) ||
           (adc_to_celcius(THERM_2_IDX) > FAN_RUN_TEMPERATURE_C) ||
           (adc_to_celcius(MCU_TEMP_IDX) > FAN_RUN_TEMPERATURE_C);
}
