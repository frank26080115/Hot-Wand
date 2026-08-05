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

typedef struct
{
    uint16_t minimum_millivolts_per_cell;
    uint16_t maximum_millivolts_per_cell;
    uint8_t minimum_cell_count;
    uint8_t maximum_cell_count;
} battery_cell_voltage_range_t;

/* Indexed by BATT_MODE_*.  BATT_MODE_NONE contains a zeroed range. */
extern const battery_cell_voltage_range_t battery_cell_voltage_ranges[
    BATT_MODE_LIFE_SAFE + 1U];

/*
 * Determines the range of whole-number cell counts consistent with the
 * supplied pack voltage and the selected BATT_MODE_* limits.
 *
 * The optimistic result uses the fewest plausible cells and therefore the
 * highest inferred charge per cell.  The pessimistic result uses the most
 * plausible cells and therefore the lowest inferred charge per cell.  Both
 * results are constrained by the selected chemistry's configured maximum;
 * no result is reported when even the optimistic count exceeds it.
 *
 * Returns false and clears *guess when the inputs describe no plausible
 * whole-number cell count.  A NULL result pointer also returns false.
 */
bool battery_guess(uint16_t battery_millivolts,
                   uint8_t battery_mode,
                   battery_guess_t *guess);

/*
 * Confirms the cell count and BATT_MODE_* for the current powered session.
 * The selected mode supplies both the per-cell cutoff and the supported cell
 * count floor.  A zero cell count disables checking; otherwise an invalid or
 * disabled mode returns false and preserves the previous values.
 */
bool battery_set_params(uint16_t cell_count, uint8_t battery_mode);

/*
 * Returns true when battery checking is disabled or the measured pack voltage
 * is at least cell_count * minimum_millivolts_per_cell.  pwrmgt_task() owns
 * calling this function and displaying the fault when it returns false.
 */
bool battery_check(void);

/* Returns whether this session may reduce the inferred pack by one cell. */
bool battery_can_override(void);

/*
 * Blocks on the low-battery warning.  When allow_override is true and the
 * session remains eligible, holding the button long enough reduces the
 * inferred pack by one cell and returns.  Otherwise this function does not
 * return.
 */
void battery_show_fault(bool allow_override);

#ifdef __cplusplus
}
#endif

