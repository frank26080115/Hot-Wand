#include "adc.h"
#include "battery.h"
#include "button.h"
#include "fan.h"
#include "nvm.h"
#include "oled.h"
#include "pwrmgt.h"
#include "pwrlvl.h"
#include "rfgen.h"
#include "setup_menu.h"
#include "stm32f0xx_hal.h"
#include "systick.h"
#include "tipdetect.h"
#include "uart_debug.h"

#include <stddef.h>
#include <stdint.h>

#if !defined(HOT_WAND_TARGET_STM32F030) && \
    !defined(HOT_WAND_TARGET_STM32F042)
#error "This source supports only the STM32F030 and STM32F042 targets."
#endif

#define BOOT_POWER_WAIT_MS       300UL
#define SETUP_HOLD_DURATION_MS  5000UL
#define SETUP_HOLD_BAR_WIDTH      32U
#define SETUP_HOLD_BAR_HEIGHT      3U
#define SETUP_HOLD_BAR_Y          45U

static void boot_check_for_setup(void);
static void boot_draw_setup_hold(u8g2_t *graphics, uint8_t bar_width);
static void Error_Handler(void);

int main(void)
{
    battery_guess_t battery_guess_result;
    hotwand_setup_nvm_t settings = {0};
    const battery_cell_voltage_range_t *cell_range;
    uint32_t random_seed;
    uint16_t input_millivolts;
    bool boot_button_down;

    /* HAL provides the temporary reset-clock tick needed by the RCC startup
     * timeout.  Application initialization begins with HSE immediately
     * afterward. */
    HAL_Init();

    (void)rfgen_clock_init();
    systick_init();
    adc_init();
    btn_init();

    I2C1_Init();
    if (!OLED_Init(&oled, &i2c1)) {
        Error_Handler();
    }

    nvm_init();

    do {
        random_seed = adc_get_rand_seed();
    } while (random_seed == 0U);
    hotwand_srand(random_seed);

    boot_button_down = btn_is_down();
    UART_SetAllowed(boot_button_down);
    if (boot_button_down) {
        boot_check_for_setup();
    }

    while ((systick_get_ms() < BOOT_POWER_WAIT_MS) &&
           (adc_to_millivolts(DC_SENS_IDX) < BOOT_DC_READY_MV)) {
    }

    (void)nvm_read(&settings);
    input_millivolts = adc_to_millivolts(DC_SENS_IDX);

    if ((settings.batt_mode != BATT_MODE_NONE) &&
        (settings.batt_mode <= BATT_MODE_LIFE_SAFE)) {
        cell_range = &battery_cell_voltage_ranges[settings.batt_mode];
        if (battery_guess(input_millivolts,
                          cell_range->maximum_millivolts_per_cell,
                          cell_range->minimum_millivolts_per_cell,
                          &battery_guess_result)) {
            (void)battery_set_params(
                battery_guess_result.optimistic_cell_count,
                cell_range->minimum_millivolts_per_cell);
        }
    } else {
        (void)battery_set_params(0U, 0U);
    }

    /* perform all other initialization here */
    tipdetect_init();
    pwrlvl_init();
    pwrmgt_set_desired_power_level(
        (pwrlvl_mode_t)settings.startup_power_level);
    fan_init();

    for (;;) {
        pwrmgt_task();
        /* TODO: implement the remaining main-loop tasks. */
    }
}

static void boot_check_for_setup(void)
{
    u8g2_t *graphics = OLED_GetGraphics(&oled);
    uint32_t hold_start_ms;
    uint32_t release_start_ms = 0U;
    uint8_t drawn_width = 0U;
    bool release_pending = false;

    if (graphics == NULL) {
        Error_Handler();
    }

    u8g2_SetDisplayRotation(graphics, U8G2_R1);
    u8g2_SetFont(graphics, u8g2_font_6x10_tr);
    boot_draw_setup_hold(graphics, 0U);
    hold_start_ms = systick_get_ms();

    for (;;) {
        uint32_t elapsed;
        uint32_t now;
        uint8_t width;

        btn_task();
        now = systick_get_ms();

        if (!btn_is_down()) {
            if (!release_pending) {
                release_start_ms = now;
                release_pending = true;
            } else if ((uint32_t)(now - release_start_ms) >=
                       BTN_DEBOUNCE_MS) {
                u8g2_ClearBuffer(graphics);
                (void)OLED_SendBuffer(&oled);
                (void)btn_has_short_press(true);
                (void)btn_has_long_press(true);
                return;
            }

            HAL_Delay(1U);
            continue;
        }

        release_pending = false;
        elapsed = (uint32_t)(now - hold_start_ms);
        if (elapsed >= SETUP_HOLD_DURATION_MS) {
            width = SETUP_HOLD_BAR_WIDTH;
        } else {
            width = (uint8_t)((elapsed * SETUP_HOLD_BAR_WIDTH) /
                              SETUP_HOLD_DURATION_MS);
        }

        if (width != drawn_width) {
            boot_draw_setup_hold(graphics, width);
            drawn_width = width;
        }

        if (width >= SETUP_HOLD_BAR_WIDTH) {
            setup_menu();
            return;
        }

        HAL_Delay(1U);
    }
}

static void boot_draw_setup_hold(u8g2_t *graphics, uint8_t bar_width)
{
    u8g2_ClearBuffer(graphics);
    u8g2_DrawStr(graphics, 1U,  9U, "HOLD");
    u8g2_DrawStr(graphics, 1U, 19U, "TO");
    u8g2_DrawStr(graphics, 1U, 29U, "ENTER");
    u8g2_DrawStr(graphics, 1U, 39U, "SETUP");
    u8g2_DrawBox(graphics,
                 0U,
                 SETUP_HOLD_BAR_Y,
                 bar_width,
                 SETUP_HOLD_BAR_HEIGHT);
    (void)OLED_SendBuffer(&oled);
}

static void Error_Handler(void)
{
    __disable_irq();

    for (;;) {
    }
}
