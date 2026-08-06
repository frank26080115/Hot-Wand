/*
 * Hardware-timed 470 kHz RF carrier and burst-power control.
 *
 * TCC0 produces the carrier on PA04. TC5 schedules
 * partial-power on/off
 * windows while TCC0's buffered compare register guarantees low-safe stops.
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

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

extern "C" void TC5_Handler();

static void     rfgen_initialize();
static uint32_t burst_period_to_ticks(uint32_t periodMs);
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
    // Treat out-of-range requests as full power rather than overflowing math.
    if (powerPercent > kMaximumPowerPercent)
    {
        powerPercent = kMaximumPowerPercent;
    }

    if (!g_initialized)
    {
        if (powerPercent == 0)
        {
            // A zero request must be safe without paying the timer-init cost.
            digitalWrite(RFGEN_PIN, LOW);
            pinMode(RFGEN_PIN, OUTPUT);
            return;
        }

        rfgen_initialize();
    }

    uint16_t onTicks  = 1;
    uint16_t offTicks = 1;
    if ((powerPercent > 0) && (powerPercent < kMaximumPowerPercent))
    {
        const uint32_t periodTicks    = burst_period_to_ticks(periodMs);
        uint32_t       roundedOnTicks = ((periodTicks * powerPercent) + 50) / 100;
        if (roundedOnTicks == 0)
        {
            roundedOnTicks = 1;
        }
        else if (roundedOnTicks >= periodTicks)
        {
            roundedOnTicks = periodTicks - 1;
        }

        onTicks  = static_cast<uint16_t>(roundedOnTicks);
        offTicks = static_cast<uint16_t>(periodTicks - roundedOnTicks);
    }

    /*
     * TC5's ISR shares the burst state and timer registers with this setter.
     * Preserve the caller's
     * interrupt state around the complete reconfiguration.
     */
    const uint32_t interruptState = __get_PRIMASK();
    __disable_irq();

    burst_timer_stop();

    if (powerPercent == 0)
    {
        g_burstIsOn = false;
        rf_pwm_set(false);
    }
    else if (powerPercent == kMaximumPowerPercent)
    {
        // Full power never schedules an off window.
        g_burstIsOn = true;
        rf_pwm_set(true);
    }
    else
    {
        g_burstOnTicks  = onTicks;
        g_burstOffTicks = offTicks;
        g_burstIsOn     = true;
        rf_pwm_set(true);
        burst_timer_start(onTicks);
    }

    if (interruptState == 0)
    {
        __enable_irq();
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
