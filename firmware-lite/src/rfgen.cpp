/*
 * Hardware-timed 470 kHz RF carrier and burst-power control.
 *
 * TCC0 produces the carrier on PA04. TC5 schedules partial-power on/off
 * windows while TCC0's buffered compare register guarantees low-safe stops.
 *
 * The RF generator signal must never be left high
 * as this would make the MOSFET turn on indefinitely
 * and the current would flow through the inductor and MOSFET
 * as if it were a short circuit
 *
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "rfgen.h"

#include <Arduino.h>
#include <wiring_private.h>

#include "hotwandlite_pins.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

static constexpr uint8_t  kMaximumPowerPercent  = 100;
static constexpr uint32_t kDefaultBurstPeriodMs = 10;
static constexpr uint32_t kPowerRampDurationMs  = 1000;
static constexpr uint32_t kRfFrequencyHz        = 470000;

// GCLK0 is the 48 MHz CPU clock. 102 clocks produce 470.588 kHz.
static constexpr uint32_t kPwmPeriodClocks = (F_CPU + (kRfFrequencyHz / 2)) / kRfFrequencyHz;
static constexpr uint32_t kPwmTop          = kPwmPeriodClocks - 1;
static constexpr uint32_t kPwmHighClocks   = kPwmPeriodClocks / 2;

// TC5 times the burst envelope at 46.875 kHz (21.33 us per count).
static constexpr uint32_t kBurstTimerPrescaler = 1024;
static constexpr uint32_t kBurstMaximumTicks   = 65536;

static_assert(F_CPU == 48000000, "RF generator timer settings require a 48 MHz CPU clock");
static_assert((kPwmPeriodClocks % 2) == 0, "RF PWM must have equal high and low times");

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static bool              g_initialized   = false;
static volatile bool     g_burstIsOn     = false;
static volatile uint16_t g_burstOnTicks  = 1;
static volatile uint16_t g_burstOffTicks = 1;

static bool     g_settingInitialized = false;
static bool     g_rampIsActive       = false;
static uint32_t g_appliedPeriodTicks = 0;
static uint32_t g_appliedOnTicks     = 0;
static uint32_t g_desiredPeriodTicks = 0;
static uint32_t g_desiredOnTicks     = 0;
static uint32_t g_rampStartOnTicks   = 0;
static uint32_t g_rampStartedMs      = 0;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

extern "C" void TC5_Handler();

static uint32_t burst_period_to_ticks(uint32_t periodMs);
static uint32_t power_percent_to_on_ticks(uint8_t powerPercent, uint32_t periodTicks);
static uint32_t scale_on_ticks(uint32_t onTicks, uint32_t oldPeriodTicks, uint32_t newPeriodTicks);
static void     rfgen_apply_ramp_ticks(uint32_t periodTicks, uint32_t onTicks);
static void     rfgen_apply_ticks(uint32_t periodTicks, uint32_t onTicks);
static void     rfgen_initialize();
static void     burst_timer_start(uint16_t firstIntervalTicks);
static void     burst_timer_stop();
static void     burst_timer_set_interval(uint16_t ticks);
static void     rf_pwm_set(bool enabled);
static void     wait_for_tcc0_sync();
static void     wait_for_tc5_sync();

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void rfgen_set(uint8_t powerPercent, uint32_t periodMs)
{
    uint32_t currentOnTicks;

    // Treat out-of-range requests as full power rather than overflowing math.
    if (powerPercent > kMaximumPowerPercent)
    {
        powerPercent = kMaximumPowerPercent;
    }

    const uint32_t periodTicks = burst_period_to_ticks(periodMs);
    const uint32_t onTicks     = power_percent_to_on_ticks(powerPercent, periodTicks);

    // Repeating the same request must not restart an in-progress ramp. This is
    // useful if a future caller refreshes its desired state every loop.
    if (g_settingInitialized && (periodTicks == g_desiredPeriodTicks) && (onTicks == g_desiredOnTicks))
    {
        return;
    }

    if (!g_settingInitialized)
    {
        // Establish a known-low output even if the first request is nonzero.
        // The first increase will then begin from zero in rfgen_task().
        rfgen_apply_ticks(periodTicks, 0);
        currentOnTicks       = 0;
        g_settingInitialized = true;
    }
    else
    {
        // Express the currently applied duty in the new period's tick scale.
        // This lets a simultaneous period change preserve actual power while
        // the requested increase ramps from its present level.
        currentOnTicks = scale_on_ticks(g_appliedOnTicks, g_appliedPeriodTicks, periodTicks);
    }

    g_desiredPeriodTicks = periodTicks;
    g_desiredOnTicks     = onTicks;

    if (onTicks <= currentOnTicks)
    {
        // Reductions are safety-relevant and take effect synchronously in the
        // setter. Equality also applies a requested period change immediately.
        g_rampIsActive = false;
        if ((g_appliedPeriodTicks != periodTicks) || (g_appliedOnTicks != onTicks))
        {
            rfgen_apply_ticks(periodTicks, onTicks);
        }
        return;
    }

    // Increases are deferred to rfgen_task(). The task derives the appropriate
    // duty ticks from elapsed milliseconds, so loop frequency does not affect
    // the one-second 0-to-100-percent ramp rate.
    g_rampStartOnTicks = currentOnTicks;
    g_rampStartedMs    = millis();
    g_rampIsActive     = true;
}

void rfgen_task(void)
{
    if (!g_settingInitialized || !g_rampIsActive)
    {
        return;
    }

    const uint32_t elapsedMs = static_cast<uint32_t>(millis() - g_rampStartedMs);
    const uint64_t nextOnTicksWide =
        g_rampStartOnTicks + ((static_cast<uint64_t>(elapsedMs) * g_desiredPeriodTicks) / kPowerRampDurationMs);
    uint32_t nextOnTicks;
    if (nextOnTicksWide >= g_desiredOnTicks)
    {
        nextOnTicks    = g_desiredOnTicks;
        g_rampIsActive = false;
    }
    else
    {
        nextOnTicks = static_cast<uint32_t>(nextOnTicksWide);
    }

    if ((g_appliedPeriodTicks != g_desiredPeriodTicks) || (g_appliedOnTicks != nextOnTicks))
    {
        rfgen_apply_ramp_ticks(g_desiredPeriodTicks, nextOnTicks);
    }
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

static void rfgen_initialize()
{
    // Load the GPIO output latch low before either GPIO or TCC takes the pin.
    digitalWrite(RFGEN_PIN, LOW);
    pinMode(RFGEN_PIN, OUTPUT);

    PM->APBCMASK.reg |= PM_APBCMASK_TCC0 | PM_APBCMASK_TC5;

    // Route the 48 MHz generic clock to the PWM and burst timers.
    GCLK->CLKCTRL.reg = static_cast<uint16_t>(GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID_TCC0_TCC1);
    while (GCLK->STATUS.bit.SYNCBUSY != 0)
    {
    }

    GCLK->CLKCTRL.reg = static_cast<uint16_t>(GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID_TC4_TC5);
    while (GCLK->STATUS.bit.SYNCBUSY != 0)
    {
    }

    // Configure TCC0 for a 50% single-slope PWM that initially remains low.
    TCC0->CTRLA.reg = TCC_CTRLA_SWRST;
    while ((TCC0->SYNCBUSY.bit.SWRST != 0) || (TCC0->CTRLA.bit.SWRST != 0))
    {
    }

    TCC0->CTRLA.reg = TCC_CTRLA_PRESCALER_DIV1 | TCC_CTRLA_PRESCSYNC_GCLK;
    TCC0->WAVE.reg  = TCC_WAVE_WAVEGEN_NPWM;
    wait_for_tcc0_sync();
    TCC0->PER.reg = kPwmTop;
    wait_for_tcc0_sync();
    TCC0->CC[0].reg = 0;
    wait_for_tcc0_sync();
    TCC0->CTRLA.bit.ENABLE = 1;
    wait_for_tcc0_sync();

    // PA04 peripheral E is TCC0/WO[0]. Its timer output is currently low.
    pinPeripheral(RFGEN_PIN, PIO_TIMER);

    // TC5 uses match-frequency mode so each compare schedules one burst phase.
    TC5->COUNT16.CTRLA.reg = TC_CTRLA_SWRST;
    while ((TC5->COUNT16.STATUS.bit.SYNCBUSY != 0) || (TC5->COUNT16.CTRLA.bit.SWRST != 0))
    {
    }

    TC5->COUNT16.CTRLA.reg =
        TC_CTRLA_MODE_COUNT16 | TC_CTRLA_WAVEGEN_MFRQ | TC_CTRLA_PRESCALER_DIV1024 | TC_CTRLA_PRESCSYNC_GCLK;
    wait_for_tc5_sync();
    NVIC_SetPriority(TC5_IRQn, 3);

    g_initialized = true;
}

extern "C" void TC5_Handler()
{
    if ((TC5->COUNT16.INTFLAG.reg & TC_INTFLAG_MC0) == 0)
    {
        return;
    }

    TC5->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;

    // Alternate between the precomputed on and off durations.
    if (g_burstIsOn)
    {
        g_burstIsOn = false;
        rf_pwm_set(false);
        burst_timer_set_interval(g_burstOffTicks);
    }
    else
    {
        g_burstIsOn = true;
        rf_pwm_set(true);
        burst_timer_set_interval(g_burstOnTicks);
    }
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static uint32_t burst_period_to_ticks(uint32_t periodMs)
{
    if (periodMs == 0)
    {
        periodMs = kDefaultBurstPeriodMs;
    }

    constexpr uint64_t kTicksDenominator = static_cast<uint64_t>(kBurstTimerPrescaler) * 1000;
    const uint64_t     roundedTicks =
        ((static_cast<uint64_t>(periodMs) * F_CPU) + (kTicksDenominator / 2)) / kTicksDenominator;

    // Partial power always needs at least one on tick and one off tick.
    if (roundedTicks < 2)
    {
        return 2;
    }
    if (roundedTicks > kBurstMaximumTicks)
    {
        return kBurstMaximumTicks;
    }

    return static_cast<uint32_t>(roundedTicks);
}

static uint32_t power_percent_to_on_ticks(uint8_t powerPercent, uint32_t periodTicks)
{
    if (powerPercent == 0)
    {
        return 0;
    }
    if (powerPercent >= kMaximumPowerPercent)
    {
        return periodTicks;
    }

    uint32_t onTicks = ((periodTicks * powerPercent) + (kMaximumPowerPercent / 2)) / kMaximumPowerPercent;

    // Every partial-power setting needs at least one on tick and one off tick.
    if (onTicks == 0)
    {
        return 1;
    }
    if (onTicks >= periodTicks)
    {
        return periodTicks - 1;
    }
    return onTicks;
}

static uint32_t scale_on_ticks(uint32_t onTicks, uint32_t oldPeriodTicks, uint32_t newPeriodTicks)
{
    if ((onTicks == 0) || (oldPeriodTicks == 0))
    {
        return 0;
    }
    if (onTicks >= oldPeriodTicks)
    {
        return newPeriodTicks;
    }

    const uint64_t scaledNumerator = static_cast<uint64_t>(onTicks) * newPeriodTicks;
    return static_cast<uint32_t>((scaledNumerator + (oldPeriodTicks / 2)) / oldPeriodTicks);
}

static void rfgen_apply_ramp_ticks(uint32_t periodTicks, uint32_t onTicks)
{
    const bool appliedIsPartial = (g_appliedOnTicks > 0) && (g_appliedOnTicks < g_appliedPeriodTicks);
    const bool nextIsPartial    = (onTicks > 0) && (onTicks < periodTicks);

    if (!g_initialized || !appliedIsPartial || !nextIsPartial || (g_appliedPeriodTicks != periodTicks))
    {
        // Entering or leaving partial power, and changing the burst period,
        // requires a complete timer reconfiguration.
        rfgen_apply_ticks(periodTicks, onTicks);
        return;
    }

    /*
     * Do not restart TC5 for each ramp tick. Ramp updates arrive more often
     * than one burst period, so restarting would repeatedly favor the on phase
     * and produce much more power than requested. The ISR will use these new
     * durations at its next on/off boundary.
     */
    const uint32_t interruptState = __get_PRIMASK();
    __disable_irq();
    g_burstOnTicks  = static_cast<uint16_t>(onTicks);
    g_burstOffTicks = static_cast<uint16_t>(periodTicks - onTicks);
    if (interruptState == 0)
    {
        __enable_irq();
    }

    g_appliedPeriodTicks = periodTicks;
    g_appliedOnTicks     = onTicks;
}

static void rfgen_apply_ticks(uint32_t periodTicks, uint32_t onTicks)
{
    if (onTicks > periodTicks)
    {
        onTicks = periodTicks;
    }

    if (!g_initialized)
    {
        if (onTicks == 0)
        {
            // A zero request must be safe without paying the timer-init cost.
            digitalWrite(RFGEN_PIN, LOW);
            pinMode(RFGEN_PIN, OUTPUT);
            g_appliedPeriodTicks = periodTicks;
            g_appliedOnTicks     = 0;
            return;
        }

        rfgen_initialize();
    }

    /*
     * TC5's ISR shares the burst state and timer registers with foreground
     * control. Preserve the caller's interrupt state around reconfiguration.
     */
    const uint32_t interruptState = __get_PRIMASK();
    __disable_irq();

    burst_timer_stop();

    if (onTicks == 0)
    {
        g_burstIsOn = false;
        rf_pwm_set(false);
    }
    else if (onTicks >= periodTicks)
    {
        // Full power never schedules an off window.
        g_burstIsOn = true;
        rf_pwm_set(true);
    }
    else
    {
        g_burstOnTicks  = static_cast<uint16_t>(onTicks);
        g_burstOffTicks = static_cast<uint16_t>(periodTicks - onTicks);
        g_burstIsOn     = true;
        rf_pwm_set(true);
        burst_timer_start(g_burstOnTicks);
    }

    if (interruptState == 0)
    {
        __enable_irq();
    }

    g_appliedPeriodTicks = periodTicks;
    g_appliedOnTicks     = onTicks;
}

static void burst_timer_start(uint16_t firstIntervalTicks)
{
    TC5->COUNT16.CTRLA.bit.ENABLE = 0;
    wait_for_tc5_sync();

    TC5->COUNT16.COUNT.reg = 0;
    wait_for_tc5_sync();
    burst_timer_set_interval(firstIntervalTicks);

    TC5->COUNT16.INTFLAG.reg  = TC_INTFLAG_MC0;
    TC5->COUNT16.INTENSET.reg = TC_INTENSET_MC0;
    NVIC_ClearPendingIRQ(TC5_IRQn);
    NVIC_EnableIRQ(TC5_IRQn);

    TC5->COUNT16.CTRLA.bit.ENABLE = 1;
    wait_for_tc5_sync();
}

static void burst_timer_stop()
{
    TC5->COUNT16.INTENCLR.reg     = TC_INTENCLR_MC0;
    TC5->COUNT16.CTRLA.bit.ENABLE = 0;
    wait_for_tc5_sync();

    TC5->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;
    NVIC_DisableIRQ(TC5_IRQn);
    NVIC_ClearPendingIRQ(TC5_IRQn);
}

static void burst_timer_set_interval(uint16_t ticks)
{
    TC5->COUNT16.CC[0].reg = static_cast<uint16_t>(ticks - 1);
    wait_for_tc5_sync();
}

static void rf_pwm_set(bool enabled)
{
    /*
     * CCB0 transfers to CC0 only at a PWM update boundary.
     *
     * A zero duty cannot truncate a high
     * pulse: the current high pulse
     * finishes, the output goes low, and the following cycle remains low.
     */
    while (TCC0->SYNCBUSY.bit.CCB0 != 0)
    {
    }

    TCC0->CCB[0].reg = enabled ? kPwmHighClocks : 0;

    while (TCC0->SYNCBUSY.bit.CCB0 != 0)
    {
    }
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------

static void wait_for_tcc0_sync()
{
    while (TCC0->SYNCBUSY.reg != 0)
    {
    }
}

static void wait_for_tc5_sync()
{
    while (TC5->COUNT16.STATUS.bit.SYNCBUSY != 0)
    {
    }
}
