/*
This code module is responsible for guessing the cell count when using a battery pack.
From the cell count, it is able to issue low battery warnings.
The warning will pause the operation of the soldering iron, and the user can override the warning to continue usage
*/

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "battery.h"
#include "pins.h"
#include "adc.h"
#include "button.h"
#include "fault.h"
#include "oled.h"
#include "fan.h"
#include "pwrlvl.h"
#include "rfgen.h"
#include "stm32f0xx_hal.h"
#include "systick.h"
#include "watchdog.h"

#include <stddef.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define BATTERY_FAULT_BAR_WIDTH  32
#define BATTERY_FAULT_BAR_HEIGHT 3
#define BATTERY_FAULT_BAR_Y      85
#define BATTERY_FAULT_REFRESH_MS 67
#define BATTERY_FAULT_STEP_MS    156

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static uint16_t battery_cell_count;
static uint8_t  battery_mode;
static bool     battery_override_used;

static const char battery_override_message[] = "LOW\nBATT\n!!!!!\nHOLD\nTO\nOVER-\nRIDE";

/* One table keeps the voltage and supported cell-count limits for each
 * BATT_MODE_* adjacent with no separate lookup code. */
const battery_cell_voltage_range_t battery_cell_voltage_ranges[BATT_MODE_LIFE_SAFE + 1] = {
    [BATT_MODE_NONE] = {0, 0, 0, 0},
    [BATT_MODE_LIPO] =
        {
                        3100, 4200,
                        BATTERY_MINIMUM_CELL_CNT_LIPO, BATTERY_MAXIMUM_CELL_CNT_LIPO,
                        },
    [BATT_MODE_LIPO_SAFE] =
        {
                        3300, 4200,
                        BATTERY_MINIMUM_CELL_CNT_LIPO, BATTERY_MAXIMUM_CELL_CNT_LIPO,
                        },
    [BATT_MODE_LIHV] =
        {
                        3100, 4350,
                        BATTERY_MINIMUM_CELL_CNT_LIHV, BATTERY_MAXIMUM_CELL_CNT_LIHV,
                        },
    [BATT_MODE_LIHV_SAFE] =
        {
                        3300, 4350,
                        BATTERY_MINIMUM_CELL_CNT_LIHV, BATTERY_MAXIMUM_CELL_CNT_LIHV,
                        },
    [BATT_MODE_LIFE] =
        {
                        2500, 3650,
                        BATTERY_MINIMUM_CELL_CNT_LIFE, BATTERY_MAXIMUM_CELL_CNT_LIFE,
                        },
    [BATT_MODE_LIFE_SAFE] =
        {
                        2750, 3650,
                        BATTERY_MINIMUM_CELL_CNT_LIFE, BATTERY_MAXIMUM_CELL_CNT_LIFE,
                        },
};

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void battery_show_fault_fatal(void);
static void battery_render_fault(u8g2_t* graphics, uint8_t progress);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

/*
Guess the number of cells according to total voltage and user selected battery chemistry
This guess will be used later to implement a low battery cutoff
*/
bool battery_guess(uint16_t battery_millivolts, uint8_t selected_battery_mode, battery_guess_t* guess)
{
    const battery_cell_voltage_range_t* limits;
    uint32_t                            optimistic_cell_count;
    uint32_t                            pessimistic_cell_count;

    if (guess == NULL)
    {
        return false;
    }

    *guess = (battery_guess_t){0};

    if ((battery_millivolts == 0) || (selected_battery_mode == BATT_MODE_NONE) ||
        (selected_battery_mode > BATT_MODE_LIFE_SAFE))
    {
        return false;
    }
    limits = &battery_cell_voltage_ranges[selected_battery_mode];

    /* Ceiling division finds the fewest cells that do not exceed the
     * configured maximum per-cell voltage. */
    optimistic_cell_count =
        ((uint32_t)battery_millivolts + limits->maximum_millivolts_per_cell - 1) / limits->maximum_millivolts_per_cell;

    /* Floor division finds the most cells that remain at or above the
     * configured minimum per-cell voltage. */
    pessimistic_cell_count = (uint32_t)battery_millivolts / limits->minimum_millivolts_per_cell;

    if ((optimistic_cell_count > limits->maximum_cell_count) || (optimistic_cell_count > pessimistic_cell_count))
    {
        /* No supported cell count puts the measured voltage inside the
         * chemistry's per-cell range.  Fail toward a low-battery warning:
         * choose the first cell count whose minimum pack voltage is above the
         * measurement.  battery_check() must reject this count, while reducing
         * it by one produces the greatest count that passes the same check. */
        pessimistic_cell_count++;
        optimistic_cell_count = pessimistic_cell_count;
    }
    else if (pessimistic_cell_count > limits->maximum_cell_count)
    {
        pessimistic_cell_count = limits->maximum_cell_count;
    }

    guess->optimistic_cell_count           = (uint16_t)optimistic_cell_count;
    guess->pessimistic_cell_count          = (uint16_t)pessimistic_cell_count;
    guess->optimistic_millivolts_per_cell  = (uint16_t)((uint32_t)battery_millivolts / optimistic_cell_count);
    guess->pessimistic_millivolts_per_cell = (uint16_t)((uint32_t)battery_millivolts / pessimistic_cell_count);

    return true;
}

bool battery_check(void)
{
    uint32_t minimum_battery_millivolts;

    /* Power management is the single owner of this periodic check and of
     * presenting battery_show_fault() when it fails.  Do not duplicate the
     * check in main or another task. */

    if (battery_cell_count == 0)
    {
        return true;
    }

    minimum_battery_millivolts =
        (uint32_t)battery_cell_count * battery_cell_voltage_ranges[battery_mode].minimum_millivolts_per_cell;

    return (uint32_t)adc_to_millivolts(DC_SENS_IDX) >= minimum_battery_millivolts;
}

void battery_show_fault(bool allow_override)
{
    u8g2_t*  graphics;
    uint32_t last_refresh_ms;
    uint32_t last_step_ms;
    uint8_t  progress       = 0;
    bool     override_ready = false;

    rfgen_stop();
    pwrlvl_force_minimum();
    fan_stop();

    if (!allow_override || !battery_can_override())
    {
        battery_show_fault_fatal();
        return;
    }

    btn_init();

    graphics = OLED_GetGraphics(&oled);
    if (graphics != NULL)
    {
        battery_render_fault(graphics, progress);
    }

    last_step_ms    = systick_get_ms();
    last_refresh_ms = last_step_ms;

    for (;;)
    {
        uint32_t elapsed;
        uint32_t now;
        uint32_t steps;

        btn_task();
        now = systick_get_ms();

        /* Once completed, keep the full bar latched until its next frame. */
        if (!override_ready)
        {
            if (!btn_is_down())
            {
                if (progress != 0)
                {
                    progress = 0;
                }
                last_step_ms = now;
            }
            else
            {
                elapsed = (uint32_t)(now - last_step_ms);
                steps   = elapsed / BATTERY_FAULT_STEP_MS;

                if (steps > 0)
                {
                    last_step_ms += steps * BATTERY_FAULT_STEP_MS;
                    if (steps >= (uint32_t)(BATTERY_FAULT_BAR_WIDTH - progress))
                    {
                        progress = BATTERY_FAULT_BAR_WIDTH;
                    }
                    else
                    {
                        progress = (uint8_t)(progress + steps);
                    }
                    override_ready = progress >= BATTERY_FAULT_BAR_WIDTH;
                }
            }
        }

        /* 67 ms is the nearest whole-millisecond period that does not exceed
         * 15 frames per second. */
        if ((uint32_t)(now - last_refresh_ms) >= BATTERY_FAULT_REFRESH_MS)
        {
            if (graphics != NULL)
            {
                battery_render_fault(graphics, progress);
            }
            last_refresh_ms = now;

            if (override_ready)
            {
                if (!battery_set_params((uint16_t)(battery_cell_count - 1), battery_mode))
                {
                    // revoke permission, but this should never happen
                    battery_show_fault_fatal();
                    return;
                }

                battery_override_used = true;
                fan_resume();
                rfgen_start();
                pwrlvl_release_minimum();
                return;
            }

            // revoke override ability if the voltage has continued to drop
            if (!battery_can_override())
            {
                battery_show_fault_fatal();
                return;
            }
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

// -----------------------------------------------------------------------------
// Getters and Setters
// -----------------------------------------------------------------------------

bool battery_set_params(uint16_t cell_count, uint8_t selected_battery_mode)
{
    if (cell_count == 0)
    {
        // enforced policy: if the cell count is zero, the battery mode must also be NONE
        battery_cell_count = 0;
        battery_mode       = BATT_MODE_NONE;
        return true;
    }

    if ((selected_battery_mode == BATT_MODE_NONE) || (selected_battery_mode > BATT_MODE_LIFE_SAFE))
    {
        // this is an error, because cell count cannot be non-zero while battery mode is NONE or invalid
        return false;
    }

    battery_cell_count = cell_count;
    battery_mode       = selected_battery_mode;

    return true;
}

bool battery_can_override(void)
{
    // the override capability is mostly to overcome a bad guess of cell count
    // when used, it sets the cell count to one less than the guessed count
    // so we only allow override to be used once
    // and also we only allow override if the cell count is still above the minimum supported cell count
    if (battery_override_used || (battery_cell_count <= battery_cell_voltage_ranges[battery_mode].minimum_cell_count))
    {
        return false;
    }

    // must also respect a minimum absolute voltage
    return adc_to_millivolts(DC_SENS_IDX) >= BATTERY_OVERRIDE_MINIMUM_MV;
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static void battery_show_fault_fatal(void)
{
    show_fault("LOW\nBATT\n!!!!!", false);
}

static void battery_render_fault(u8g2_t* graphics, uint8_t progress)
{
    fault_render(graphics, battery_override_message, 1, 1);
    u8g2_DrawBox(graphics, 0, BATTERY_FAULT_BAR_Y, progress, BATTERY_FAULT_BAR_HEIGHT);
    OLED_SendBuffer(&oled);
}
