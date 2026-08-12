/*
 * DMA-modulated, fixed-pulse-width RF generator for RP2040.
 *
 * One DMA channel writes the next PWM TOP value
 * after every slice wrap. A
 * second channel restores the table read address and retriggers the data
 * channel,
 * producing an indefinite hardware-only loop of the generated table.
 *
 * The RF output must never be left
 * continuously high. Reconfiguration first
 * latches a zero compare at a PWM boundary, then disconnects a known-low
 * pin.
 */

#include "rfgen_internal.h"

#include <Arduino.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <hardware/structs/dma.h>
#include <hardware/structs/pwm.h>

#include "hotwandlite.h"

#ifdef RFGEN_MUTED_DEBUG

bool rfgen_platform_start(const uint32_t*, uint16_t)
{
    return false;
}

void rfgen_platform_stop(void) {}

#else

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// One undivided 133 MHz PWM period is 283 clocks, or about 469.965 kHz.
static constexpr uint32_t kPwmPeriodClocks = (F_CPU + (RFGEN_FREQUENCY_HZ / 2u)) / RFGEN_FREQUENCY_HZ;
static constexpr uint16_t kPwmInitialTop   = static_cast<uint16_t>(kPwmPeriodClocks - 1u);
static constexpr uint16_t kPwmHighClocks   = static_cast<uint16_t>(kPwmPeriodClocks / 2u);

static_assert(F_CPU == 133000000, "RF generator timer settings require a 133 MHz CPU clock");
static_assert(kPwmHighClocks == 141, "Unexpected RP2040 RF pulse width");
static_assert(kPwmHighClocks <= kPwmInitialTop, "RF PWM must include a low interval");

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static bool g_initialized = false;
static bool g_pwmRunning  = false;
static bool g_dmaRunning  = false;
static uint g_pwmSlice    = 0;
static uint g_pwmChannel  = 0;
static int  g_dataDma     = -1;
static int  g_reloadDma   = -1;

static uint32_t g_dmaReadAddress = 0;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static bool initialize_hardware();
static bool initialize_dma();
static bool validate_table(const uint32_t* periodTable, uint16_t periodCount);
static void stop_dma();
static void start_output(const uint32_t* periodTable, uint16_t periodCount);

// -----------------------------------------------------------------------------
// Platform Interface
// -----------------------------------------------------------------------------

bool rfgen_platform_start(const uint32_t* periodTable, uint16_t periodCount)
{
    if (!validate_table(periodTable, periodCount))
    {
        return false;
    }

    if (!g_initialized && !initialize_hardware())
    {
        return false;
    }

    // The shared controller has already stopped the previous waveform. Keep
    // this backend defensive if it is ever called directly.
    rfgen_platform_stop();
    start_output(periodTable, periodCount);
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

    if (g_pwmRunning)
    {
        // The compare write is double-buffered. Waiting for the next wrap
        // guarantees the current high pulse has ended and zero is active.
        pwm_set_chan_level(g_pwmSlice, g_pwmChannel, 0);
        pwm_clear_irq(g_pwmSlice);
        while ((pwm_hw->intr & (1ul << g_pwmSlice)) == 0u)
        {
        }
        pwm_clear_irq(g_pwmSlice);

        pwm_set_enabled(g_pwmSlice, false);
        g_pwmRunning = false;
    }

    stop_dma();

    // Disconnect PWM only after its active output is known low.
    digitalWrite(RFGEN_PIN, LOW);
    pinMode(RFGEN_PIN, OUTPUT);
}

// -----------------------------------------------------------------------------
// Hardware Setup
// -----------------------------------------------------------------------------

static bool initialize_hardware()
{
    digitalWrite(RFGEN_PIN, LOW);
    pinMode(RFGEN_PIN, OUTPUT);

    g_pwmSlice   = pwm_gpio_to_slice_num(RFGEN_PIN);
    g_pwmChannel = pwm_gpio_to_channel(RFGEN_PIN);

    if (!initialize_dma())
    {
        return false;
    }

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv_int(&config, 1);
    pwm_config_set_wrap(&config, kPwmInitialTop);
    pwm_init(g_pwmSlice, &config, false);
    pwm_set_chan_level(g_pwmSlice, g_pwmChannel, 0);

    // The PWM peripheral is disabled and its selected channel is known low.
    gpio_set_function(RFGEN_PIN, GPIO_FUNC_PWM);

    g_initialized = true;
    return true;
}

static bool initialize_dma()
{
    g_dataDma = dma_claim_unused_channel(false);
    if (g_dataDma < 0)
    {
        return false;
    }

    g_reloadDma = dma_claim_unused_channel(false);
    if (g_reloadDma < 0)
    {
        dma_channel_unclaim(static_cast<uint>(g_dataDma));
        g_dataDma = -1;
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
        if ((top < kPwmHighClocks) || (top > UINT16_MAX))
        {
            return false;
        }
    }
    return true;
}

// -----------------------------------------------------------------------------
// DMA and PWM Start/Stop
// -----------------------------------------------------------------------------

static void stop_dma()
{
    if (!g_dmaRunning)
    {
        return;
    }

    // Disable both channels before aborting so neither can retrigger the other
    // while the shared table is being replaced.
    hw_clear_bits(&dma_hw->ch[g_reloadDma].ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_clear_bits(&dma_hw->ch[g_dataDma].ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_abort(static_cast<uint>(g_reloadDma));
    dma_channel_abort(static_cast<uint>(g_dataDma));
    g_dmaRunning = false;
}

static void start_output(const uint32_t* periodTable, uint16_t periodCount)
{
    // PWM is disabled and the pin remains a GPIO held low here.
    pwm_set_counter(g_pwmSlice, 0);
    pwm_set_wrap(g_pwmSlice, static_cast<uint16_t>(periodTable[0]));
    pwm_set_chan_level(g_pwmSlice, g_pwmChannel, 0);

    dma_channel_config dataConfig = dma_channel_get_default_config(static_cast<uint>(g_dataDma));
    channel_config_set_transfer_data_size(&dataConfig, DMA_SIZE_32);
    channel_config_set_read_increment(&dataConfig, true);
    channel_config_set_write_increment(&dataConfig, false);
    channel_config_set_dreq(&dataConfig, pwm_get_dreq(g_pwmSlice));
    channel_config_set_chain_to(&dataConfig, static_cast<uint>(g_reloadDma));
    channel_config_set_high_priority(&dataConfig, true);
    channel_config_set_irq_quiet(&dataConfig, true);
    dma_channel_configure(static_cast<uint>(g_dataDma),
                          &dataConfig,
                          &pwm_hw->slice[g_pwmSlice].top,
                          periodTable,
                          periodCount,
                          false);

    // TRANS_COUNT automatically reloads its most recently programmed value on
    // RP2040. The control channel therefore only restores READ_ADDR and uses
    // the trigger alias to start the next variable-length table pass.
    g_dmaReadAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(periodTable));

    dma_channel_config reloadConfig = dma_channel_get_default_config(static_cast<uint>(g_reloadDma));
    channel_config_set_transfer_data_size(&reloadConfig, DMA_SIZE_32);
    channel_config_set_read_increment(&reloadConfig, false);
    channel_config_set_write_increment(&reloadConfig, false);
    channel_config_set_high_priority(&reloadConfig, true);
    channel_config_set_irq_quiet(&reloadConfig, true);
    dma_channel_configure(static_cast<uint>(g_reloadDma),
                          &reloadConfig,
                          &dma_hw->ch[g_dataDma].al3_read_addr_trig,
                          &g_dmaReadAddress,
                          1,
                          false);

    // The data channel waits for the first PWM-wrap DREQ. Thereafter its
    // completion invokes the reload channel, which retriggers it indefinitely.
    dma_start_channel_mask(1ul << g_dataDma);
    g_dmaRunning = true;

    gpio_set_function(RFGEN_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(g_pwmSlice, true);
    g_pwmRunning = true;

    // The fixed pulse width becomes active at the next PWM boundary.
    pwm_set_chan_level(g_pwmSlice, g_pwmChannel, kPwmHighClocks);
}

#endif // RFGEN_MUTED_DEBUG
