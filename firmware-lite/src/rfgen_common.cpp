/*
 * Shared RF power-table generation and transition control.
 *
 * The first carrier run models the inductor's finite
 * energy build-up. A long
 * PWM period is treated as a blank even though it necessarily begins with one
 * fixed-width
 * pulse; that singular pulse is intentionally ignored by the
 * modeled average-power calculation.
 */

#include "rfgen_internal.h"

#include <limits.h>

#ifndef RFGEN_UNIT_TEST
#include <Arduino.h>

#include "hotwandlite.h"
#endif

// -----------------------------------------------------------------------------
// Configuration derived from the selected target
// -----------------------------------------------------------------------------

#ifdef RFGEN_UNIT_TEST
static constexpr uint32_t kPwmPeriodClocks = 102u;
static constexpr uint32_t kMaximumPwmTop   = 0xFFFFFFu;
#elif defined(HOT_WAND_TARGET_XIAO_SAMD21)
static constexpr uint32_t kPwmPeriodClocks = (F_CPU + (RFGEN_FREQUENCY_HZ / 2u)) / RFGEN_FREQUENCY_HZ;
static constexpr uint32_t kMaximumPwmTop   = 0xFFFFFFu;
#elif defined(HOT_WAND_TARGET_XIAO_RP2040) || defined(HOT_WAND_TARGET_WAVESHARE_RP2040_ZERO)
static constexpr uint32_t kPwmPeriodClocks = (F_CPU + (RFGEN_FREQUENCY_HZ / 2u)) / RFGEN_FREQUENCY_HZ;
static constexpr uint32_t kMaximumPwmTop   = UINT16_MAX;
#elif defined(HOT_WAND_TARGET_XIAO_ESP32S3) || defined(HOT_WAND_TARGET_XIAO_ESP32C3)
// The RMT carrier uses the 80 MHz APB clock. RMT durations are 15-bit.
static constexpr uint32_t kEsp32RmtClockHz = 80000000u;
static constexpr uint32_t kPwmPeriodClocks =
    (kEsp32RmtClockHz + (RFGEN_FREQUENCY_HZ / 2u)) / RFGEN_FREQUENCY_HZ;
static constexpr uint32_t kMaximumPwmTop = 0x7FFFu;
#else
#error "Select exactly one supported microcontroller"
#endif

static constexpr uint32_t kPwmTop = kPwmPeriodClocks - 1u;

static_assert(RFGEN_STARTUP_PERIOD_COUNT > 0u, "RF startup run must not be empty");
static_assert(RFGEN_STARTUP_PERIOD_COUNT < RFGEN_TABLE_CAPACITY, "RF table must leave room for a blank");
static_assert(RFGEN_STARTUP_POWER_PERCENT <= RFGEN_MAXIMUM_POWER_PERCENT, "Invalid RF startup power");
static_assert(RFGEN_MINIMUM_POWER_PERCENT > 0u, "Minimum nonzero RF power must be positive");
static_assert((RFGEN_MINIMUM_POWER_PERCENT * (RFGEN_STARTUP_PERIOD_COUNT + RFGEN_MINIMUM_BLANK_PERIOD_COUNT)) <=
                  (RFGEN_STARTUP_PERIOD_COUNT * RFGEN_STARTUP_POWER_PERCENT),
              "Low-power waveform crossover must remain reachable");
static_assert((RFGEN_CONTINUOUS_POWER_PERCENT * (RFGEN_STARTUP_PERIOD_COUNT + RFGEN_MINIMUM_BLANK_PERIOD_COUNT)) >
                  (RFGEN_STARTUP_PERIOD_COUNT * RFGEN_STARTUP_POWER_PERCENT),
              "Continuous RF threshold must exceed the crossover");
static_assert(RFGEN_CONTINUOUS_POWER_PERCENT <= RFGEN_MAXIMUM_POWER_PERCENT,
              "Continuous RF threshold exceeds the public range");
static_assert(RFGEN_TABLE_CAPACITY <= UINT16_MAX, "RF DMA table count does not fit uint16_t");
static_assert(kPwmPeriodClocks > 1u, "RF PWM period is too short");
static_assert(kPwmTop <= kMaximumPwmTop, "RF PWM period does not fit the target counter");

// -----------------------------------------------------------------------------
// Shared waveform and state
// -----------------------------------------------------------------------------

uint16_t g_rfgenPeriodCount = 0;

namespace
{
alignas(16) uint32_t g_primaryPeriodTable[RFGEN_TABLE_CAPACITY];
alignas(16) uint32_t g_alternatePeriodTable[RFGEN_TABLE_CAPACITY];

uint8_t g_requestedPowerPercent  = 0;
uint8_t g_normalizedPowerPercent = 0;
uint8_t g_appliedPowerPercent    = 0;
const uint32_t* g_activePeriodTable = nullptr;

static uint64_t absolute_difference(uint64_t left, uint64_t right);
static bool     candidate_is_better(uint64_t candidateNumerator,
                                    uint64_t candidateDenominator,
                                    uint64_t bestNumerator,
                                    uint64_t bestDenominator,
                                    uint8_t  requestedPowerPercent);
static uint32_t choose_low_power_blank(uint8_t powerPercent);
static uint32_t choose_extra_full_periods(uint8_t powerPercent, uint16_t maximumExtraPeriods);
static bool     append_full_periods(
    uint32_t* periodTable, uint16_t tableCapacity, uint16_t* periodCount, uint32_t fullPeriodCount, uint32_t pwmTop);
static bool append_blank(uint32_t* periodTable,
                         uint16_t  tableCapacity,
                         uint16_t* periodCount,
                         uint64_t  blankPeriodCount,
                         uint32_t  pwmPeriodClocks,
                         uint32_t  maximumPwmTop);
static uint32_t* inactive_period_table();
static void force_output_low();
} // namespace

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void rfgen_set(uint8_t powerPercent)
{
    const uint8_t normalizedPowerPercent = rfgen_normalize_power_percent(powerPercent);

    g_requestedPowerPercent  = powerPercent;
    g_normalizedPowerPercent = normalizedPowerPercent;

    if (normalizedPowerPercent == 0u)
    {
#ifdef RFGEN_MUTED_DEBUG
        force_output_low();
#else
        rfgen_platform_stop();
#endif
        g_rfgenPeriodCount    = 0;
        g_appliedPowerPercent = 0;
        g_activePeriodTable   = nullptr;
        return;
    }

    if (normalizedPowerPercent == g_appliedPowerPercent)
    {
        return;
    }

    uint32_t* nextPeriodTable = inactive_period_table();
    uint16_t  nextPeriodCount = 0;
    if (!rfgen_generate_period_table(normalizedPowerPercent,
                                     kPwmPeriodClocks,
                                     kMaximumPwmTop,
                                     nextPeriodTable,
                                     RFGEN_TABLE_CAPACITY,
                                     &nextPeriodCount))
    {
        if (g_appliedPowerPercent == 0u)
        {
            g_rfgenPeriodCount = 0;
            force_output_low();
        }
        return;
    }

#ifdef RFGEN_MUTED_DEBUG
    // Muted builds retain the generated data for inspection but never touch a
    // timer or DMA peripheral.
    force_output_low();
    g_activePeriodTable   = nextPeriodTable;
    g_rfgenPeriodCount    = nextPeriodCount;
    g_appliedPowerPercent = normalizedPowerPercent;
#else
    const bool platformSucceeded = (g_appliedPowerPercent == 0u)
                                       ? rfgen_platform_start(nextPeriodTable, nextPeriodCount)
                                       : rfgen_platform_change(nextPeriodTable, nextPeriodCount);
    if (!platformSucceeded)
    {
        // A failed initial start returns to the safe off state. A failed live
        // handoff leaves the previous waveform running and its buffer intact.
        if (g_appliedPowerPercent == 0u)
        {
            rfgen_platform_stop();
            g_rfgenPeriodCount  = 0;
            g_activePeriodTable = nullptr;
        }
        return;
    }

    g_activePeriodTable   = nextPeriodTable;
    g_rfgenPeriodCount    = nextPeriodCount;
    g_appliedPowerPercent = normalizedPowerPercent;
#endif
}

void rfgen_print_table(void)
{
#ifndef RFGEN_UNIT_TEST
    Serial.print("RFGEN requested=");
    Serial.print(g_requestedPowerPercent);
    Serial.print(", normalized=");
    Serial.print(g_normalizedPowerPercent);
    Serial.print(", applied=");
    Serial.print(g_appliedPowerPercent);
    Serial.print(", entries=");
    Serial.println(g_rfgenPeriodCount);

    for (uint16_t index = 0; index < g_rfgenPeriodCount; ++index)
    {
        const uint32_t top            = g_activePeriodTable[index];
        const uint32_t carrierPeriods = (top + 1u) / kPwmPeriodClocks;

        Serial.print(index);
        Serial.print(": TOP=");
        Serial.print(top);
        Serial.print(", periods=");
        Serial.println(carrierPeriods);
    }
#endif
}

// -----------------------------------------------------------------------------
// Generator
// -----------------------------------------------------------------------------

uint8_t rfgen_normalize_power_percent(uint8_t powerPercent)
{
    if (powerPercent > RFGEN_MAXIMUM_POWER_PERCENT)
    {
        powerPercent = RFGEN_MAXIMUM_POWER_PERCENT;
    }
    if ((powerPercent > 0u) && (powerPercent < RFGEN_MINIMUM_POWER_PERCENT))
    {
        powerPercent = RFGEN_MINIMUM_POWER_PERCENT;
    }
    return powerPercent;
}

bool rfgen_generate_period_table(uint8_t   normalizedPowerPercent,
                                 uint32_t  pwmPeriodClocks,
                                 uint32_t  maximumPwmTop,
                                 uint32_t* periodTable,
                                 uint16_t  tableCapacity,
                                 uint16_t* periodCount)
{
    if ((periodTable == nullptr) || (periodCount == nullptr) || (pwmPeriodClocks == 0u) ||
        (tableCapacity < RFGEN_STARTUP_PERIOD_COUNT))
    {
        return false;
    }

    normalizedPowerPercent = rfgen_normalize_power_percent(normalizedPowerPercent);
    *periodCount           = 0;

    if (normalizedPowerPercent == 0u)
    {
        return true;
    }

    const uint64_t pwmTopWide = static_cast<uint64_t>(pwmPeriodClocks) - 1u;
    if (pwmTopWide > maximumPwmTop)
    {
        return false;
    }
    const uint32_t pwmTop = static_cast<uint32_t>(pwmTopWide);

    if (!append_full_periods(periodTable, tableCapacity, periodCount, RFGEN_STARTUP_PERIOD_COUNT, pwmTop))
    {
        return false;
    }

    if (normalizedPowerPercent >= RFGEN_CONTINUOUS_POWER_PERCENT)
    {
        return true;
    }

    uint64_t blankPeriodCount = RFGEN_MINIMUM_BLANK_PERIOD_COUNT;
    constexpr uint32_t kStartupPowerUnits = RFGEN_STARTUP_PERIOD_COUNT * RFGEN_STARTUP_POWER_PERCENT;
    constexpr uint32_t kBaseTotalPeriods  = RFGEN_STARTUP_PERIOD_COUNT + RFGEN_MINIMUM_BLANK_PERIOD_COUNT;
    if ((static_cast<uint32_t>(normalizedPowerPercent) * kBaseTotalPeriods) <= kStartupPowerUnits)
    {
        blankPeriodCount = choose_low_power_blank(normalizedPowerPercent);
    }
    else
    {
        // Reserve one table entry for the mandatory blank. The current limits
        // need only one, while append_blank() handles target-sized chunking.
        if (*periodCount >= tableCapacity)
        {
            return false;
        }
        const uint16_t maximumExtraPeriods = tableCapacity - *periodCount - 1u;
        const uint32_t extraFullPeriods    = choose_extra_full_periods(normalizedPowerPercent, maximumExtraPeriods);
        if (!append_full_periods(periodTable, tableCapacity, periodCount, extraFullPeriods, pwmTop))
        {
            return false;
        }
    }

    return append_blank(periodTable, tableCapacity, periodCount, blankPeriodCount, pwmPeriodClocks, maximumPwmTop);
}

namespace
{
static uint64_t absolute_difference(uint64_t left, uint64_t right)
{
    return (left >= right) ? (left - right) : (right - left);
}

static bool candidate_is_better(uint64_t candidateNumerator,
                                uint64_t candidateDenominator,
                                uint64_t bestNumerator,
                                uint64_t bestDenominator,
                                uint8_t  requestedPowerPercent)
{
    const uint64_t candidateError =
        absolute_difference(candidateNumerator, static_cast<uint64_t>(requestedPowerPercent) * candidateDenominator);
    const uint64_t bestError =
        absolute_difference(bestNumerator, static_cast<uint64_t>(requestedPowerPercent) * bestDenominator);

    const uint64_t candidateScaledError = candidateError * bestDenominator;
    const uint64_t bestScaledError      = bestError * candidateDenominator;
    if (candidateScaledError != bestScaledError)
    {
        return candidateScaledError < bestScaledError;
    }

    // Exact error ties choose the lower delivered power.
    return (candidateNumerator * bestDenominator) < (bestNumerator * candidateDenominator);
}

static uint32_t choose_low_power_blank(uint8_t powerPercent)
{
    constexpr uint64_t kStartupPowerUnits =
        static_cast<uint64_t>(RFGEN_STARTUP_PERIOD_COUNT) * RFGEN_STARTUP_POWER_PERCENT;
    constexpr uint32_t kMinimumTotalPeriods = RFGEN_STARTUP_PERIOD_COUNT + RFGEN_MINIMUM_BLANK_PERIOD_COUNT;

    uint32_t lowerTotalPeriods = static_cast<uint32_t>(kStartupPowerUnits / powerPercent);
    if (lowerTotalPeriods < kMinimumTotalPeriods)
    {
        lowerTotalPeriods = kMinimumTotalPeriods;
    }
    uint32_t upperTotalPeriods = lowerTotalPeriods;
    if ((kStartupPowerUnits % powerPercent) != 0u)
    {
        ++upperTotalPeriods;
    }

    uint32_t bestTotalPeriods = lowerTotalPeriods;
    if ((upperTotalPeriods != lowerTotalPeriods) &&
        candidate_is_better(kStartupPowerUnits, upperTotalPeriods, kStartupPowerUnits, lowerTotalPeriods, powerPercent))
    {
        bestTotalPeriods = upperTotalPeriods;
    }

    return bestTotalPeriods - RFGEN_STARTUP_PERIOD_COUNT;
}

static uint32_t choose_extra_full_periods(uint8_t powerPercent, uint16_t maximumExtraPeriods)
{
    constexpr uint64_t kStartupPowerUnits =
        static_cast<uint64_t>(RFGEN_STARTUP_PERIOD_COUNT) * RFGEN_STARTUP_POWER_PERCENT;
    constexpr uint32_t kBaseTotalPeriods = RFGEN_STARTUP_PERIOD_COUNT + RFGEN_MINIMUM_BLANK_PERIOD_COUNT;

    const uint64_t idealNumerator   = (static_cast<uint64_t>(powerPercent) * kBaseTotalPeriods) - kStartupPowerUnits;
    const uint32_t idealDenominator = RFGEN_MAXIMUM_POWER_PERCENT - powerPercent;

    uint32_t lowerExtraPeriods = static_cast<uint32_t>(idealNumerator / idealDenominator);
    if (lowerExtraPeriods > maximumExtraPeriods)
    {
        lowerExtraPeriods = maximumExtraPeriods;
    }

    uint32_t upperExtraPeriods = lowerExtraPeriods;
    if (((idealNumerator % idealDenominator) != 0u) && (upperExtraPeriods < maximumExtraPeriods))
    {
        ++upperExtraPeriods;
    }

    uint32_t bestExtraPeriods = lowerExtraPeriods;
    if (upperExtraPeriods != lowerExtraPeriods)
    {
        const uint64_t lowerPowerUnits =
            kStartupPowerUnits + (static_cast<uint64_t>(lowerExtraPeriods) * RFGEN_MAXIMUM_POWER_PERCENT);
        const uint64_t upperPowerUnits =
            kStartupPowerUnits + (static_cast<uint64_t>(upperExtraPeriods) * RFGEN_MAXIMUM_POWER_PERCENT);

        if (candidate_is_better(upperPowerUnits,
                                kBaseTotalPeriods + upperExtraPeriods,
                                lowerPowerUnits,
                                kBaseTotalPeriods + lowerExtraPeriods,
                                powerPercent))
        {
            bestExtraPeriods = upperExtraPeriods;
        }
    }

    return bestExtraPeriods;
}

static bool append_full_periods(
    uint32_t* periodTable, uint16_t tableCapacity, uint16_t* periodCount, uint32_t fullPeriodCount, uint32_t pwmTop)
{
    if (fullPeriodCount > static_cast<uint32_t>(tableCapacity - *periodCount))
    {
        return false;
    }

    for (uint32_t index = 0; index < fullPeriodCount; ++index)
    {
        periodTable[*periodCount] = pwmTop;
        ++(*periodCount);
    }
    return true;
}

static bool append_blank(uint32_t* periodTable,
                         uint16_t  tableCapacity,
                         uint16_t* periodCount,
                         uint64_t  blankPeriodCount,
                         uint32_t  pwmPeriodClocks,
                         uint32_t  maximumPwmTop)
{
    if (*periodCount >= tableCapacity)
    {
        return false;
    }

    const uint64_t maximumBlankPeriods = (static_cast<uint64_t>(maximumPwmTop) + 1u) / pwmPeriodClocks;
    if (maximumBlankPeriods < RFGEN_MINIMUM_BLANK_PERIOD_COUNT)
    {
        return false;
    }

    if (blankPeriodCount < RFGEN_MINIMUM_BLANK_PERIOD_COUNT)
    {
        blankPeriodCount = RFGEN_MINIMUM_BLANK_PERIOD_COUNT;
    }

    const uint16_t availableEntries = tableCapacity - *periodCount;
    uint64_t       chunkCount       = (blankPeriodCount + maximumBlankPeriods - 1u) / maximumBlankPeriods;
    if (chunkCount > availableEntries)
    {
        // Best-effort floor: consume every remaining entry at the longest
        // target-supported blank, accepting the resulting higher minimum power.
        chunkCount       = availableEntries;
        blankPeriodCount = chunkCount * maximumBlankPeriods;
    }

    // With the supported counters, the shortest balanced chunk is comfortably
    // above the minimum. Keep this correction for future configurations.
    if (blankPeriodCount < (chunkCount * RFGEN_MINIMUM_BLANK_PERIOD_COUNT))
    {
        blankPeriodCount = chunkCount * RFGEN_MINIMUM_BLANK_PERIOD_COUNT;
    }

    const uint64_t baseChunkPeriods = blankPeriodCount / chunkCount;
    const uint64_t longerChunkCount = blankPeriodCount % chunkCount;
    if ((baseChunkPeriods < RFGEN_MINIMUM_BLANK_PERIOD_COUNT) ||
        ((baseChunkPeriods + ((longerChunkCount > 0u) ? 1u : 0u)) > maximumBlankPeriods))
    {
        return false;
    }

    for (uint64_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
    {
        const uint64_t chunkPeriods = baseChunkPeriods + ((chunkIndex < longerChunkCount) ? 1u : 0u);
        const uint64_t topWide      = (chunkPeriods * pwmPeriodClocks) - 1u;
        if (topWide > maximumPwmTop)
        {
            return false;
        }

        periodTable[*periodCount] = static_cast<uint32_t>(topWide);
        ++(*periodCount);
    }

    return true;
}

static uint32_t* inactive_period_table()
{
    return (g_activePeriodTable == g_primaryPeriodTable) ? g_alternatePeriodTable : g_primaryPeriodTable;
}

static void force_output_low()
{
#ifdef RFGEN_UNIT_TEST
    rfgen_test_force_output_low();
#else
    digitalWrite(RFGEN_PIN, LOW);
    pinMode(RFGEN_PIN, OUTPUT);
#endif
}

} // namespace

#ifdef RFGEN_UNIT_TEST
void rfgen_test_reset_state(void)
{
    g_requestedPowerPercent  = 0;
    g_normalizedPowerPercent = 0;
    g_appliedPowerPercent    = 0;
    g_rfgenPeriodCount       = 0;
    g_activePeriodTable      = nullptr;
}

bool rfgen_test_append_blank(uint32_t* periodTable,
                             uint16_t  tableCapacity,
                             uint16_t* periodCount,
                             uint64_t  blankPeriodCount,
                             uint32_t  pwmPeriodClocks,
                             uint32_t  maximumPwmTop)
{
    return append_blank(periodTable, tableCapacity, periodCount, blankPeriodCount, pwmPeriodClocks, maximumPwmTop);
}
#endif
