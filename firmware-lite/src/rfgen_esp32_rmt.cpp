/*
 * Shared ESP32 RMT implementation used by the XIAO ESP32-S3 and ESP32-C3.
 *
 * The common RF generator describes
 * one fixed-width carrier pulse followed by
 * an optional blank in each table entry. RMT's hardware carrier generates
 * the
 * 470 kHz pulses while a short, looped RMT envelope describes only the carrier
 * runs and blanks. This
 * compression easily fits the 48-word RMT memory on both
 * chips, including the common generator's 512-entry
 * high-power table.
 */

#if defined(HOTWANDLITE_TARGET_XIAO_ESP32S3) || defined(HOTWANDLITE_MCU_ESP32C3)

#include "rfgen.h"

#include "rfgen_esp32_rmt.h"

#include <Arduino.h>
#include <driver/rmt.h>
#include <soc/soc_caps.h>

#include "hotwandlite.h"

namespace
{
constexpr rmt_channel_t kRmtChannel        = RMT_CHANNEL_0;
constexpr uint32_t      kRmtClockHz        = 80000000u;
constexpr uint16_t      kMaximumDuration   = 0x7FFFu;

static_assert(SOC_RMT_MEM_WORDS_PER_CHANNEL >= 48, "ESP32 RMT channel memory is too small");

struct EnvelopeSegment
{
    uint16_t duration;
    bool     level;
};

struct RmtPattern
{
    rmt_item32_t items[SOC_RMT_MEM_WORDS_PER_CHANNEL];
    uint8_t      itemCount;
    uint16_t     highClocks;
    uint16_t     lowClocks;
    bool         carrierEnabled;
};

RmtPattern g_patterns[2]   = {};
int8_t     g_activePattern = -1;
bool       g_initialized   = false;
bool       g_running       = false;

bool append_segment(EnvelopeSegment* segments, uint16_t* segmentCount, bool level, uint32_t duration,
                    uint32_t carrierClocks);
bool encode_pattern(const uint32_t* periodTable, uint16_t periodCount, RmtPattern* pattern);
bool initialize_hardware(const RmtPattern& pattern);
bool transmit_pattern(const RmtPattern& pattern);
void force_idle_low();

bool append_segment(EnvelopeSegment* segments, uint16_t* segmentCount, bool level, uint32_t duration,
                    uint32_t carrierClocks)
{
    const uint32_t maximumAlignedDuration = (kMaximumDuration / carrierClocks) * carrierClocks;
    if ((duration == 0u) || ((duration % carrierClocks) != 0u))
    {
        return duration == 0u;
    }

    while (duration > 0u)
    {
        if ((*segmentCount > 0u) && (segments[*segmentCount - 1u].level == level) &&
            (segments[*segmentCount - 1u].duration < maximumAlignedDuration))
        {
            EnvelopeSegment& tail      = segments[*segmentCount - 1u];
            const uint32_t   available = maximumAlignedDuration - tail.duration;
            const uint32_t   addition  = (duration < available) ? duration : available;
            tail.duration              = static_cast<uint16_t>(tail.duration + addition);
            duration -= addition;
            continue;
        }

        if (*segmentCount >= (2u * SOC_RMT_MEM_WORDS_PER_CHANNEL))
        {
            return false;
        }

        const uint32_t chunk             = (duration < maximumAlignedDuration) ? duration : maximumAlignedDuration;
        segments[*segmentCount].duration = static_cast<uint16_t>(chunk);
        segments[*segmentCount].level    = level;
        ++(*segmentCount);
        duration -= chunk;
    }

    return true;
}

bool encode_pattern(const uint32_t* periodTable, uint16_t periodCount, RmtPattern* pattern)
{
    if ((periodTable == nullptr) || (pattern == nullptr) || (periodCount == 0u) || (periodCount > RFGEN_TABLE_CAPACITY))
    {
        return false;
    }

    // The common generator always starts its table with a full carrier period.
    const uint32_t carrierClocks = periodTable[0] + 1u;
    if ((carrierClocks < 2u) || (carrierClocks > kMaximumDuration / 2u))
    {
        return false;
    }
    pattern->highClocks = static_cast<uint16_t>(carrierClocks / 2u);
    pattern->lowClocks = static_cast<uint16_t>(carrierClocks - pattern->highClocks);
    pattern->carrierEnabled = true;

#if defined(RFGEN_ESP32C3_RMT_EXPLICIT_PULSES) || \
    (defined(HOTWANDLITE_MCU_ESP32C3) && defined(RFGEN_ESP32C3_MACRO_PULSE_BURSTS))
    // A/B diagnostic: bypass carrier modulation when every edge fits in RAM.
    // With the current power tables this covers 30-80% and continuous 100%.
    // Longer patterns retain the carrier envelope and its known phase issue.
    if (periodCount < SOC_RMT_MEM_WORDS_PER_CHANNEL)
    {
        for (uint16_t i = 0; i < periodCount; ++i)
        {
            const uint32_t duration = periodTable[i] + 1u;
            if ((duration < carrierClocks) || ((duration % carrierClocks) != 0u) ||
                ((duration - pattern->highClocks) > kMaximumDuration))
            {
                return false;
            }
            pattern->items[i].level0    = 1;
            pattern->items[i].duration0 = pattern->highClocks;
            pattern->items[i].level1    = 0;
            pattern->items[i].duration1 = duration - pattern->highClocks;
        }
        pattern->itemCount      = static_cast<uint8_t>(periodCount);
        pattern->carrierEnabled = false;
        return true;
    }
#endif

    EnvelopeSegment segments[2u * SOC_RMT_MEM_WORDS_PER_CHANNEL] = {};
    uint16_t        segmentCount                                 = 0;

    for (uint16_t index = 0; index < periodCount; ++index)
    {
        const uint32_t duration = periodTable[index] + 1u;
        if ((duration < carrierClocks) || ((duration % carrierClocks) != 0u))
        {
            return false;
        }

        if (!append_segment(segments, &segmentCount, true, carrierClocks, carrierClocks) ||
            !append_segment(segments, &segmentCount, false, duration - carrierClocks, carrierClocks))
        {
            return false;
        }
    }

    // RMT items contain two nonzero durations. Generated RF patterns always
    // leave enough room to split an odd final segment at a carrier boundary.
    if ((segmentCount & 1u) != 0u)
    {
        if ((segmentCount >= (2u * SOC_RMT_MEM_WORDS_PER_CHANNEL)) ||
            (segments[segmentCount - 1u].duration < (2u * carrierClocks)))
        {
            return false;
        }

        EnvelopeSegment& tail          = segments[segmentCount - 1u];
        uint16_t         firstDuration = static_cast<uint16_t>((tail.duration / 2u / carrierClocks) * carrierClocks);
        if (firstDuration == 0u)
        {
            firstDuration = static_cast<uint16_t>(carrierClocks);
        }

        segments[segmentCount].duration = static_cast<uint16_t>(tail.duration - firstDuration);
        segments[segmentCount].level    = tail.level;
        tail.duration                   = firstDuration;
        ++segmentCount;
    }

    const uint16_t itemCount = segmentCount / 2u;
    // Reserve one hardware-memory word for RMT's zero-duration terminator.
    if ((itemCount == 0u) || (itemCount >= SOC_RMT_MEM_WORDS_PER_CHANNEL))
    {
        return false;
    }

    for (uint16_t itemIndex = 0; itemIndex < itemCount; ++itemIndex)
    {
        const EnvelopeSegment& first  = segments[2u * itemIndex];
        const EnvelopeSegment& second = segments[(2u * itemIndex) + 1u];
        rmt_item32_t&          item   = pattern->items[itemIndex];

        item.duration0 = first.duration;
        item.level0    = first.level ? 1u : 0u;
        item.duration1 = second.duration;
        item.level1    = second.level ? 1u : 0u;
    }

    pattern->itemCount = static_cast<uint8_t>(itemCount);
    return true;
}

bool initialize_hardware(const RmtPattern& pattern)
{
    digitalWrite(RFGEN_PIN, LOW);
    pinMode(RFGEN_PIN, OUTPUT);

    rmt_config_t config                   = RMT_DEFAULT_CONFIG_TX(static_cast<gpio_num_t>(RFGEN_PIN), kRmtChannel);
    config.clk_div                        = 1u;
    config.mem_block_num                  = 1u;
    config.flags                          = 0u;
    config.tx_config.loop_en              = true;
    config.tx_config.carrier_en           = true;
    config.tx_config.carrier_freq_hz      = kRmtClockHz / (pattern.highClocks + pattern.lowClocks);
    config.tx_config.carrier_duty_percent = 50u;
    config.tx_config.carrier_level        = RMT_CARRIER_LEVEL_HIGH;
    config.tx_config.idle_output_en       = true;
    config.tx_config.idle_level           = RMT_IDLE_LEVEL_LOW;

    if (rmt_config(&config) != ESP_OK)
    {
        force_idle_low();
        return false;
    }

    if (rmt_driver_install(kRmtChannel, 0u, 0) != ESP_OK)
    {
        force_idle_low();
        return false;
    }

    if ((rmt_set_source_clk(kRmtChannel, RMT_BASECLK_APB) != ESP_OK) ||
        (rmt_set_tx_carrier(kRmtChannel, true, pattern.highClocks, pattern.lowClocks, RMT_CARRIER_LEVEL_HIGH) !=
         ESP_OK) ||
        (rmt_set_idle_level(kRmtChannel, true, RMT_IDLE_LEVEL_LOW) != ESP_OK))
    {
        rmt_driver_uninstall(kRmtChannel);
        force_idle_low();
        return false;
    }

    g_initialized = true;
    return true;
}

bool transmit_pattern(const RmtPattern& pattern)
{
    rmt_item32_t terminator = {};
    terminator.level0       = 0u;
    terminator.duration0    = 0u;

    // Direct hardware-memory writes avoid the legacy driver's TX semaphore,
    // which intentionally remains held during an indefinite loop. This makes
    // every later stop/change nonblocking on both the C3 and S3.
    if ((rmt_tx_stop(kRmtChannel) != ESP_OK) || (rmt_set_idle_level(kRmtChannel, true, RMT_IDLE_LEVEL_LOW) != ESP_OK) ||
        (rmt_set_tx_carrier(kRmtChannel,
                            pattern.carrierEnabled,
                            pattern.highClocks,
                            pattern.lowClocks,
                            RMT_CARRIER_LEVEL_HIGH) != ESP_OK) ||
        (rmt_fill_tx_items(kRmtChannel, pattern.items, pattern.itemCount, 0u) != ESP_OK) ||
        (rmt_fill_tx_items(kRmtChannel, &terminator, 1u, pattern.itemCount) != ESP_OK) ||
        (rmt_set_tx_loop_mode(kRmtChannel, true) != ESP_OK) || (rmt_tx_start(kRmtChannel, true) != ESP_OK))
    {
        g_running = false;
        force_idle_low();
        return false;
    }

    g_running = true;
    return true;
}

void force_idle_low()
{
    if (g_initialized)
    {
        rmt_set_idle_level(kRmtChannel, true, RMT_IDLE_LEVEL_LOW);
    }
    else
    {
        digitalWrite(RFGEN_PIN, LOW);
        pinMode(RFGEN_PIN, OUTPUT);
    }
}
} // namespace

namespace rfgen_esp32_rmt
{
bool start(const uint32_t* periodTable, uint16_t periodCount)
{
    const uint8_t nextPattern = (g_activePattern == 0) ? 1u : 0u;
    if (!encode_pattern(periodTable, periodCount, &g_patterns[nextPattern]))
    {
        return false;
    }

    if (!g_initialized && !initialize_hardware(g_patterns[nextPattern]))
    {
        return false;
    }

    if (g_running)
    {
        stop();
    }

    if (!transmit_pattern(g_patterns[nextPattern]))
    {
        return false;
    }

    g_activePattern = static_cast<int8_t>(nextPattern);
    return true;
}

bool change(const uint32_t* periodTable, uint16_t periodCount)
{
    if (!g_initialized || !g_running || (g_activePattern < 0))
    {
        return false;
    }

    const uint8_t previousPattern = static_cast<uint8_t>(g_activePattern);
    const uint8_t nextPattern     = (previousPattern == 0u) ? 1u : 0u;
    if (!encode_pattern(periodTable, periodCount, &g_patterns[nextPattern]))
    {
        return false;
    }

    // RMT is briefly stopped while the staged envelope is copied into its
    // hardware memory. The pin stays low during this accepted live-change gap.
    if (!transmit_pattern(g_patterns[nextPattern]))
    {
        transmit_pattern(g_patterns[previousPattern]);
        return false;
    }

    g_activePattern = static_cast<int8_t>(nextPattern);
    return true;
}

void stop()
{
    if (!g_initialized)
    {
        force_idle_low();
        g_activePattern = -1;
        return;
    }

    if (g_running)
    {
        rmt_tx_stop(kRmtChannel);
        g_running = false;
    }

    force_idle_low();
    g_activePattern = -1;
}
} // namespace rfgen_esp32_rmt

#endif
