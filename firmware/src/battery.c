#include "battery.h"
#include "adc.h"
#include "pins.h"

#include <stddef.h>

static uint16_t battery_cell_count;
static uint16_t battery_minimum_millivolts_per_cell;

bool battery_guess(uint16_t battery_millivolts,
                   uint16_t highest_millivolts_per_cell,
                   uint16_t lowest_millivolts_per_cell,
                   battery_guess_t *guess)
{
    uint32_t optimistic_cell_count;
    uint32_t pessimistic_cell_count;

    if (guess == NULL) {
        return false;
    }

    *guess = (battery_guess_t){0};

    if ((battery_millivolts == 0U) ||
        (lowest_millivolts_per_cell == 0U) ||
        (highest_millivolts_per_cell == 0U) ||
        (lowest_millivolts_per_cell > highest_millivolts_per_cell)) {
        return false;
    }

    /* Ceiling division finds the fewest cells that do not exceed the
     * configured maximum per-cell voltage. */
    optimistic_cell_count =
        ((uint32_t)battery_millivolts + highest_millivolts_per_cell - 1U) /
        highest_millivolts_per_cell;

    /* Floor division finds the most cells that remain at or above the
     * configured minimum per-cell voltage. */
    pessimistic_cell_count =
        (uint32_t)battery_millivolts / lowest_millivolts_per_cell;

    if ((optimistic_cell_count == 0U) ||
        (optimistic_cell_count > pessimistic_cell_count) ||
        (pessimistic_cell_count > UINT16_MAX)) {
        return false;
    }

    guess->optimistic_cell_count = (uint16_t)optimistic_cell_count;
    guess->pessimistic_cell_count = (uint16_t)pessimistic_cell_count;
    guess->optimistic_millivolts_per_cell =
        (uint16_t)((uint32_t)battery_millivolts / optimistic_cell_count);
    guess->pessimistic_millivolts_per_cell =
        (uint16_t)((uint32_t)battery_millivolts / pessimistic_cell_count);

    return true;
}

bool battery_set_params(uint16_t cell_count,
                        uint16_t minimum_millivolts_per_cell)
{
    if (cell_count == 0U) {
        battery_cell_count = 0U;
        battery_minimum_millivolts_per_cell = 0U;
        return true;
    }

    if (minimum_millivolts_per_cell == 0U) {
        return false;
    }

    battery_cell_count = cell_count;
    battery_minimum_millivolts_per_cell = minimum_millivolts_per_cell;

    return true;
}

bool battery_check(void)
{
    uint32_t minimum_battery_millivolts;

    if (battery_cell_count == 0U) {
        return true;
    }

    minimum_battery_millivolts =
        (uint32_t)battery_cell_count *
        battery_minimum_millivolts_per_cell;

    return (uint32_t)adc_to_millivolts(DC_SENS_IDX) >=
           minimum_battery_millivolts;
}
