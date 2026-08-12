/*
 * DMA-modulated, fixed-pulse-width RF generator for SAMD21.
 *
 * TCC0 produces one pulse per PWM period on PA04. The compare value remains
 * fixed while a circular DMA descriptor writes the next period to PERB after
 * every TCC0 overflow.
 *
 * The RF output must never be left continuously high. Every table entry is
 * validated before use, and all reconfiguration first latches a zero compare
 * value at a PWM boundary.
 */

#include "rfgen.h"

#include <Adafruit_ZeroDMA.h>
#include <Arduino.h>
#include <wiring_private.h>

#include "hotwandlite.h"
#include "pwr_table.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// GCLK0 is 48 MHz. 102 clocks produce approximately 470.588 kHz.
static constexpr uint32_t kPwmPeriodClocks = (F_CPU + (kRfFrequencyHz / 2)) / kRfFrequencyHz;
static constexpr uint32_t kPwmInitialTop    = kPwmPeriodClocks - 1;
static constexpr uint32_t kPwmHighClocks   = kPwmPeriodClocks / 2;
static constexpr uint32_t kPwmMaximumTop   = TCC_PER_PER_Msk;

static_assert(F_CPU == 48000000, "RF generator timer settings require a 48 MHz CPU clock");
static_assert(kPwmHighClocks == 51, "Unexpected SAMD21 RF pulse width");
static_assert(kPwmHighClocks <= kPwmInitialTop, "RF PWM must include a low interval");
static_assert(PWR_TABLE_ROW_LENGTH <= UINT16_MAX, "Power row does not fit a DMA descriptor");

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static bool              g_initialized  = false;
static bool              g_dmaRunning  = false;
static uint8_t           g_currentLevel = RFGEN_POWER_OFF;
static Adafruit_ZeroDMA  g_periodDma;
static DmacDescriptor*   g_periodDescriptor = nullptr;
alignas(16) static uint32_t g_dmaPeriods[PWR_TABLE_ROW_LENGTH];

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static bool initialize_hardware();
static bool initialize_dma();
static bool validate_row(uint8_t powerLevel);
static void copy_row(uint8_t powerLevel);
static void stop_output();
static bool start_output();
static void wait_for_tcc0_sync();

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void rfgen_set(uint8_t powerLevel)
{
    if (powerLevel > RFGEN_POWER_MAXIMUM)
    {
        powerLevel = RFGEN_POWER_MAXIMUM;
    }

    if (powerLevel == g_currentLevel)
    {
        if (powerLevel == RFGEN_POWER_OFF)
        {
            stop_output();
        }
        return;
    }

    if (powerLevel == RFGEN_POWER_OFF)
    {
        stop_output();
        g_currentLevel = RFGEN_POWER_OFF;
        return;
    }

    // Validate the flash-resident source before disturbing a running row.
    if (!validate_row(powerLevel))
    {
        stop_output();
        g_currentLevel = RFGEN_POWER_OFF;
        return;
    }

    if (!g_initialized && !initialize_hardware())
    {
        g_currentLevel = RFGEN_POWER_OFF;
        return;
    }

    // Row changes are allowed to interrupt the waveform, but never while high.
    stop_output();
    copy_row(powerLevel);

    if (!start_output())
    {
        stop_output();
        g_currentLevel = RFGEN_POWER_OFF;
        return;
    }

    g_currentLevel = powerLevel;
}

// -----------------------------------------------------------------------------
// Hardware Setup
// -----------------------------------------------------------------------------

static bool initialize_hardware()
{
    // Load the GPIO output latch low before TCC0 is connected to the pad.
    digitalWrite(RFGEN_PIN, LOW);
    pinMode(RFGEN_PIN, OUTPUT);

    PM->APBCMASK.reg |= PM_APBCMASK_TCC0;

    GCLK->CLKCTRL.reg = static_cast<uint16_t>(GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 |
                                              GCLK_CLKCTRL_ID_TCC0_TCC1);
    while (GCLK->STATUS.bit.SYNCBUSY != 0)
    {
    }

    TCC0->CTRLA.reg = TCC_CTRLA_SWRST;
    while ((TCC0->SYNCBUSY.bit.SWRST != 0) || (TCC0->CTRLA.bit.SWRST != 0))
    {
    }

    TCC0->CTRLA.reg = TCC_CTRLA_PRESCALER_DIV1 | TCC_CTRLA_PRESCSYNC_GCLK;
    TCC0->WAVE.reg  = TCC_WAVE_WAVEGEN_NPWM;
    wait_for_tcc0_sync();

    TCC0->PER.reg = kPwmInitialTop;
    wait_for_tcc0_sync();
    TCC0->CC[0].reg = 0;
    wait_for_tcc0_sync();

    if (!initialize_dma())
    {
        return false;
    }

    // Start TCC0 disconnected from the pin and producing a steady low level.
    TCC0->CTRLA.bit.ENABLE = 1;
    wait_for_tcc0_sync();
    pinPeripheral(RFGEN_PIN, PIO_TIMER);

    g_initialized = true;
    return true;
}

static bool initialize_dma()
{
    g_periodDma.setTrigger(TCC0_DMAC_ID_OVF);
    g_periodDma.setAction(DMA_TRIGGER_ACTON_BEAT);
    g_periodDma.loop(true);

    if (g_periodDma.allocate() != DMA_STATUS_OK)
    {
        return false;
    }

    g_periodDma.setPriority(DMA_PRIORITY_3);
    g_periodDescriptor = g_periodDma.addDescriptor(g_dmaPeriods,
                                                    const_cast<uint32_t*>(&TCC0->PERB.reg),
                                                    PWR_TABLE_ROW_LENGTH,
                                                    DMA_BEAT_SIZE_WORD,
                                                    true,
                                                    false);
    if (g_periodDescriptor == nullptr)
    {
        g_periodDma.free();
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// Row Management
// -----------------------------------------------------------------------------

static bool validate_row(uint8_t powerLevel)
{
    const uint32_t* row = g_powerPeriodTable[powerLevel - 1];
    for (uint32_t index = 0; index < PWR_TABLE_ROW_LENGTH; ++index)
    {
        const uint32_t top = row[index];
        if ((top < kPwmHighClocks) || (top > kPwmMaximumTop))
        {
            return false;
        }
    }

    return true;
}

static void copy_row(uint8_t powerLevel)
{
    const uint32_t* row = g_powerPeriodTable[powerLevel - 1];
    for (uint32_t index = 0; index < PWR_TABLE_ROW_LENGTH; ++index)
    {
        g_dmaPeriods[index] = row[index];
    }
}

// -----------------------------------------------------------------------------
// Safe Stop/Start
// -----------------------------------------------------------------------------

static void stop_output()
{
    if (!g_initialized)
    {
        digitalWrite(RFGEN_PIN, LOW);
        pinMode(RFGEN_PIN, OUTPUT);
        return;
    }

    if (TCC0->CTRLA.bit.ENABLE != 0)
    {
        // A buffered zero takes effect only at UPDATE. Wait for a later
        // overflow so the current pulse has finished and zero is active.
        while (TCC0->SYNCBUSY.bit.CCB0 != 0)
        {
        }
        TCC0->CCB[0].reg = 0;
        while (TCC0->SYNCBUSY.bit.CCB0 != 0)
        {
        }

        TCC0->INTFLAG.reg = TCC_INTFLAG_OVF;
        while ((TCC0->INTFLAG.reg & TCC_INTFLAG_OVF) == 0)
        {
        }
        TCC0->INTFLAG.reg = TCC_INTFLAG_OVF;

        TCC0->CTRLA.bit.ENABLE = 0;
        wait_for_tcc0_sync();
    }

    if (g_dmaRunning)
    {
        const uint32_t channelMask = 1ul << g_periodDma.getChannel();
        g_periodDma.abort();
        while ((DMAC->BUSYCH.reg & channelMask) != 0)
        {
        }
        g_dmaRunning = false;
    }

    // Disconnect TCC0 only after its active output is known low.
    digitalWrite(RFGEN_PIN, LOW);
    pinMode(RFGEN_PIN, OUTPUT);
}

static bool start_output()
{
    // TCC0 is disabled and the pin is still a GPIO held low here.
    TCC0->COUNT.reg = 0;
    wait_for_tcc0_sync();
    TCC0->PER.reg = g_dmaPeriods[0];
    wait_for_tcc0_sync();
    TCC0->PERB.reg = g_dmaPeriods[0];
    wait_for_tcc0_sync();
    TCC0->CC[0].reg = 0;
    wait_for_tcc0_sync();
    TCC0->CCB[0].reg = 0;
    wait_for_tcc0_sync();

    g_periodDma.changeDescriptor(g_periodDescriptor,
                                 g_dmaPeriods,
                                 const_cast<uint32_t*>(&TCC0->PERB.reg),
                                 PWR_TABLE_ROW_LENGTH);
    if (g_periodDma.startJob() != DMA_STATUS_OK)
    {
        return false;
    }
    g_dmaRunning = true;

    // Connect a known-low peripheral output before starting its counter.
    pinPeripheral(RFGEN_PIN, PIO_TIMER);
    TCC0->CTRLA.bit.ENABLE = 1;
    wait_for_tcc0_sync();

    // The fixed pulse width becomes active at the next PWM boundary.
    TCC0->CCB[0].reg = kPwmHighClocks;
    while (TCC0->SYNCBUSY.bit.CCB0 != 0)
    {
    }

    return true;
}

static void wait_for_tcc0_sync()
{
    while (TCC0->SYNCBUSY.reg != 0)
    {
    }
}
