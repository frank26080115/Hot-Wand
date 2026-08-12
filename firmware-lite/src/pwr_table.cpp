#include "pwr_table.h"
#include "rfgen.h"

/*
Power uses period-density control: each level selects a repeating table of PWM periods. Every period contains one pulse with the same approximately 1.06 us high time, while longer periods reduce average delivered power. DMA advances and loops the selected table without CPU timing. Eco, Normal, and Sport select table rows 1, 3, and 6; the other public levels are available for experiments. An indicator LED shows the system input voltage and selected output power level.
*/

#if defined(HOT_WAND_TARGET_XIAO_SAMD21) && !defined(HOT_WAND_TARGET_XIAO_RP2040)
static_assert(F_CPU == 48000000, "SAMD21 power table requires a 48 MHz clock");
#elif defined(HOT_WAND_TARGET_XIAO_RP2040) && !defined(HOT_WAND_TARGET_XIAO_SAMD21)
static_assert(F_CPU == 133000000, "RP2040 power table requires a 133 MHz clock");
#else
#error "Select exactly one supported microcontroller"
#endif

/* The nearest whole-clock period to one 470 kHz carrier cycle. */
#define PWM_PERIOD_CLOCKS ((F_CPU + (kRfFrequencyHz / 2)) / kRfFrequencyHz)

/* TOP is inclusive, so an N-times-long period contains N*clocks minus one. */
#define PER_1 ((PWM_PERIOD_CLOCKS * 1u) - 1u)
#define PER_2 ((PWM_PERIOD_CLOCKS * 2u) - 1u)
#define PER_3 ((PWM_PERIOD_CLOCKS * 3u) - 1u)

static_assert(PWR_TABLE_ROWS_CNT == 6, "The public RF levels require six table rows");
static_assert(PWR_TABLE_ROW_LENGTH == 16, "DMA setup requires sixteen periods per row");
static_assert(PER_1 <= UINT16_MAX, "Base PWM period does not fit the supported counters");
static_assert(PER_3 <= UINT16_MAX, "Longest PWM period does not fit the supported counters");

/*
 * Pattern frequencies below use the nominal 470 kHz base period. A motif's
 * frequency is 470 kHz divided by the sum of its period multipliers, not by
 * its number of entries. Actual values are about 0.13% higher on SAMD21 and
 * 0.007% lower on RP2040 because their integer-clock periods differ slightly.
 */
const uint32_t g_powerPeriodTable[PWR_TABLE_ROWS_CNT][PWR_TABLE_ROW_LENGTH] = {
    { // Level 1: [3,3,3,1] spans 10 base periods = 47.0 kHz
      // Average pulse rate: 188 kHz
      PER_3,
      PER_3,
      PER_3,
      PER_1,
      PER_3,
      PER_3,
      PER_3,
      PER_1,
      PER_3,
      PER_3,
      PER_3,
      PER_1,
      PER_3,
      PER_3,
      PER_3,
      PER_1 },
    { // Level 2, I am hoping, on paper, this results in roughly 50% power
      // [2,2,2,1] spans 7 base periods = 67.1 kHz
      // Average pulse rate: 268.6 kHz
      PER_2,
      PER_2,
      PER_2,
      PER_1,
      PER_2,
      PER_2,
      PER_2,
      PER_1,
      PER_2,
      PER_2,
      PER_2,
      PER_1,
      PER_2,
      PER_2,
      PER_2,
      PER_1 },
    { // Level 3: [2,1] spans 3 base periods = 156.7 kHz
      // Average pulse rate: 313.3 kHz
      PER_2,
      PER_1,
      PER_2,
      PER_1,
      PER_2,
      PER_1,
      PER_2,
      PER_1,
      PER_2,
      PER_1,
      PER_2,
      PER_1,
      PER_2,
      PER_1,
      PER_2,
      PER_1 },
    { // Level 4: full 21-base-period row = 22.4 kHz
      // Strong local [2,1,1] cadence = 117.5 kHz; average pulse rate = 358.1 kHz
      PER_2,
      PER_1,
      PER_1,
      PER_2,
      PER_1,
      PER_1,
      PER_2,
      PER_1,
      PER_1,
      PER_2,
      PER_1,
      PER_1,
      PER_2,
      PER_1,
      PER_1,
      PER_1 },
    { // Level 5: [2,1,1,1] spans 5 base periods = 94.0 kHz
      // Average pulse rate: 376 kHz
      PER_2,
      PER_1,
      PER_1,
      PER_1,
      PER_2,
      PER_1,
      PER_1,
      PER_1,
      PER_2,
      PER_1,
      PER_1,
      PER_1,
      PER_2,
      PER_1,
      PER_1,
      PER_1 },
    { // Level 6: continuous 470 kHz carrier, maximum power; no sub-pattern
      PER_1,
      PER_1,
      PER_1,
      PER_1,
      PER_1,
      PER_1,
      PER_1,
      PER_1,
      PER_1,
      PER_1,
      PER_1,
      PER_1,
      PER_1,
      PER_1,
      PER_1,
      PER_1 },
};

#undef PER_3
#undef PER_2
#undef PER_1
#undef PWM_PERIOD_CLOCKS
