/*
 * DMA-modulated, fixed-pulse-width RF generator for SAMD21.
 *
 * TCC0 produces one pulse per PWM period on PA04.
 * The compare value remains
 * fixed while a circular DMA descriptor writes the next generated period to
 * PERB after
 * every TCC0 overflow.
 *
 * Live reconfiguration stages a new circular descriptor and lets the active
 * table finish before DMA selects it. Explicit stop requests still latch a
 * zero compare before disconnecting the timer pin.
 */

#include "rfgen_internal.h"

#include <Adafruit_ZeroDMA.h>
#include <Arduino.h>
#include <wiring_private.h>

#include "hotwandlite.h"

#ifdef RFGEN_MUTED_DEBUG

bool rfgen_platform_start(const uint32_t*, uint16_t)
{
    return false;
}

bool rfgen_platform_change(const uint32_t*, uint16_t)
{
    return false;
}

void rfgen_platform_stop(void) {}

#else

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// GCLK0 is 48 MHz. 102 clocks produce approximately 470.588 kHz.
static constexpr uint32_t kPwmPeriodClocks = (F_CPU + (RFGEN_FREQUENCY_HZ / 2u)) / RFGEN_FREQUENCY_HZ;
static constexpr uint32_t kPwmInitialTop   = kPwmPeriodClocks - 1u;
static constexpr uint32_t kPwmHighClocks   = kPwmPeriodClocks / 2u;
static constexpr uint32_t kPwmMaximumTop   = TCC_PER_PER_Msk;

static_assert(F_CPU == 48000000, "RF generator timer settings require a 48 MHz CPU clock");
static_assert(kPwmHighClocks == 51, "Unexpected SAMD21 RF pulse width");
static_assert(kPwmHighClocks <= kPwmInitialTop, "RF PWM must include a low interval");
static_assert(RFGEN_TABLE_CAPACITY <= UINT16_MAX, "RF table does not fit a DMA descriptor");

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static bool             g_initialized = false;
static bool             g_dmaRunning  = false;
static Adafruit_ZeroDMA g_periodDma;
static DmacDescriptor*  g_periodDescriptor = nullptr;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static bool initialize_hardware(const uint32_t* periodTable, uint16_t periodCount);
static bool initialize_dma(const uint32_t* periodTable, uint16_t periodCount);
static bool validate_table(const uint32_t* periodTable, uint16_t periodCount);
static bool start_output(const uint32_t* periodTable, uint16_t periodCount);
static void change_output(const uint32_t* periodTable, uint16_t periodCount);
static void wait_for_tcc0_sync();

// -----------------------------------------------------------------------------
// Platform Interface
// -----------------------------------------------------------------------------

bool rfgen_platform_start(const uint32_t* periodTable, uint16_t periodCount)
{
    if (!validate_table(periodTable, periodCount))
    {
        return false;
    }

    if (!g_initialized && !initialize_hardware(periodTable, periodCount))
    {
        return false;
    }

    // The shared controller has already stopped the previous waveform. Keep
    // this backend defensive if it is ever called directly.
    rfgen_platform_stop();
    return start_output(periodTable, periodCount);
}

bool rfgen_platform_change(const uint32_t* periodTable, uint16_t periodCount)
{
    if (!validate_table(periodTable, periodCount) || !g_dmaRunning || (TCC0->CTRLA.bit.ENABLE == 0u))
    {
        return false;
    }

    change_output(periodTable, periodCount);
    return true;
}

void rfgen_platform_stop(void)
{
    if (!g_initialized)
    {
        digitalWrite(RFGEN_PIN, LOW);
        pinMode(RFGEN_PIN, OUTPUT);
        return;
    }

    if (TCC0->CTRLA.bit.ENABLE != 0u)
    {
        // CCB0 transfers only at UPDATE. Waiting for a later overflow ensures
        // the current fixed-width pulse has ended and zero is active.
        while (TCC0->SYNCBUSY.bit.CCB0 != 0u)
        {
        }
        TCC0->CCB[0].reg = 0;
        while (TCC0->SYNCBUSY.bit.CCB0 != 0u)
        {
        }

        TCC0->INTFLAG.reg = TCC_INTFLAG_OVF;
        while ((TCC0->INTFLAG.reg & TCC_INTFLAG_OVF) == 0u)
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
        while ((DMAC->BUSYCH.reg & channelMask) != 0u)
        {
        }
        g_dmaRunning = false;
    }

    // Disconnect TCC0 only after its active output is known low.
    digitalWrite(RFGEN_PIN, LOW);
    pinMode(RFGEN_PIN, OUTPUT);
}

// -----------------------------------------------------------------------------
// Hardware Setup
// -----------------------------------------------------------------------------

static bool initialize_hardware(const uint32_t* periodTable, uint16_t periodCount)
{
    digitalWrite(RFGEN_PIN, LOW);
    pinMode(RFGEN_PIN, OUTPUT);

    PM->APBCMASK.reg |= PM_APBCMASK_TCC0;

    GCLK->CLKCTRL.reg = static_cast<uint16_t>(GCLK_CLKCTRL_CLKEN | GCLK_CLKCTRL_GEN_GCLK0 | GCLK_CLKCTRL_ID_TCC0_TCC1);
    while (GCLK->STATUS.bit.SYNCBUSY != 0u)
    {
    }

    TCC0->CTRLA.reg = TCC_CTRLA_SWRST;
    while ((TCC0->SYNCBUSY.bit.SWRST != 0u) || (TCC0->CTRLA.bit.SWRST != 0u))
    {
    }

    TCC0->CTRLA.reg = TCC_CTRLA_PRESCALER_DIV1 | TCC_CTRLA_PRESCSYNC_GCLK;
    TCC0->WAVE.reg  = TCC_WAVE_WAVEGEN_NPWM;
    wait_for_tcc0_sync();

    TCC0->PER.reg = kPwmInitialTop;
    wait_for_tcc0_sync();
    TCC0->CC[0].reg = 0;
    wait_for_tcc0_sync();

    if (!initialize_dma(periodTable, periodCount))
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

static bool initialize_dma(const uint32_t* periodTable, uint16_t periodCount)
{
    g_periodDma.setTrigger(TCC0_DMAC_ID_OVF);
    g_periodDma.setAction(DMA_TRIGGER_ACTON_BEAT);
    g_periodDma.loop(true);

    if (g_periodDma.allocate() != DMA_STATUS_OK)
    {
        return false;
    }

    g_periodDma.setPriority(DMA_PRIORITY_3);
    g_periodDescriptor = g_periodDma.addDescriptor(const_cast<uint32_t*>(periodTable),
                                                   const_cast<uint32_t*>(&TCC0->PERB.reg),
                                                   periodCount,
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

static bool validate_table(const uint32_t* periodTable, uint16_t periodCount)
{
    if ((periodTable == nullptr) || (periodCount == 0u) || (periodCount > RFGEN_TABLE_CAPACITY))
    {
        return false;
    }

    for (uint16_t index = 0; index < periodCount; ++index)
    {
        const uint32_t top = periodTable[index];
        if ((top < kPwmHighClocks) || (top > kPwmMaximumTop))
        {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// DMA and PWM Start
// -----------------------------------------------------------------------------

static bool start_output(const uint32_t* periodTable, uint16_t periodCount)
{
    // TCC0 is disabled and the pin remains a GPIO held low here.
    TCC0->COUNT.reg = 0;
    wait_for_tcc0_sync();
    TCC0->PER.reg = periodTable[0];
    wait_for_tcc0_sync();
    TCC0->PERB.reg = periodTable[0];
    wait_for_tcc0_sync();
    TCC0->CC[0].reg = 0;
    wait_for_tcc0_sync();
    TCC0->CCB[0].reg = 0;
    wait_for_tcc0_sync();

    g_periodDma.changeDescriptor(g_periodDescriptor,
                                 const_cast<uint32_t*>(periodTable),
                                 const_cast<uint32_t*>(&TCC0->PERB.reg),
                                 periodCount);
    if (g_periodDma.startJob() != DMA_STATUS_OK)
    {
        return false;
    }
    g_dmaRunning = true;

    pinPeripheral(RFGEN_PIN, PIO_TIMER);
    TCC0->CTRLA.bit.ENABLE = 1;
    wait_for_tcc0_sync();

    // The fixed pulse width becomes active at the next PWM boundary.
    TCC0->CCB[0].reg = kPwmHighClocks;
    while (TCC0->SYNCBUSY.bit.CCB0 != 0u)
    {
    }

    return true;
}

static void change_output(const uint32_t* periodTable, uint16_t periodCount)
{
    const uint8_t channel = g_periodDma.getChannel();
    volatile DmacDescriptor* const writebackDescriptors =
        reinterpret_cast<volatile DmacDescriptor*>(DMAC->WRBADDR.reg);
    volatile DmacDescriptor& writeback = writebackDescriptors[channel];

    /*
     * The active descriptor runs from the DMAC writeback area, leaving the
     * circular base descriptor free to stage the next table. Reserve several
     * carrier periods for the descriptor writes, then wait for the old block's
     * completion flag. TCC0 and its fixed compare never stop.
     */
    uint32_t interruptState = 0;
    for (;;)
    {
        while (writeback.BTCNT.reg <= 4u)
        {
        }

        interruptState = __get_PRIMASK();
        __disable_irq();
        if (writeback.BTCNT.reg > 4u)
        {
            break;
        }
        if (interruptState == 0u)
        {
            __enable_irq();
        }
    }

    DMAC->CHID.bit.ID     = channel;
    DMAC->CHINTFLAG.reg   = DMAC_CHINTFLAG_TCMPL;
    g_periodDma.changeDescriptor(g_periodDescriptor,
                                 const_cast<uint32_t*>(periodTable),
                                 const_cast<uint32_t*>(&TCC0->PERB.reg),
                                 periodCount);
    __DMB();
    if (interruptState == 0u)
    {
        __enable_irq();
    }

    do
    {
        DMAC->CHID.bit.ID = channel;
    } while ((DMAC->CHINTFLAG.reg & DMAC_CHINTFLAG_TCMPL) == 0u);
    DMAC->CHINTFLAG.reg = DMAC_CHINTFLAG_TCMPL;
}

static void wait_for_tcc0_sync()
{
    while (TCC0->SYNCBUSY.reg != 0u)
    {
    }
}

#endif // RFGEN_MUTED_DEBUG
