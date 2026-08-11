#pragma once

#include <stdint.h>

/**
 * Request RF output power as a percentage. Decreases take effect immediately;
 * increases are applied gradually by rfgen_task().
 *
 * @param power_percent 0 is off, 100 is continuously on, and values
 * above 100 are treated as 100.
 * @param period_us Burst period at partial power. The default, and an explicit zero,
 * select 10000 us.
 */

#define kMaximumPowerPercent  100
#define kDefaultBurstPeriodUs 10000
#define kPowerRampDurationMs  1000
#define kRfFrequencyHz        470000

/*
 * Burst frequency = 1000000 / period_us. Approximate auditory reference:
 *
 *     Period      Frequency
 *     100000 us       10  Hz  distinct pulses
 *      50000 us       20  Hz  lower edge of human hearing
 *      20000 us       50  Hz
 *      10000 us      100  Hz
 *       5000 us      200  Hz
 *       2000 us      500  Hz
 *       1000 us        1 kHz
 *        500 us        2 kHz
 *        250 us        4 kHz
 *        100 us       10 kHz
 *         50 us       20 kHz  upper edge of human hearing
 */

#if defined(HOT_WAND_TARGET_XIAO_SAMD21) && !defined(HOT_WAND_TARGET_XIAO_RP2040)
/*
 * TC5 runs at 48 MHz / 1024. A 512 us burst is exactly 24 timer ticks
 * and causes at most 3907 short, dedicated TC5 interrupts per second.
 */
#define kMinimumDefaultBurstPeriodUs 512
#elif defined(HOT_WAND_TARGET_XIAO_RP2040) && !defined(HOT_WAND_TARGET_XIAO_SAMD21)
/*
 * The hardware alarm has 1 us ticks but uses the longer SDK alarm-pool ISR.
 * A 250 us burst limits steady-state callbacks to 8000 per second and leaves
 * ample ISR timing margin in the 83/167 us phases used at 33/66 percent power.
 */
#define kMinimumDefaultBurstPeriodUs 250
#else
#error "Select exactly one supported microcontroller"
#endif

#if kDefaultBurstPeriodUs < kMinimumDefaultBurstPeriodUs
#error "kDefaultBurstPeriodUs is too short for this target's burst scheduler"
#endif

#ifdef __cplusplus
extern "C"
{
void rfgen_set(uint8_t power_percent, uint32_t period_us = kDefaultBurstPeriodUs);
void rfgen_task(void);
}
#else
void rfgen_set(uint8_t power_percent, uint32_t period_us);
void rfgen_task(void);
#endif
