#include <unity.h>
#include <Arduino.h>
#include "rfgen_internal.h"

namespace
{
uint32_t nowUs;
unsigned starts, stops, changes;
bool outputLow;

void test_updates_preserve_on_phase()
{
    rfgen_set(80);
    TEST_ASSERT_EQUAL_UINT(1, starts);
    nowUs = 100000; rfgen_set(85);
    TEST_ASSERT_EQUAL_UINT(0, changes);
    nowUs = 169999; rfgen_task(); TEST_ASSERT_FALSE(outputLow);
    nowUs = 170000; rfgen_task(); TEST_ASSERT_TRUE(outputLow);
    TEST_ASSERT_EQUAL_UINT(1, stops);
}

void test_updates_preserve_off_phase()
{
    rfgen_set(80);
    nowUs = 160000; rfgen_task(); TEST_ASSERT_TRUE(outputLow);
    nowUs = 165000; rfgen_set(90);
    TEST_ASSERT_TRUE(outputLow);
    TEST_ASSERT_EQUAL_UINT(1, starts);
    TEST_ASSERT_EQUAL_UINT(0, changes);
    nowUs = 179999; rfgen_task(); TEST_ASSERT_TRUE(outputLow);
    nowUs = 180000; rfgen_task(); TEST_ASSERT_FALSE(outputLow);
    TEST_ASSERT_EQUAL_UINT(2, starts);
}

void test_macro_exit_to_continuous_while_off()
{
    rfgen_set(80);
    nowUs = 160000; rfgen_task(); TEST_ASSERT_TRUE(outputLow);
    rfgen_set(100); TEST_ASSERT_FALSE(outputLow);
    TEST_ASSERT_EQUAL_UINT(2, starts);
    nowUs = 500000; rfgen_task(); TEST_ASSERT_FALSE(outputLow);
    TEST_ASSERT_EQUAL_UINT(1, stops);
}

void test_macro_exit_to_short_bursts_and_shutdown()
{
    rfgen_set(90);
    nowUs = 180000; rfgen_task(); TEST_ASSERT_TRUE(outputLow);
    rfgen_set(70); TEST_ASSERT_FALSE(outputLow);
    TEST_ASSERT_EQUAL_UINT(2, starts);
    nowUs = 500000; rfgen_task(); TEST_ASSERT_EQUAL_UINT(1, stops);
    rfgen_set(0); TEST_ASSERT_TRUE(outputLow);
    nowUs = 1000000; rfgen_task(); TEST_ASSERT_EQUAL_UINT(2, starts);
}

void test_phase_timing_handles_rollover()
{
    nowUs = UINT32_MAX - 100000u;
    rfgen_set(80);
    nowUs += 160000u; rfgen_task(); TEST_ASSERT_TRUE(outputLow);
    nowUs += 40000u; rfgen_task(); TEST_ASSERT_FALSE(outputLow);
}
} // namespace

uint32_t micros() { return nowUs; }
bool rfgen_platform_start(const uint32_t*, uint16_t)
{
    TEST_ASSERT_TRUE(outputLow);
    ++starts; outputLow = false; return true;
}
bool rfgen_platform_change(const uint32_t*, uint16_t)
{
    TEST_ASSERT_FALSE(outputLow);
    ++changes; return true;
}
void rfgen_platform_stop() { ++stops; outputLow = true; }
void rfgen_test_force_output_low() { outputLow = true; }
void setUp()
{
    rfgen_test_reset_state(); nowUs = 0;
    starts = stops = changes = 0; outputLow = true;
}
void tearDown() {}
int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_updates_preserve_on_phase);
    RUN_TEST(test_updates_preserve_off_phase);
    RUN_TEST(test_macro_exit_to_continuous_while_off);
    RUN_TEST(test_macro_exit_to_short_bursts_and_shutdown);
    RUN_TEST(test_phase_timing_handles_rollover);
    return UNITY_END();
}
