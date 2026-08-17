/*
Fan control state machine
Determines when to turn on the fan based on the configured mode and the measured temperatures
*/

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "fan.h"

#include "adc.h"
#include "conf.h"
#include "fault.h"
#include "pins.h"
#include "stm32f0xx_hal.h"
#include "systick.h"

#include <stdbool.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define NTC_CHECK_DELAY_MS            10000
#define NTC_FAULT_MESSAGE_DURATION_MS 3000

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static bool     fan_enabled;
static bool     fan_running;
static bool     fan_pin_initialized;
static uint32_t fan_last_transition_ms;
static uint8_t  fan_mode;

#if NTC_FAULT_WARNING_ENABLED
static bool     ntc_task_started;
static bool     ntc_check_complete;
static uint32_t ntc_task_started_ms;
#endif

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
    bool     should_run;
    uint32_t now;

    if (!fan_enabled)
    {
        return;
    }

    now        = systick_get_ms();
    should_run = fan_should_run();

    /* Hold each commanded state long enough to prevent rapid, irritating
     * cycling around a temperature threshold. Unsigned subtraction keeps the
     * elapsed-time check valid when the millisecond tick wraps around. */
    if (fan_running && !should_run && ((uint32_t)(now - fan_last_transition_ms) < FAN_MINIMUM_ON_TIME_MS))
    {
        return;
    }

    if (!fan_running && should_run && ((uint32_t)(now - fan_last_transition_ms) < FAN_MINIMUM_OFF_TIME_MS))
    {
        return;
    }

    /* There is no transition to apply, so avoid repeatedly writing the same
     * GPIO state and leave the transition timestamp unchanged. */
    if (should_run == fan_running)
    {
        return;
    }

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
    fan_running            = should_run;
    fan_last_transition_ms = now;
}

void ntc_task(void)
{
#if NTC_FAULT_WARNING_ENABLED
    uint32_t now;

    if (ntc_check_complete)
    {
        return;
    }

    now = systick_get_ms();
    if (!ntc_task_started)
    {
        ntc_task_started    = true;
        ntc_task_started_ms = now;
        return;
    }

    if ((uint32_t)(now - ntc_task_started_ms) < NTC_CHECK_DELAY_MS)
    {
        return;
    }

    ntc_check_complete = true;
    /* The ADC temperature conversion returns zero for an unavailable or
     * disconnected external NTC. By this point every ADC input has had ample
     * time to initialize. */
    if ((adc_to_celcius(THERM_1_IDX) == 0) || (adc_to_celcius(THERM_2_IDX) == 0))
    {
        show_short_msg("NTC\nSENS\nFAULT", NTC_FAULT_MESSAGE_DURATION_MS);
    }
#endif
}

void fan_on_wake(void)
{
    /* Initialization begins in the off state. Starting its dwell interval here
     * also preserves the configured startup delay before PA13 is claimed. */
    fan_last_transition_ms = systick_get_ms();
}

void fan_stop(void)
{
    bool was_running = fan_running;

    fan_enabled = false;
    fan_running = false;

    /* An uninitialized PA13 still belongs to SWD and the fan has never been
     * started, so do not take ownership of the pin merely to stop it. */
    if (fan_pin_initialized)
    {
        HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, GPIO_PIN_RESET);
    }

    /* Explicit safety and fault stops override the minimum-on time. If the fan
     * was running, still begin a full minimum-off interval so fan_resume()
     * cannot immediately cycle it back on. */
    if (was_running)
    {
        fan_last_transition_ms = systick_get_ms();
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
     * until every monitored temperature falls by the configured hysteresis. */
    temperature_limit_c = fan_running ? (turn_on_temperature_c - TEMPERATURE_HYSTERYSIS_C) : turn_on_temperature_c;
    return fan_any_temperature_exceeds(temperature_limit_c);
}

static bool fan_any_temperature_exceeds(uint16_t temperature_c)
{
    return (adc_to_celcius(THERM_1_IDX) > temperature_c) || (adc_to_celcius(THERM_2_IDX) > temperature_c) ||
           (adc_to_celcius(MCU_TEMP_IDX) > temperature_c);
}
