#include <unity.h>
#include <Arduino.h>
#include <vector>

#include "hotwandlite.h"
#include "testing_cli.h"

TestSerial Serial;
namespace
{
uint32_t nowMs;
uint32_t voltageMv;
uint8_t mode;
bool switchOn;
bool voltageOverride;
int adcReading;
std::vector<int> adcSequence;
size_t adcIndex;
std::vector<uint8_t> powers;

void at(uint32_t time)
{
    nowMs = time;
    pwrmgt_task();
}

void expect_power(uint8_t expected)
{
    TEST_ASSERT_FALSE(powers.empty());
    TEST_ASSERT_EQUAL_UINT8(expected, powers.back());
}

void start()
{
    at(0);
    TEST_ASSERT_TRUE(powers.empty());
    at(499);
    TEST_ASSERT_TRUE(powers.empty());
    at(500);
}

void set_adc_voltage(uint32_t mv)
{
    adcReading = static_cast<int>((static_cast<uint64_t>(mv) * 4095 * 4700 + 172755000) / 345510000);
}

void test_threshold_and_non_sport_startup()
{
    for (uint32_t mv : {26999u, 27000u, 27001u})
    {
        pwrmgt_test_reset_state(); powers.clear(); voltageMv = mv;
        start();
        expect_power(mv > 27000 ? 40 : 100);
    }
    for (uint8_t selection : {0, 1})
    {
        pwrmgt_test_reset_state(); powers.clear(); voltageMv = 28000; mode = selection;
        start();
        expect_power(selection == 0 ? 70 : 79);
    }
}

void test_linear_ramp_and_no_duplicate_requests()
{
    start(); expect_power(40);
    at(600); at(666);
    TEST_ASSERT_EQUAL_UINT32(1, powers.size());
    at(667); expect_power(41);
    at(1500); expect_power(46);
    at(5500); expect_power(70);
    at(10499); expect_power(99);
    at(10500); expect_power(100);
    const size_t count = powers.size();
    at(20000); TEST_ASSERT_EQUAL_UINT32(count, powers.size());
}

void test_irregular_service_and_clock_rollover()
{
    const uint32_t begin = UINT32_MAX - 1000u;
    at(begin); expect_power(40);
    at(begin + 1234u); expect_power(47);
    at(begin + 6789u); expect_power(80);
    at(begin + 11000u); expect_power(100);
}

void test_voltage_changes_neither_cancel_nor_start_ramp()
{
    start();
    voltageMv = 20000; at(1500); expect_power(46);
    voltageMv = 29000; at(5500); expect_power(70);
    voltageMv = 15000; at(10500); expect_power(100);

    pwrmgt_test_reset_state(); powers.clear(); voltageMv = 27000;
    start(); expect_power(100);
    voltageMv = 28000; at(1000); at(4000); expect_power(100);
    TEST_ASSERT_EQUAL_UINT32(1, powers.size());
}

void test_off_on_and_boot_off_restart_from_start()
{
    switchOn = false; at(0); expect_power(0);
    switchOn = true; at(1000); expect_power(40);
    at(6000); expect_power(70);
    switchOn = false; at(6100); expect_power(0);
    switchOn = true; at(6200); expect_power(40);
    at(16200); expect_power(100);
    switchOn = false; at(16300); expect_power(0);
    switchOn = true; at(16400); expect_power(40);
}

void test_fresh_adc_at_enable_and_after_switch_off()
{
    voltageOverride = false;
    set_adc_voltage(20000); at(0);
    set_adc_voltage(28000); at(500); expect_power(40);
    switchOn = false; at(600); expect_power(0);
    set_adc_voltage(20000); switchOn = true; at(700); expect_power(100);
    switchOn = false; at(800); expect_power(0);
    set_adc_voltage(28000); switchOn = true; at(900); expect_power(40);
}

void test_overvoltage_preempts_ramp_and_recovery_restarts()
{
    start(); at(1500); expect_power(46);
    voltageMv = 31000; at(1600); expect_power(0);
    voltageMv = 29600; at(1700); expect_power(0);
    voltageMv = 28000; at(1800); expect_power(0);
    at(3800); expect_power(40);
    at(4800); expect_power(46);
}

void test_fresh_enable_sample_can_block_overvoltage()
{
    voltageOverride = false;
    set_adc_voltage(20000); at(0);
    set_adc_voltage(28000); const int safeReading = adcReading;
    set_adc_voltage(31000);
    adcSequence = {safeReading, adcReading};
    at(500);
    expect_power(0);
    TEST_ASSERT_EQUAL_UINT32(1, powers.size());
    TEST_ASSERT_EQUAL_UINT32(2, adcIndex);
}

void test_mode_change_waits_two_seconds_independent_of_voltage()
{
    start(); mode = 0; at(1500);
    at(3499); expect_power(57);
    voltageMv = 20000; at(3500); expect_power(70);
    mode = 2; at(3600); at(5599); expect_power(70);
    at(5600); expect_power(100);
    switchOn = false; at(5700); expect_power(0);
    voltageMv = 28000; switchOn = true; at(5800); expect_power(40);
}

void test_brief_mode_change_does_not_cancel()
{
    start(); mode = 0; at(1500);
    mode = 2; at(2000); expect_power(49);
    at(5500); expect_power(70);
    at(10500); expect_power(100);
}

void test_entering_sport_while_powered_skips_ramp()
{
    mode = 1; start(); expect_power(79);
    mode = 2; at(1000); at(3000); expect_power(100);
}

void test_normal_mode_cancels_using_current_voltage()
{
    start(); mode = 1; at(1500);
    voltageMv = 24000; at(3500); expect_power(91);
}
} // namespace

uint32_t millis() { return nowMs; }
int analogRead(uint8_t)
{
    if (adcIndex < adcSequence.size()) return adcSequence[adcIndex++];
    return adcReading;
}
int digitalRead(uint8_t pin)
{
    if (pin == POWER_SWITCH_PIN) return switchOn ? HIGH : LOW;
    if (pin == SEL2_PIN) return mode == 0 ? LOW : HIGH;
    if (pin == SEL3_PIN) return mode == 2 ? LOW : HIGH;
    return HIGH;
}
bool testing_get_simulated_mode(uint8_t*) { return false; }
bool testing_get_simulated_voltage_mv(uint32_t* result)
{
    if (!voltageOverride) return false;
    *result = voltageMv;
    return true;
}
void rfgen_set(uint8_t power) { powers.push_back(power); }
void blink_set_enabled(bool) {}
void blink_set_pattern(blink_voltage_t, blink_power_t) {}
void setUp()
{
    pwrmgt_test_reset_state();
    powers.clear(); nowMs = 0; voltageMv = 28000; mode = 2;
    switchOn = true; voltageOverride = true; adcReading = 0;
    adcSequence.clear(); adcIndex = 0;
}
void tearDown() {}
int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_threshold_and_non_sport_startup);
    RUN_TEST(test_linear_ramp_and_no_duplicate_requests);
    RUN_TEST(test_irregular_service_and_clock_rollover);
    RUN_TEST(test_voltage_changes_neither_cancel_nor_start_ramp);
    RUN_TEST(test_off_on_and_boot_off_restart_from_start);
    RUN_TEST(test_fresh_adc_at_enable_and_after_switch_off);
    RUN_TEST(test_overvoltage_preempts_ramp_and_recovery_restarts);
    RUN_TEST(test_fresh_enable_sample_can_block_overvoltage);
    RUN_TEST(test_mode_change_waits_two_seconds_independent_of_voltage);
    RUN_TEST(test_brief_mode_change_does_not_cancel);
    RUN_TEST(test_entering_sport_while_powered_skips_ramp);
    RUN_TEST(test_normal_mode_cancels_using_current_voltage);
    return UNITY_END();
}
