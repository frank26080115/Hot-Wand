#include <unity.h>

#include <stdio.h>

#include "rfgen_internal.h"

namespace
{
enum class Event : uint8_t
{
    Stop,
    Start,
    Change,
    ForceLow,
};

Event           g_events[8];
uint8_t         g_eventCount      = 0;
bool            g_outputIsLow     = true;
bool            g_platformStarts  = true;
bool            g_platformChanges = true;
const uint32_t* g_platformTable   = nullptr;

void record_event(Event event)
{
    TEST_ASSERT_LESS_THAN_UINT8(sizeof(g_events) / sizeof(g_events[0]), g_eventCount);
    g_events[g_eventCount++] = event;
}

void reset_fixture()
{
    rfgen_test_reset_state();
    g_eventCount      = 0;
    g_outputIsLow     = true;
    g_platformStarts  = true;
    g_platformChanges = true;
    g_platformTable   = nullptr;
}

void assert_full_prefix(const uint32_t* table, uint16_t count, uint32_t pwmTop)
{
    TEST_ASSERT_GREATER_OR_EQUAL_UINT16(RFGEN_STARTUP_PERIOD_COUNT, count);
    for (uint16_t index = 0; index < RFGEN_STARTUP_PERIOD_COUNT; ++index)
    {
        TEST_ASSERT_EQUAL_UINT32(pwmTop, table[index]);
    }
}

void test_normalization()
{
    TEST_ASSERT_EQUAL_UINT8(0, rfgen_normalize_power_percent(0));
    TEST_ASSERT_EQUAL_UINT8(30, rfgen_normalize_power_percent(1));
    TEST_ASSERT_EQUAL_UINT8(30, rfgen_normalize_power_percent(29));
    TEST_ASSERT_EQUAL_UINT8(30, rfgen_normalize_power_percent(30));
    TEST_ASSERT_EQUAL_UINT8(31, rfgen_normalize_power_percent(31));
    TEST_ASSERT_EQUAL_UINT8(100, rfgen_normalize_power_percent(100));
    TEST_ASSERT_EQUAL_UINT8(100, rfgen_normalize_power_percent(101));
    TEST_ASSERT_EQUAL_UINT8(100, rfgen_normalize_power_percent(255));
}

void test_representative_samd21_tables()
{
    constexpr uint32_t kPeriodClocks    = 102;
    constexpr uint32_t kPwmTop          = kPeriodClocks - 1;
    constexpr uint32_t kMinimumBlankTop = (RFGEN_MINIMUM_BLANK_PERIOD_COUNT * kPeriodClocks) - 1;

    struct Expectation
    {
        uint8_t  power;
        uint16_t count;
        uint32_t finalTop;
    };

    const Expectation expectations[] = {
        {30,  13,  (20u * kPeriodClocks) - 1u},
        {31,  13,  (19u * kPeriodClocks) - 1u},
        {40,  13,  kMinimumBlankTop          },
        {41,  13,  kMinimumBlankTop          },
        {50,  18,  kMinimumBlankTop          },
        {75,  47,  kMinimumBlankTop          },
        {80,  61,  kMinimumBlankTop          },
        {94,  229, kMinimumBlankTop          },
        {95,  277, kMinimumBlankTop          },
        {100, 12,  kPwmTop                   },
    };

    uint32_t table[RFGEN_TABLE_CAPACITY];
    for (const Expectation& expectation : expectations)
    {
        uint16_t count = 0;
        TEST_ASSERT_TRUE(rfgen_generate_period_table(expectation.power,
                                                     kPeriodClocks,
                                                     0xFFFFFFu,
                                                     table,
                                                     RFGEN_TABLE_CAPACITY,
                                                     &count));
        TEST_ASSERT_EQUAL_UINT16(expectation.count, count);
        assert_full_prefix(table, count, kPwmTop);
        TEST_ASSERT_EQUAL_UINT32(expectation.finalTop, table[count - 1u]);
    }
}

void test_rp2040_entries_fit_counter()
{
    constexpr uint32_t kPeriodClocks = 283;
    uint32_t           table[RFGEN_TABLE_CAPACITY];
    uint16_t           count = 0;

    TEST_ASSERT_TRUE(rfgen_generate_period_table(30, kPeriodClocks, UINT16_MAX, table, RFGEN_TABLE_CAPACITY, &count));
    TEST_ASSERT_EQUAL_UINT16(13, count);
    TEST_ASSERT_EQUAL_UINT32((20u * kPeriodClocks) - 1u, table[count - 1u]);
    for (uint16_t index = 0; index < count; ++index)
    {
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(UINT16_MAX, table[index]);
    }
}

void test_blank_chunking_balances_legal_chunks()
{
    constexpr uint32_t kPeriodClocks = 102;
    constexpr uint32_t kMaximumTop   = (15u * kPeriodClocks) - 1u;
    uint32_t           table[4]      = {};
    uint16_t           count         = 0;

    TEST_ASSERT_TRUE(rfgen_test_append_blank(table, 4, &count, 40, kPeriodClocks, kMaximumTop));
    TEST_ASSERT_EQUAL_UINT16(3, count);
    TEST_ASSERT_EQUAL_UINT32((14u * kPeriodClocks) - 1u, table[0]);
    TEST_ASSERT_EQUAL_UINT32((13u * kPeriodClocks) - 1u, table[1]);
    TEST_ASSERT_EQUAL_UINT32((13u * kPeriodClocks) - 1u, table[2]);
}

void test_blank_buffer_exhaustion_uses_longest_available_chunks()
{
    constexpr uint32_t kPeriodClocks = 102;
    constexpr uint32_t kMaximumTop   = (15u * kPeriodClocks) - 1u;
    uint32_t           table[2]      = {};
    uint16_t           count         = 0;

    TEST_ASSERT_TRUE(rfgen_test_append_blank(table, 2, &count, 100, kPeriodClocks, kMaximumTop));
    TEST_ASSERT_EQUAL_UINT16(2, count);
    TEST_ASSERT_EQUAL_UINT32(kMaximumTop, table[0]);
    TEST_ASSERT_EQUAL_UINT32(kMaximumTop, table[1]);
}

void test_print_samd21_tables_in_five_percent_steps()
{
    constexpr uint32_t kPeriodClocks = 102;
    uint32_t           table[RFGEN_TABLE_CAPACITY];

    for (uint16_t requestedPower = 0; requestedPower <= RFGEN_MAXIMUM_POWER_PERCENT; requestedPower += 5)
    {
        const uint8_t normalizedPower = rfgen_normalize_power_percent(static_cast<uint8_t>(requestedPower));
        uint16_t      count           = 0;

        TEST_ASSERT_TRUE(rfgen_generate_period_table(normalizedPower,
                                                     kPeriodClocks,
                                                     0xFFFFFFu,
                                                     table,
                                                     RFGEN_TABLE_CAPACITY,
                                                     &count));

        printf("\nRFGEN TABLE requested=%u%%, normalized=%u%%, entries=%u\n",
               static_cast<unsigned>(requestedPower),
               static_cast<unsigned>(normalizedPower),
               static_cast<unsigned>(count));

        if (count == 0)
        {
            printf("  (off; empty table)\n");
        }

        for (uint16_t index = 0; index < count; ++index)
        {
            const uint32_t top            = table[index];
            const uint32_t carrierPeriods = (top + 1u) / kPeriodClocks;
            printf("  %u: TOP=%lu, periods=%lu\n",
                   static_cast<unsigned>(index),
                   static_cast<unsigned long>(top),
                   static_cast<unsigned long>(carrierPeriods));
        }
    }
}

void test_runtime_transition_order_and_repetition()
{
    reset_fixture();
    rfgen_set(50);

#ifdef RFGEN_MUTED_DEBUG
    TEST_ASSERT_EQUAL_UINT8(1, g_eventCount);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Event::ForceLow), static_cast<int>(g_events[0]));
#else
    TEST_ASSERT_EQUAL_UINT8(1, g_eventCount);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Event::Start), static_cast<int>(g_events[0]));
#endif
    TEST_ASSERT_EQUAL_UINT16(18, g_rfgenPeriodCount);

    g_eventCount = 0;
    rfgen_set(50);
    TEST_ASSERT_EQUAL_UINT8(0, g_eventCount);

    rfgen_set(75);
#ifdef RFGEN_MUTED_DEBUG
    TEST_ASSERT_EQUAL_UINT8(1, g_eventCount);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Event::ForceLow), static_cast<int>(g_events[0]));
#else
    TEST_ASSERT_EQUAL_UINT8(1, g_eventCount);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Event::Change), static_cast<int>(g_events[0]));
#endif
    TEST_ASSERT_EQUAL_UINT16(47, g_rfgenPeriodCount);

    g_eventCount = 0;
    rfgen_set(0);
    TEST_ASSERT_EQUAL_UINT16(0, g_rfgenPeriodCount);
#ifdef RFGEN_MUTED_DEBUG
    TEST_ASSERT_EQUAL_UINT8(1, g_eventCount);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Event::ForceLow), static_cast<int>(g_events[0]));
#else
    TEST_ASSERT_EQUAL_UINT8(1, g_eventCount);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Event::Stop), static_cast<int>(g_events[0]));
#endif
}

#ifndef RFGEN_MUTED_DEBUG
void test_platform_start_failure_returns_to_off()
{
    reset_fixture();
    g_platformStarts = false;
    rfgen_set(75);

    TEST_ASSERT_EQUAL_UINT8(2, g_eventCount);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Event::Start), static_cast<int>(g_events[0]));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Event::Stop), static_cast<int>(g_events[1]));
    TEST_ASSERT_EQUAL_UINT16(0, g_rfgenPeriodCount);
    TEST_ASSERT_TRUE(g_outputIsLow);
}

void test_platform_change_failure_keeps_previous_waveform()
{
    reset_fixture();
    rfgen_set(50);
    TEST_ASSERT_EQUAL_UINT16(18, g_rfgenPeriodCount);

    g_eventCount      = 0;
    g_platformChanges = false;
    rfgen_set(75);

    TEST_ASSERT_EQUAL_UINT8(1, g_eventCount);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Event::Change), static_cast<int>(g_events[0]));
    TEST_ASSERT_EQUAL_UINT16(18, g_rfgenPeriodCount);
    TEST_ASSERT_FALSE(g_outputIsLow);
}
#endif
} // namespace

bool rfgen_platform_start(const uint32_t* periodTable, uint16_t)
{
    record_event(Event::Start);
    TEST_ASSERT_TRUE(g_outputIsLow);
    g_outputIsLow = !g_platformStarts;
    if (g_platformStarts)
    {
        g_platformTable = periodTable;
    }
    return g_platformStarts;
}

bool rfgen_platform_change(const uint32_t* periodTable, uint16_t)
{
    record_event(Event::Change);
    TEST_ASSERT_FALSE(g_outputIsLow);
    TEST_ASSERT_NOT_EQUAL(g_platformTable, periodTable);
    if (g_platformChanges)
    {
        g_platformTable = periodTable;
    }
    return g_platformChanges;
}

void rfgen_platform_stop(void)
{
    record_event(Event::Stop);
    g_outputIsLow = true;
}

void rfgen_test_force_output_low(void)
{
    record_event(Event::ForceLow);
    g_outputIsLow = true;
}

void setUp(void) {}

void tearDown(void) {}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_normalization);
    RUN_TEST(test_representative_samd21_tables);
    RUN_TEST(test_rp2040_entries_fit_counter);
    RUN_TEST(test_blank_chunking_balances_legal_chunks);
    RUN_TEST(test_blank_buffer_exhaustion_uses_longest_available_chunks);
    RUN_TEST(test_print_samd21_tables_in_five_percent_steps);
    RUN_TEST(test_runtime_transition_order_and_repetition);
#ifndef RFGEN_MUTED_DEBUG
    RUN_TEST(test_platform_start_failure_returns_to_off);
    RUN_TEST(test_platform_change_failure_keeps_previous_waveform);
#endif
    return UNITY_END();
}
