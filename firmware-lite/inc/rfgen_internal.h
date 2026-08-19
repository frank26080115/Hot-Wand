#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rfgen.h"

/* Active waveform length, exposed for diagnostics and native tests. */
extern uint16_t g_rfgenPeriodCount;

uint8_t rfgen_normalize_power_percent(uint8_t powerPercent);

bool rfgen_generate_period_table(uint8_t   normalizedPowerPercent,
                                 uint32_t  pwmPeriodClocks,
                                 uint32_t  maximumPwmTop,
                                 uint32_t* periodTable,
                                 uint16_t  tableCapacity,
                                 uint16_t* periodCount);

/* Implemented by exactly one target-specific RF generator source file. */
bool rfgen_platform_start(const uint32_t* periodTable, uint16_t periodCount);
bool rfgen_platform_change(const uint32_t* periodTable, uint16_t periodCount);
void rfgen_platform_stop(void);

#ifdef RFGEN_UNIT_TEST
void rfgen_test_force_output_low(void);
void rfgen_test_reset_state(void);
bool rfgen_test_append_blank(uint32_t* periodTable,
                             uint16_t  tableCapacity,
                             uint16_t* periodCount,
                             uint64_t  blankPeriodCount,
                             uint32_t  pwmPeriodClocks,
                             uint32_t  maximumPwmTop);
#endif
