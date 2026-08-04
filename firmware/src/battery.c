#include "battery.h"
#include "adc.h"
#include "button.h"
#include "fault.h"
#include "miscutils.h"
#include "oled.h"
#include "pins.h"
#include "pwrlvl.h"
#include "rfgen.h"
#include "stm32f0xx_hal.h"
#include "systick.h"

#include <stddef.h>

#define BATTERY_FAULT_BAR_WIDTH          32U
#define BATTERY_FAULT_BAR_HEIGHT          3U
#define BATTERY_FAULT_BAR_Y              85U
#define BATTERY_FAULT_FIRST_BASELINE      9U
#define BATTERY_FAULT_LINE_HEIGHT        10U
#define BATTERY_FAULT_REFRESH_MS          67UL
#define BATTERY_FAULT_STEP_MS           156UL
#define BATTERY_FAULT_VOLTAGE_CAPACITY    8U

static uint16_t battery_cell_count;
static uint16_t battery_minimum_millivolts_per_cell;

static void battery_render_fault(u8g2_t *graphics, uint8_t progress);

/* A single public table has no lookup-code overhead and keeps paired limits
 * adjacent.  Validate these generic limits against the actual cells. */
const battery_cell_voltage_range_t battery_cell_voltage_ranges[
    BATT_MODE_LIFE_SAFE + 1U] = {
    [BATT_MODE_NONE]      = {0U,    0U},
    [BATT_MODE_LIPO]      = {3100U, 4200U},
    [BATT_MODE_LIPO_SAFE] = {3300U, 4200U},
    [BATT_MODE_LIHV]      = {3100U, 4350U},
    [BATT_MODE_LIHV_SAFE] = {3300U, 4350U},
    [BATT_MODE_LIFE]      = {2500U, 3650U},
    [BATT_MODE_LIFE_SAFE] = {3000U, 3650U},
};

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

void battery_show_fault(bool allow_override)
{
    u8g2_t *graphics;
    uint32_t last_refresh_ms;
    uint32_t last_step_ms;
    uint8_t progress = 0U;
    bool override_ready = false;

    rfgen_stop();
    pwrlvl_force_minimum();
    if (!allow_override) {
        show_fault("LOW\nBATT\n!!!!!", false);
        return;
    }
    btn_init();

    graphics = OLED_GetGraphics(&oled);
    if (graphics != NULL) {
        u8g2_SetDisplayRotation(graphics, U8G2_R1);
        u8g2_SetFont(graphics, u8g2_font_6x10_tr);
        battery_render_fault(graphics, progress);
    }

    last_step_ms = systick_get_ms();
    last_refresh_ms = last_step_ms;

    for (;;) {
        uint32_t elapsed;
        uint32_t now;
        uint32_t steps;

        btn_task();
        now = systick_get_ms();

        /* Once completed, keep the full bar latched until its next frame. */
        if (!override_ready) {
            if (!btn_is_down()) {
                if (progress != 0U) {
                    progress = 0U;
                }
                last_step_ms = now;
            } else {
                elapsed = (uint32_t)(now - last_step_ms);
                steps = elapsed / BATTERY_FAULT_STEP_MS;

                if (steps > 0U) {
                    last_step_ms += steps * BATTERY_FAULT_STEP_MS;
                    if (steps >= (uint32_t)(BATTERY_FAULT_BAR_WIDTH -
                                            progress)) {
                        progress = BATTERY_FAULT_BAR_WIDTH;
                    } else {
                        progress = (uint8_t)(progress + steps);
                    }
                    override_ready =
                        progress >= BATTERY_FAULT_BAR_WIDTH;
                }
            }
        }

        /* 67 ms is the nearest whole-millisecond period that does not exceed
         * 15 frames per second. */
        if ((uint32_t)(now - last_refresh_ms) >=
            BATTERY_FAULT_REFRESH_MS) {
            if (graphics != NULL) {
                battery_render_fault(graphics, progress);
            }
            last_refresh_ms = now;

            if (override_ready) {
                (void)battery_set_params(0U, 0U);
                rfgen_start();
                pwrlvl_release_minimum();
                return;
            }
        }

        HAL_Delay(1U);
    }
}

static void battery_render_fault(u8g2_t *graphics, uint8_t progress)
{
    char voltage[BATTERY_FAULT_VOLTAGE_CAPACITY];
    char *end;
    uint8_t baseline = BATTERY_FAULT_FIRST_BASELINE;

    millivolts_to_str(adc_to_millivolts(DC_SENS_IDX), voltage, 1U);
    end = voltage;
    while (*end != '\0') {
        ++end;
    }
    *end++ = 'V';
    *end = '\0';

    u8g2_ClearBuffer(graphics);
    u8g2_DrawStr(graphics, 1U, baseline, voltage);
    baseline = (uint8_t)(baseline + BATTERY_FAULT_LINE_HEIGHT);
    u8g2_DrawStr(graphics, 1U, baseline, "LOW");
    baseline = (uint8_t)(baseline + BATTERY_FAULT_LINE_HEIGHT);
    u8g2_DrawStr(graphics, 1U, baseline, "BATT");
    baseline = (uint8_t)(baseline + BATTERY_FAULT_LINE_HEIGHT);
    u8g2_DrawStr(graphics, 1U, baseline, "!!!!!");
    baseline = (uint8_t)(baseline + BATTERY_FAULT_LINE_HEIGHT);
    u8g2_DrawStr(graphics, 1U, baseline, "HOLD");
    baseline = (uint8_t)(baseline + BATTERY_FAULT_LINE_HEIGHT);
    u8g2_DrawStr(graphics, 1U, baseline, "TO");
    baseline = (uint8_t)(baseline + BATTERY_FAULT_LINE_HEIGHT);
    u8g2_DrawStr(graphics, 1U, baseline, "OVER-");
    baseline = (uint8_t)(baseline + BATTERY_FAULT_LINE_HEIGHT);
    u8g2_DrawStr(graphics, 1U, baseline, "RIDE");
    u8g2_DrawBox(graphics,
                 0U,
                 BATTERY_FAULT_BAR_Y,
                 progress,
                 BATTERY_FAULT_BAR_HEIGHT);
    (void)OLED_SendBuffer(&oled);
}
