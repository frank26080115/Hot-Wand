/*
 * Hardware-timed 470 kHz RF carrier and burst-power control for RP2040.
 *
 * The PWM slice is left running permanently once initialized. Turning RF off
 * writes a zero compare value, which is transferred at the next PWM wrap. This
 * lets the current pulse finish normally and can never freeze the output high.
 * A hardware alarm gates the carrier for partial-power burst operation.
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "rfgen.h"

#include <Arduino.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <hardware/sync.h>
#include <pico/time.h>

#include "hotwandlite.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// One undivided 133 MHz PWM period is 283 clocks, or about 469.965 kHz.
static constexpr uint32_t kPwmPeriodClocks = (F_CPU + (kRfFrequencyHz / 2)) / kRfFrequencyHz;
static constexpr uint16_t kPwmTop          = static_cast<uint16_t>(kPwmPeriodClocks - 1);
static constexpr uint16_t kPwmHighClocks   = static_cast<uint16_t>(kPwmPeriodClocks / 2);

// Avoid an accidentally huge partial-power on window from an unreasonable
// caller-supplied burst period. Normal operation uses the 10 ms default.
static constexpr uint32_t kBurstMaximumPeriodUs = 1000000;

static_assert(F_CPU == 133000000, "RF generator timer settings require a 133 MHz CPU clock");
static_assert(kPwmPeriodClocks >= 2, "RF PWM period must contain a high and a low clock");
static_assert(kPwmPeriodClocks <= 65536, "RF PWM period does not fit the RP2040 PWM counter");
static_assert(kPwmHighClocks > 0, "RF PWM high time must not be zero");
static_assert(kPwmHighClocks < kPwmPeriodClocks, "RF PWM must include a low interval");

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static bool g_initialized = false;
static uint g_pwmSlice    = 0;

static volatile bool       g_burstIsActive = false;
static volatile bool       g_burstIsOn     = false;
static volatile uint32_t   g_burstOnUs     = 1;
static volatile uint32_t   g_burstOffUs    = 1;
static volatile alarm_id_t g_burstAlarmId  = -1;

static bool     g_settingInitialized = false;
static bool     g_rampIsActive       = false;
static uint32_t g_appliedPeriodUs    = 0;
static uint32_t g_appliedOnUs        = 0;
static uint32_t g_desiredPeriodUs    = 0;
static uint32_t g_desiredOnUs        = 0;
static uint32_t g_rampStartOnUs      = 0;
static uint32_t g_rampStartedMs      = 0;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static uint32_t burst_period_to_us(uint32_t periodMs);
static uint32_t power_percent_to_on_us(uint8_t powerPercent, uint32_t periodUs);
static uint32_t scale_on_time(uint32_t onUs, uint32_t oldPeriodUs, uint32_t newPeriodUs);
static void     restart_ramp_from_applied();
static void     rfgen_apply_ramp_time(uint32_t periodUs, uint32_t onUs);
static void     rfgen_apply_time(uint32_t periodUs, uint32_t onUs);
static void     rfgen_initialize();
static void     carrier_set(bool enabled);
static void     burst_cancel_locked();
static bool     burst_start_locked(uint32_t onUs, uint32_t offUs);
static int64_t  burst_alarm_callback(alarm_id_t alarmId, void* userData);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void rfgen_set(uint8_t powerPercent, uint32_t periodMs)
{
    uint32_t currentOnUs;

    if (powerPercent > kMaximumPowerPercent)
    {
        powerPercent = kMaximumPowerPercent;
    }

    const uint32_t periodUs = burst_period_to_us(periodMs);
    const uint32_t onUs     = power_percent_to_on_us(powerPercent, periodUs);

    // Repeated requests do not restart a ramp already in progress.
    if (g_settingInitialized && (periodUs == g_desiredPeriodUs) && (onUs == g_desiredOnUs))
    {
        return;
    }

    if (!g_settingInitialized)
    {
        // Establish a known-low output before accepting any increase.
        rfgen_apply_time(periodUs, 0);
        currentOnUs          = 0;
        g_settingInitialized = true;
    }
    else
    {
        // Preserve actual duty if the requested burst period changes.
        currentOnUs = scale_on_time(g_appliedOnUs, g_appliedPeriodUs, periodUs);
    }

    g_desiredPeriodUs = periodUs;
    g_desiredOnUs     = onUs;

    if (onUs <= currentOnUs)
    {
        // Reductions, including off, are applied synchronously.
        g_rampIsActive = false;
        if ((g_appliedPeriodUs != periodUs) || (g_appliedOnUs != onUs))
        {
            rfgen_apply_time(periodUs, onUs);
            if (g_appliedOnUs != onUs)
            {
                restart_ramp_from_applied();
            }
        }
        return;
    }

    // Increases are derived from elapsed time in rfgen_task(), so loop rate
    // does not affect the nominal one-second 0-to-100-percent ramp.
    g_rampStartOnUs = currentOnUs;
    g_rampStartedMs = millis();
    g_rampIsActive  = true;
}

void rfgen_task(void)
{
    if (!g_settingInitialized || !g_rampIsActive)
    {
        return;
    }

    const uint32_t elapsedMs = static_cast<uint32_t>(millis() - g_rampStartedMs);
    const uint64_t nextOnUsWide =
        g_rampStartOnUs + ((static_cast<uint64_t>(elapsedMs) * g_desiredPeriodUs) / kPowerRampDurationMs);
    uint32_t nextOnUs;
    if (nextOnUsWide >= g_desiredOnUs)
    {
        nextOnUs       = g_desiredOnUs;
        g_rampIsActive = false;
    }
    else
    {
        nextOnUs = static_cast<uint32_t>(nextOnUsWide);
    }

    if ((g_appliedPeriodUs != g_desiredPeriodUs) || (g_appliedOnUs != nextOnUs))
    {
        rfgen_apply_ramp_time(g_desiredPeriodUs, nextOnUs);
        if (g_appliedOnUs != nextOnUs)
        {
            restart_ramp_from_applied();
        }
    }
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

static void rfgen_initialize()
{
    // Set the GPIO latch low before handing the pad to the PWM peripheral.
    digitalWrite(RFGEN_PIN, LOW);
    pinMode(RFGEN_PIN, OUTPUT);

    g_pwmSlice = pwm_gpio_to_slice_num(RFGEN_PIN);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&config, 1);
    pwm_config_set_wrap(&config, kPwmTop);

    // pwm_init clears both compare channels. Start the counter before changing
    // the pad function so the peripheral is already producing a steady low.
    pwm_init(g_pwmSlice, &config, true);
    pwm_set_gpio_level(RFGEN_PIN, 0);
    gpio_set_function(RFGEN_PIN, GPIO_FUNC_PWM);

    g_initialized = true;
}

static int64_t burst_alarm_callback(alarm_id_t alarmId, void* userData)
{
    (void)userData;

    if (!g_burstIsActive || (alarmId != g_burstAlarmId))
    {
        carrier_set(false);
        return 0;
    }

    if (g_burstIsOn)
    {
        // A zero compare takes effect at the next wrap, so the current high
        // pulse finishes but can never be stretched by stopping the counter.
        carrier_set(false);
        g_burstIsOn = false;
        return -static_cast<int64_t>(g_burstOffUs);
    }

    carrier_set(true);
    g_burstIsOn = true;
    return -static_cast<int64_t>(g_burstOnUs);
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static uint32_t burst_period_to_us(uint32_t periodMs)
{
    if (periodMs == 0)
    {
        periodMs = kDefaultBurstPeriodMs;
    }

    const uint64_t periodUs = static_cast<uint64_t>(periodMs) * 1000;
    if (periodUs > kBurstMaximumPeriodUs)
    {
        return kBurstMaximumPeriodUs;
    }

    return static_cast<uint32_t>(periodUs);
}

static uint32_t power_percent_to_on_us(uint8_t powerPercent, uint32_t periodUs)
{
    if (powerPercent == 0)
    {
        return 0;
    }
    if (powerPercent >= kMaximumPowerPercent)
    {
        return periodUs;
    }

    uint32_t onUs = ((periodUs * powerPercent) + (kMaximumPowerPercent / 2)) / kMaximumPowerPercent;

    // Every partial setting must retain both an on and an off phase.
    if (onUs == 0)
    {
        return 1;
    }
    if (onUs >= periodUs)
    {
        return periodUs - 1;
    }
    return onUs;
}

static uint32_t scale_on_time(uint32_t onUs, uint32_t oldPeriodUs, uint32_t newPeriodUs)
{
    if ((onUs == 0) || (oldPeriodUs == 0))
    {
        return 0;
    }
    if (onUs >= oldPeriodUs)
    {
        return newPeriodUs;
    }

    const uint64_t scaledNumerator = static_cast<uint64_t>(onUs) * newPeriodUs;
    return static_cast<uint32_t>((scaledNumerator + (oldPeriodUs / 2)) / oldPeriodUs);
}

static void restart_ramp_from_applied()
{
    // A failed partial-burst alarm allocation leaves the carrier safely off.
    // Retry later as a fresh ramp so recovery can never jump straight to the
    // requested power.
    g_rampStartOnUs = g_appliedOnUs;
    g_rampStartedMs = millis();
    g_rampIsActive  = g_appliedOnUs < g_desiredOnUs;
}

static void rfgen_apply_ramp_time(uint32_t periodUs, uint32_t onUs)
{
    const bool appliedIsPartial = (g_appliedOnUs > 0) && (g_appliedOnUs < g_appliedPeriodUs);
    const bool nextIsPartial    = (onUs > 0) && (onUs < periodUs);

    if (!g_initialized || !g_burstIsActive || !appliedIsPartial || !nextIsPartial || (g_appliedPeriodUs != periodUs))
    {
        rfgen_apply_time(periodUs, onUs);
        return;
    }

    // Keep the current phase running. The alarm callback consumes the new
    // durations at the following on/off boundary.
    const uint32_t interruptState = save_and_disable_interrupts();
    g_burstOnUs                   = onUs;
    g_burstOffUs                  = periodUs - onUs;
    g_appliedPeriodUs             = periodUs;
    g_appliedOnUs                 = onUs;
    restore_interrupts(interruptState);
}

static void rfgen_apply_time(uint32_t periodUs, uint32_t onUs)
{
    if (onUs > periodUs)
    {
        onUs = periodUs;
    }

    if (!g_initialized)
    {
        if (onUs == 0)
        {
            // The boot-time off request is safe without initializing PWM.
            digitalWrite(RFGEN_PIN, LOW);
            pinMode(RFGEN_PIN, OUTPUT);
            g_appliedPeriodUs = periodUs;
            g_appliedOnUs     = 0;
            return;
        }

        rfgen_initialize();
    }

    const uint32_t interruptState = save_and_disable_interrupts();

    // Always gate off before replacing a burst schedule. The PWM counter stays
    // running, so zero is latched no later than the next 470 kHz wrap.
    burst_cancel_locked();

    uint32_t appliedOnUs = onUs;
    if (onUs == 0)
    {
        carrier_set(false);
    }
    else if (onUs >= periodUs)
    {
        carrier_set(true);
    }
    else if (!burst_start_locked(onUs, periodUs - onUs))
    {
        // Alarm-pool exhaustion fails safe: output remains low and the next
        // ramp task pass will retry because the applied duty remains zero.
        appliedOnUs = 0;
    }

    g_appliedPeriodUs = periodUs;
    g_appliedOnUs     = appliedOnUs;
    restore_interrupts(interruptState);
}

static void burst_cancel_locked()
{
    g_burstIsActive = false;
    carrier_set(false);

    const alarm_id_t alarmId = g_burstAlarmId;
    g_burstAlarmId           = -1;
    if (alarmId > 0)
    {
        cancel_alarm(alarmId);
    }

    g_burstIsOn = false;
}

static bool burst_start_locked(uint32_t onUs, uint32_t offUs)
{
    g_burstOnUs     = onUs;
    g_burstOffUs    = offUs;
    g_burstIsOn     = false;
    g_burstIsActive = true;

    // Begin partial power with its off phase. This avoids an initial power
    // overshoot and gives the zero compare ample time to reach a PWM wrap.
    const alarm_id_t alarmId = add_alarm_in_us(offUs, burst_alarm_callback, nullptr, false);
    if (alarmId <= 0)
    {
        g_burstIsActive = false;
        g_burstAlarmId  = -1;
        carrier_set(false);
        return false;
    }

    g_burstAlarmId = alarmId;
    return true;
}

static void carrier_set(bool enabled)
{
    pwm_set_gpio_level(RFGEN_PIN, enabled ? kPwmHighClocks : 0);
}
