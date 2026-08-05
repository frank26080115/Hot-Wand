// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "fan.h"

#include "adc.h"
#include "conf.h"
#include "pins.h"
#include "stm32f0xx_hal.h"
#include "systick.h"

#include <stdbool.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define FAN_START_DELAY_MS 5000

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static bool     fan_enabled;
static bool     fan_running;
static bool     fan_pin_initialized;
static uint32_t fan_last_wake_ms;
static uint8_t  fan_mode;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void fan_pin_init(void);
static bool fan_should_run(void);
static bool fan_any_temperature_exceeds(uint16_t temperature_c);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void fan_init(uint8_t mode)
{
    fan_enabled = true;
    fan_running = false;
    fan_mode    = mode;
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
    fan_running = should_run;
}

void fan_on_wake(void)
{
    fan_last_wake_ms = systick_get_ms();
}

void fan_stop(void)
{
    fan_enabled = false;
    fan_running = false;

    /* An uninitialized PA13 still belongs to SWD and the fan has never been
     * started, so do not take ownership of the pin merely to stop it. */
    if (fan_pin_initialized)
    {
        HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, GPIO_PIN_RESET);
    }
}

void fan_resume(void)
{
    fan_enabled = true;
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
    uint16_t turn_on_temperature_c;
    uint16_t temperature_limit_c;

    switch (fan_mode)
    {
    case FAN_MODE_ON:
        return true;

    case FAN_MODE_AUTO_LOW:
        turn_on_temperature_c = TEMPERATURE_FAN_THRESHOLD_LOW_C;
        break;

    case FAN_MODE_AUTO_HIGH:
        turn_on_temperature_c = TEMPERATURE_FAN_THRESHOLD_HIGH_C;
        break;

    case FAN_MODE_OFF:
    default:
        return false;
    }

    /* Automatic modes turn on above their selected threshold, then remain on
     * until every monitored temperature
     * falls by the configured hysteresis. */
    temperature_limit_c = fan_running ? (turn_on_temperature_c - TEMPERATURE_HYSTERYSIS_C) : turn_on_temperature_c;
    return fan_any_temperature_exceeds(temperature_limit_c);
}

static bool fan_any_temperature_exceeds(uint16_t temperature_c)
{
    return (adc_to_celcius(THERM_1_IDX) > temperature_c) || (adc_to_celcius(THERM_2_IDX) > temperature_c) ||
           (adc_to_celcius(MCU_TEMP_IDX) > temperature_c);
}
