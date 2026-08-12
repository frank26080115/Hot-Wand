#pragma once

#include <stdint.h>

#define PWR_TABLE_ROW_LENGTH 16
#define PWR_TABLE_ROWS_CNT   6

/* Each entry is the PWM TOP value for one fixed-width pulse period. */
extern const uint32_t g_powerPeriodTable[PWR_TABLE_ROWS_CNT][PWR_TABLE_ROW_LENGTH];
