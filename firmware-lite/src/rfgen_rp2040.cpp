/*
 * DMA-modulated, fixed-pulse-width RF generator for RP2040.
 *
 * One DMA channel writes the next PWM TOP value after every slice wrap. A
 * second channel reloads and retriggers the data channel after each complete
 * table row, producing an indefinite hardware-only loop.
 *
 * The RF output must never be left continuously high. Every table entry is
 * validated before use, and all reconfiguration first latches a zero compare
 * value at a PWM boundary.
 */

#include "rfgen.h"

#include <Arduino.h>
#include <hardware/dma.h>
#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <hardware/structs/dma.h>
#include <hardware/structs/pwm.h>

#include "hotwandlite.h"
#include "pwr_table.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// One undivided 133 MHz PWM period is 283 clocks, or about 469.965 kHz.
static constexpr uint32_t kPwmPeriodClocks = (F_CPU + (kRfFrequencyHz / 2)) / kRfFrequencyHz;
static constexpr uint16_t kPwmInitialTop    = static_cast<uint16_t>(kPwmPeriodClocks - 1);
static constexpr uint16_t kPwmHighClocks   = static_cast<uint16_t>(kPwmPeriodClocks / 2);

static_assert(F_CPU == 133000000, "RF generator timer settings require a 133 MHz CPU clock");
static_assert(kPwmHighClocks == 141, "Unexpected RP2040 RF pulse width");
static_assert(kPwmHighClocks <= kPwmInitialTop, "RF PWM must include a low interval");

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static bool    g_initialized   = false;
static bool    g_pwmRunning    = false;
static bool    g_dmaRunning    = false;
static uint8_t g_currentLevel  = RFGEN_POWER_OFF;
static uint    g_pwmSlice      = 0;
static uint    g_pwmChannel    = 0;
static int     g_dataDma       = -1;
static int     g_reloadDma     = -1;

alignas(16) static uint32_t g_dmaPeriods[PWR_TABLE_ROW_LENGTH];
static uint32_t             g_dmaReadAddress = 0;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static bool initialize_hardware();
static bool initialize_dma();
static bool validate_row(uint8_t powerLevel);
static void copy_row(uint8_t powerLevel);
static void stop_output();
static void stop_dma();
static void start_output();

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
    start_output();
    g_currentLevel = powerLevel;
}

// -----------------------------------------------------------------------------
// Hardware Setup
// -----------------------------------------------------------------------------

static bool initialize_hardware()
{
    // Load the GPIO output latch low before PWM is connected to the pad.
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

// -----------------------------------------------------------------------------
// Row Management
// -----------------------------------------------------------------------------

static bool validate_row(uint8_t powerLevel)
{
    const uint32_t* row = g_powerPeriodTable[powerLevel - 1];
    for (uint32_t index = 0; index < PWR_TABLE_ROW_LENGTH; ++index)
    {
        const uint32_t top = row[index];
        if ((top < kPwmHighClocks) || (top > UINT16_MAX))
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

    g_dmaReadAddress = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_dmaPeriods));
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

    if (g_pwmRunning)
    {
        // A compare write is double-buffered. Wait for a subsequent wrap so
        // the zero compare is active before disabling the slice.
        pwm_set_chan_level(g_pwmSlice, g_pwmChannel, 0);
        pwm_clear_irq(g_pwmSlice);
        while ((pwm_hw->intr & (1ul << g_pwmSlice)) == 0)
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

static void stop_dma()
{
    if (!g_dmaRunning)
    {
        return;
    }

    // Disable both channels before aborting so neither can retrigger the other
    // while a row change is in progress.
    hw_clear_bits(&dma_hw->ch[g_reloadDma].ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS);
    hw_clear_bits(&dma_hw->ch[g_dataDma].ctrl_trig, DMA_CH0_CTRL_TRIG_EN_BITS);
    dma_channel_abort(static_cast<uint>(g_reloadDma));
    dma_channel_abort(static_cast<uint>(g_dataDma));
    g_dmaRunning = false;
}

static void start_output()
{
    // PWM is disabled and the pin is still a GPIO held low here.
    pwm_set_counter(g_pwmSlice, 0);
    pwm_set_wrap(g_pwmSlice, static_cast<uint16_t>(g_dmaPeriods[0]));
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
                          g_dmaPeriods,
                          PWR_TABLE_ROW_LENGTH,
                          false);

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

    // Connect a known-low PWM output before starting its counter.
    gpio_set_function(RFGEN_PIN, GPIO_FUNC_PWM);
    pwm_set_enabled(g_pwmSlice, true);
    g_pwmRunning = true;

    // The fixed pulse width becomes active at the next PWM boundary.
    pwm_set_chan_level(g_pwmSlice, g_pwmChannel, kPwmHighClocks);
}
