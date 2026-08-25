/*
 * Fan and external-NTC control
 *
 * The normal build provides sixteen fixed, binary, and temperature-adaptive
 * fan profiles through PA13's hardware PWM path. FAN_PWM_ENABLED=0 compiles a
 * smaller direct-GPIO controller containing only the four useful binary modes.
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "fan.h"

#include "adc.h"
#include "conf.h"
#include "fault.h"
#if FAN_PWM_ENABLED
#include "fan_pwm.h"
#endif
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

#if FAN_PWM_ENABLED
#define FAN_PERCENT_SCALE_MILLI 1000
#define FAN_FULL_DUTY_PERCENT   100
#define FAN_FULL_DUTY_MILLI     (FAN_FULL_DUTY_PERCENT * FAN_PERCENT_SCALE_MILLI)
#define FAN_ADAPTIVE_RANGE_C    40
#define FAN_ADAPTIVE_RANGE_DUTY (FAN_FULL_DUTY_PERCENT - FAN_MINIMUM_PWM_PERCENT)
#define FAN_ADAPTIVE_GAIN_SCALE 100
#define FAN_ADAPTIVE_ROUNDED_DUTY(temperature_c, gain_percent)                                                         \
    (FAN_MINIMUM_PWM_PERCENT +                                                                                         \
     (((((temperature_c) - FAN_ADAPTIVE_ON_TEMPERATURE_C) * FAN_ADAPTIVE_RANGE_DUTY * (gain_percent)) +                \
       ((FAN_ADAPTIVE_RANGE_C * FAN_ADAPTIVE_GAIN_SCALE) / 2)) /                                                       \
      (FAN_ADAPTIVE_RANGE_C * FAN_ADAPTIVE_GAIN_SCALE)))

_Static_assert(FAN_ADAPTIVE_ROUNDED_DUTY(80, 25) == 44, "25 percent adaptive gain changed");
_Static_assert(FAN_ADAPTIVE_ROUNDED_DUTY(80, 50) == 63, "50 percent adaptive gain changed");
_Static_assert(FAN_ADAPTIVE_ROUNDED_DUTY(80, 75) == 81, "75 percent adaptive gain changed");
_Static_assert(FAN_ADAPTIVE_ROUNDED_DUTY(80, 100) == 100, "100 percent adaptive gain changed");
_Static_assert(FAN_ADAPTIVE_ROUNDED_DUTY(80, 150) > 100, "150 percent adaptive gain must clamp");

typedef enum
{
    FAN_CONTROLLER_OFF = 0,
    FAN_CONTROLLER_FIXED,
    FAN_CONTROLLER_BINARY,
    FAN_CONTROLLER_ADAPTIVE,
} fan_controller_t;

typedef struct
{
    fan_controller_t controller;
    uint8_t          active_value;
    uint8_t          activation_temperature_c;
    bool             ramp_enabled;
} fan_profile_t;

/* The array index is the persistent FAN_MODE_* value. For fixed and binary
 * profiles, active_value is duty percent. For adaptive profiles, it is gain
 * percent. A zero activation temperature marks a non-automatic profile. */
static const fan_profile_t fan_profiles[FAN_MODE_LAST + 1] = {
    [FAN_MODE_OFF]                   = {FAN_CONTROLLER_OFF,      0,   0,                             false},
    [FAN_MODE_ON_100_RAMPED]         = {FAN_CONTROLLER_FIXED,    100, 0,                             true },
    [FAN_MODE_ON_25]                 = {FAN_CONTROLLER_FIXED,    25,  0,                             false},
    [FAN_MODE_ON_50]                 = {FAN_CONTROLLER_FIXED,    50,  0,                             false},
    [FAN_MODE_ON_75]                 = {FAN_CONTROLLER_FIXED,    75,  0,                             false},
    [FAN_MODE_AUTO_BINARY_100_COOL]  = {FAN_CONTROLLER_BINARY,   100, FAN_COOL_ON_TEMPERATURE_C,     true },
    [FAN_MODE_AUTO_BINARY_100_QUIET] = {FAN_CONTROLLER_BINARY,   100, FAN_QUIET_ON_TEMPERATURE_C,    true },
    [FAN_MODE_AUTO_BINARY_50_COOL]   = {FAN_CONTROLLER_BINARY,   50,  FAN_COOL_ON_TEMPERATURE_C,     false},
    [FAN_MODE_AUTO_BINARY_50_QUIET]  = {FAN_CONTROLLER_BINARY,   50,  FAN_QUIET_ON_TEMPERATURE_C,    false},
    [FAN_MODE_AUTO_BINARY_25_COOL]   = {FAN_CONTROLLER_BINARY,   25,  FAN_COOL_ON_TEMPERATURE_C,     false},
    [FAN_MODE_AUTO_BINARY_25_QUIET]  = {FAN_CONTROLLER_BINARY,   25,  FAN_QUIET_ON_TEMPERATURE_C,    false},
    [FAN_MODE_AUTO_ADAPTIVE_25]      = {FAN_CONTROLLER_ADAPTIVE, 25,  FAN_ADAPTIVE_ON_TEMPERATURE_C, false},
    [FAN_MODE_AUTO_ADAPTIVE_50]      = {FAN_CONTROLLER_ADAPTIVE, 50,  FAN_ADAPTIVE_ON_TEMPERATURE_C, false},
    [FAN_MODE_AUTO_ADAPTIVE_75]      = {FAN_CONTROLLER_ADAPTIVE, 75,  FAN_ADAPTIVE_ON_TEMPERATURE_C, false},
    [FAN_MODE_AUTO_ADAPTIVE_100]     = {FAN_CONTROLLER_ADAPTIVE, 100, FAN_ADAPTIVE_ON_TEMPERATURE_C, false},
    [FAN_MODE_AUTO_ADAPTIVE_150]     = {FAN_CONTROLLER_ADAPTIVE, 150, FAN_ADAPTIVE_ON_TEMPERATURE_C, false},
};

_Static_assert((sizeof(fan_profiles) / sizeof(fan_profiles[0])) == (FAN_MODE_LAST + 1),
               "Every fan mode must have a profile");
#endif

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static bool     fan_enabled;
static bool     fan_output_initialized;
static bool     fan_startup_hold_pending;
static uint8_t  fan_mode;
static uint32_t fan_startup_hold_started_ms;

#if FAN_PWM_ENABLED
static bool     fan_signal_inverted;
static bool     fan_automatic_active;
static bool     fan_zero_reached;
static bool     fan_startup_boost_active;
static uint8_t  fan_commanded_duty_percent;
static uint8_t  fan_requested_duty_percent;
static uint8_t  fan_adaptive_duty_percent;
static uint32_t fan_current_duty_millipercent;
static uint32_t fan_last_task_ms;
static uint32_t fan_automatic_active_since_ms;
static uint32_t fan_zero_reached_ms;
static uint32_t fan_startup_boost_started_ms;
static uint32_t fan_adaptive_updated_ms;
#else
static bool     fan_running;
static uint32_t fan_last_transition_ms;
#endif

#if NTC_FAULT_WARNING_ENABLED
static bool     ntc_task_started;
static bool     ntc_check_complete;
static uint32_t ntc_task_started_ms;
#endif

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static uint16_t fan_maximum_temperature_c(void);
static void     fan_reset_controller(uint32_t now);
static bool     fan_finish_startup_hold(uint32_t now);

#if FAN_PWM_ENABLED
static uint8_t fan_automatic_target(const fan_profile_t* profile, uint16_t temperature_c, uint32_t now);
static uint8_t fan_adaptive_duty(uint16_t temperature_c, uint8_t gain_percent);
static uint8_t fan_apply_startup_policy(const fan_profile_t* profile, uint8_t requested_duty_percent, uint32_t now);
static void    fan_apply_ramp(const fan_profile_t* profile, uint8_t target_duty_percent, uint32_t now);
static bool    fan_output_initialize(void);
static void    fan_output_set(uint8_t duty_percent);
#else
static void fan_pin_init(void);
static void fan_output_set(bool running);
#endif

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

uint8_t fan_normalize_mode(uint8_t mode)
{
#if FAN_PWM_ENABLED
    return mode <= FAN_MODE_LAST ? mode : FAN_MODE_AUTO_BINARY_100_COOL;
#else
    switch (mode)
    {
    case FAN_MODE_OFF:
    case FAN_MODE_ON_100_RAMPED:
    case FAN_MODE_AUTO_BINARY_100_COOL:
    case FAN_MODE_AUTO_BINARY_100_QUIET:
        return mode;

    default:
        return FAN_MODE_AUTO_BINARY_100_COOL;
    }
#endif
}

void fan_init(uint8_t mode, bool signal_inverted)
{
    uint32_t now = systick_get_ms();

#if FAN_PWM_ENABLED
    fan_signal_inverted = signal_inverted;
    if (fan_output_initialized)
    {
        fanpwm_mode_t pwm_mode = fan_signal_inverted ? FANPWM_MODE_EXTERNAL_MOSFET : FANPWM_MODE_DIRECT;

        /* PA13 cannot be returned to SWD without reset. If this API is used to
         * reconfigure a claimed output, safely reinitialize its polarity at
         * zero duty before the new profile starts. */
        fanpwm_set(0);
        fan_output_initialized = fanpwm_init(pwm_mode);
    }
#else
    /* The fallback build supports only an active-high, push-pull GPIO. */
    (void)signal_inverted;
    fan_output_set(false);
#endif

    fan_mode    = fan_normalize_mode(mode);
    fan_enabled = true;
    fan_reset_controller(now);
    fan_startup_hold_started_ms = now;
    fan_startup_hold_pending    = true;

#if FAN_STARTUP_OFF_TIME_MS == 0
    if (!fan_finish_startup_hold(now))
    {
        return;
    }
#endif
}

void fan_task(void)
{
    uint32_t now;

    if (!fan_enabled)
    {
        return;
    }

    now = systick_get_ms();

    if (fan_startup_hold_pending)
    {
        if ((uint32_t)(now - fan_startup_hold_started_ms) < FAN_STARTUP_OFF_TIME_MS)
        {
            return;
        }

        if (!fan_finish_startup_hold(now))
        {
            return;
        }
    }

#if FAN_PWM_ENABLED
    {
        const fan_profile_t* profile = &fan_profiles[fan_mode];
        uint8_t              requested_duty_percent;
        uint8_t              target_duty_percent;

        switch (profile->controller)
        {
        case FAN_CONTROLLER_FIXED:
            requested_duty_percent = profile->active_value;
            break;

        case FAN_CONTROLLER_BINARY:
        case FAN_CONTROLLER_ADAPTIVE:
            requested_duty_percent = fan_automatic_target(profile, fan_maximum_temperature_c(), now);
            break;

        case FAN_CONTROLLER_OFF:
        default:
            requested_duty_percent = 0;
            break;
        }

        target_duty_percent = fan_apply_startup_policy(profile, requested_duty_percent, now);
        fan_apply_ramp(profile, target_duty_percent, now);
        fan_requested_duty_percent = requested_duty_percent;
    }
#else
    if (fan_mode == FAN_MODE_ON_100_RAMPED)
    {
        fan_output_set(true);
        return;
    }

    if ((fan_mode == FAN_MODE_AUTO_BINARY_100_COOL) || (fan_mode == FAN_MODE_AUTO_BINARY_100_QUIET))
    {
        uint16_t activation_temperature_c =
            fan_mode == FAN_MODE_AUTO_BINARY_100_COOL ? FAN_COOL_ON_TEMPERATURE_C : FAN_QUIET_ON_TEMPERATURE_C;
        uint16_t temperature_c = fan_maximum_temperature_c();

        if (!fan_running && (temperature_c >= activation_temperature_c) &&
            ((uint32_t)(now - fan_last_transition_ms) >= FAN_MINIMUM_OFF_TIME_MS))
        {
            fan_output_set(true);
            fan_last_transition_ms = now;
        }
        else if (fan_running && (temperature_c < (activation_temperature_c - FAN_TEMPERATURE_HYSTERESIS_C)) &&
                 ((uint32_t)(now - fan_last_transition_ms) >= FAN_MINIMUM_ON_TIME_MS))
        {
            fan_output_set(false);
            fan_last_transition_ms = now;
        }
    }
#endif
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
#if FAN_PWM_ENABLED
    fan_output_set(0);
#else
    fan_output_set(false);
#endif
    fan_reset_controller(systick_get_ms());
}

void fan_stop(void)
{
    fan_enabled = false;

#if FAN_PWM_ENABLED
    fan_output_set(0);
#else
    fan_output_set(false);
#endif

    fan_reset_controller(systick_get_ms());
}

void fan_resume(void)
{
    fan_reset_controller(systick_get_ms());
    fan_enabled = true;
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static uint16_t fan_maximum_temperature_c(void)
{
    uint16_t maximum_c = adc_to_celcius(THERM_1_IDX);
    uint16_t candidate_c;

    candidate_c = adc_to_celcius(THERM_2_IDX);
    if (candidate_c > maximum_c)
    {
        maximum_c = candidate_c;
    }

    candidate_c = adc_to_celcius(MCU_TEMP_IDX);
    if (candidate_c > maximum_c)
    {
        maximum_c = candidate_c;
    }

    return maximum_c;
}

static void fan_reset_controller(uint32_t now)
{
#if FAN_PWM_ENABLED
    fan_automatic_active          = false;
    fan_zero_reached              = true;
    fan_startup_boost_active      = false;
    fan_commanded_duty_percent    = 0;
    fan_requested_duty_percent    = 0;
    fan_adaptive_duty_percent     = FAN_MINIMUM_PWM_PERCENT;
    fan_current_duty_millipercent = 0;
    fan_last_task_ms              = now;
    fan_automatic_active_since_ms = now;
    fan_zero_reached_ms           = now;
    fan_startup_boost_started_ms  = now;
    fan_adaptive_updated_ms       = now;
#else
    fan_running            = false;
    fan_last_transition_ms = now;
#endif
}

static bool fan_finish_startup_hold(uint32_t now)
{
#if FAN_PWM_ENABLED
    if (!fan_output_initialize())
    {
        return false;
    }

    /* The startup hold replaces, rather than adds to, the first automatic
     * minimum-off dwell. Later off transitions still use the full dwell. */
    fan_zero_reached    = true;
    fan_zero_reached_ms = now - FAN_MINIMUM_OFF_TIME_MS;
    fan_last_task_ms    = now;
#else
    if (!fan_output_initialized)
    {
        fan_pin_init();
    }

    fan_last_transition_ms = now - FAN_MINIMUM_OFF_TIME_MS;
#endif

    fan_startup_hold_pending = false;
    return true;
}

#if FAN_PWM_ENABLED
static uint8_t fan_automatic_target(const fan_profile_t* profile, uint16_t temperature_c, uint32_t now)
{
    uint16_t turn_off_temperature_c = profile->activation_temperature_c - FAN_TEMPERATURE_HYSTERESIS_C;
    bool     activated_now          = false;

    if (!fan_automatic_active)
    {
        bool minimum_off_complete =
            !fan_zero_reached || ((uint32_t)(now - fan_zero_reached_ms) >= FAN_MINIMUM_OFF_TIME_MS);

        if ((temperature_c >= profile->activation_temperature_c) && minimum_off_complete)
        {
            fan_automatic_active          = true;
            fan_automatic_active_since_ms = now;
            fan_zero_reached              = false;
            activated_now                 = true;
        }
    }
    else if ((temperature_c < turn_off_temperature_c) &&
             ((uint32_t)(now - fan_automatic_active_since_ms) >= FAN_MINIMUM_ON_TIME_MS))
    {
        fan_automatic_active = false;
    }

    if (!fan_automatic_active)
    {
        return 0;
    }

    if (profile->controller == FAN_CONTROLLER_BINARY)
    {
        return profile->active_value;
    }

    if (activated_now || ((uint32_t)(now - fan_adaptive_updated_ms) >= FAN_ADAPTIVE_UPDATE_INTERVAL_MS))
    {
        fan_adaptive_duty_percent = fan_adaptive_duty(temperature_c, profile->active_value);
        fan_adaptive_updated_ms   = now;
    }

    return fan_adaptive_duty_percent;
}

static uint8_t fan_adaptive_duty(uint16_t temperature_c, uint8_t gain_percent)
{
    uint32_t duty_percent;

    if (temperature_c <= FAN_ADAPTIVE_ON_TEMPERATURE_C)
    {
        return FAN_MINIMUM_PWM_PERCENT;
    }

    duty_percent = FAN_ADAPTIVE_ROUNDED_DUTY((uint32_t)temperature_c, (uint32_t)gain_percent);

    if (duty_percent > FAN_FULL_DUTY_PERCENT)
    {
        duty_percent = FAN_FULL_DUTY_PERCENT;
    }

    return (uint8_t)duty_percent;
}

static uint8_t fan_apply_startup_policy(const fan_profile_t* profile, uint8_t requested_duty_percent, uint32_t now)
{
    if (profile->ramp_enabled)
    {
        fan_startup_boost_active = false;
        return requested_duty_percent;
    }

    if (requested_duty_percent == 0)
    {
        fan_startup_boost_active = false;
        return 0;
    }

    /* A boost starts only from a truly stopped fan. Changing an adaptive
     * target or reversing an unfinished ramp-down does not retrigger it. */
    if ((fan_requested_duty_percent == 0) && (fan_current_duty_millipercent == 0))
    {
        fan_startup_boost_active     = true;
        fan_startup_boost_started_ms = now;
    }

    if (fan_startup_boost_active)
    {
        if ((uint32_t)(now - fan_startup_boost_started_ms) < FAN_STARTUP_BOOST_TIME_MS)
        {
            return FAN_FULL_DUTY_PERCENT;
        }
        fan_startup_boost_active = false;
    }

    return requested_duty_percent;
}

static void fan_apply_ramp(const fan_profile_t* profile, uint8_t target_duty_percent, uint32_t now)
{
    uint32_t target_millipercent = (uint32_t)target_duty_percent * FAN_PERCENT_SCALE_MILLI;
    uint32_t elapsed_ms          = (uint32_t)(now - fan_last_task_ms);
    uint32_t maximum_change;
    uint8_t  output_percent;

    fan_last_task_ms = now;

    if (!profile->ramp_enabled)
    {
        fan_current_duty_millipercent = target_millipercent;
    }
    else
    {
        /* 25 percentage points per second equals 25 milli-percent per
         * millisecond. Clamp long gaps before multiplying to avoid overflow. */
        maximum_change = elapsed_ms >= (FAN_FULL_DUTY_MILLI / FAN_RAMP_PERCENT_PER_SECOND)
                             ? FAN_FULL_DUTY_MILLI
                             : elapsed_ms * FAN_RAMP_PERCENT_PER_SECOND;

        if (fan_current_duty_millipercent < target_millipercent)
        {
            uint32_t remaining = target_millipercent - fan_current_duty_millipercent;
            fan_current_duty_millipercent += maximum_change < remaining ? maximum_change : remaining;
        }
        else if (fan_current_duty_millipercent > target_millipercent)
        {
            uint32_t remaining = fan_current_duty_millipercent - target_millipercent;
            fan_current_duty_millipercent -= maximum_change < remaining ? maximum_change : remaining;
        }
    }

    output_percent =
        (uint8_t)((fan_current_duty_millipercent + (FAN_PERCENT_SCALE_MILLI / 2)) / FAN_PERCENT_SCALE_MILLI);

    if (output_percent > 0)
    {
        fan_zero_reached = false;
    }
    else if (!fan_zero_reached)
    {
        /* Automatic minimum-off dwell starts only when the integer command
         * actually completes its ramp to zero. */
        fan_zero_reached    = true;
        fan_zero_reached_ms = now;
    }

    fan_output_set(output_percent);
}

static void fan_output_set(uint8_t duty_percent)
{
    if (duty_percent > FAN_FULL_DUTY_PERCENT)
    {
        duty_percent = FAN_FULL_DUTY_PERCENT;
    }

    if ((duty_percent > 0) && !fan_output_initialized)
    {
        if (!fan_output_initialize())
        {
            return;
        }
    }

    if (fan_output_initialized && (duty_percent != fan_commanded_duty_percent))
    {
        fanpwm_set(duty_percent);
        fan_commanded_duty_percent = duty_percent;
    }
}

static bool fan_output_initialize(void)
{
    fanpwm_mode_t pwm_mode;

    if (fan_output_initialized)
    {
        return true;
    }

    pwm_mode = fan_signal_inverted ? FANPWM_MODE_EXTERNAL_MOSFET : FANPWM_MODE_DIRECT;
    if (!fanpwm_init(pwm_mode))
    {
        return false;
    }

    fan_output_initialized     = true;
    fan_commanded_duty_percent = 0;
    return true;
}
#else
static void fan_pin_init(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* Preload low before PA13 is taken away from SWD, preventing a transient
     * high at the direct active-high fan input. */
    HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, GPIO_PIN_RESET);

    gpio_cfg.Pin   = FAN_PINn;
    gpio_cfg.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_cfg.Pull  = GPIO_NOPULL;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(FAN_GPIOx, &gpio_cfg);

    fan_output_initialized = true;
}

static void fan_output_set(bool running)
{
    if (running && !fan_output_initialized)
    {
        fan_pin_init();
    }

    if (fan_output_initialized && (running != fan_running))
    {
        HAL_GPIO_WritePin(FAN_GPIOx, FAN_PINn, running ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }

    fan_running = running;
}
#endif
