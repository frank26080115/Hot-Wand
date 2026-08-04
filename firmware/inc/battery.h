#pragma once

#include "hotwand.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t optimistic_cell_count;
    uint16_t pessimistic_cell_count;
    uint16_t optimistic_millivolts_per_cell;
    uint16_t pessimistic_millivolts_per_cell;
} battery_guess_t;

/*
 * Determines the range of whole-number cell counts consistent with the
 * supplied pack voltage and permissible per-cell voltage range.
 *
 * The optimistic result uses the fewest plausible cells and therefore the
 * highest inferred charge per cell.  The pessimistic result uses the most
 * plausible cells and therefore the lowest inferred charge per cell.
 *
 * Returns false and clears *guess when the inputs describe no plausible
 * whole-number cell count.  A NULL result pointer also returns false.
 */
bool battery_guess(uint16_t battery_millivolts,
                   uint16_t highest_millivolts_per_cell,
                   uint16_t lowest_millivolts_per_cell,
                   battery_guess_t *guess);

/*
 * Confirms the cell count and minimum permitted per-cell voltage for the
 * current powered session.  A zero cell count disables battery checking and
 * ignores the minimum-voltage argument.  Otherwise, a zero minimum voltage
 * returns false and preserves the previous values.
 */
bool battery_set_params(uint16_t cell_count,
                        uint16_t minimum_millivolts_per_cell);

/*
 * Returns true when battery checking is disabled or the measured pack voltage
 * is at least cell_count * minimum_millivolts_per_cell.
 */
bool battery_check(void);

#ifdef __cplusplus
}
#endif

