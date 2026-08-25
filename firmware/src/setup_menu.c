/*
 * This module implements the button-driven OLED setup menu. The user can cycle
 * through settings, adjust values, and save or discard changes before exiting.
 * The menu acts as its own application and reboots into the normal application
 * when it exits.
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "setup_menu.h"

#include "adc.h"
#include "button.h"
#include "fan.h"
#include "miscutils.h"
#include "nvm.h"
#include "oled.h"
#include "pins.h"
#include "pwrlvl.h"
#include "rfgen.h"
#include "stm32f0xx_hal.h"
#include "systick.h"
#include "typedefs.h"
#include "watchdog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define SETUP_MENU_CHARS_PER_LINE              6
#define SETUP_MENU_FIVE_CHAR_COUNT             5
#define SETUP_MENU_CENTERED_MARGIN             1
#define SETUP_MENU_VOLTAGE_REFRESH_INTERVAL_MS 100
#define SETUP_MENU_VOLTAGE_BUFFER_SIZE         7
#define SETUP_MENU_BOTTOM_TEXT_BASELINE        (OLED_FIRST_TEXT_BASELINE + (11 * OLED_TEXT_LINE_HEIGHT))
#if FAN_PWM_ENABLED
#define SETUP_MENU_FAN_ITEMS                                                                                           \
    "OFF|ON\n100%|ON\n25%|ON\n50%|ON\n75%|AUTO\n100/0\nCOOL|AUTO\n100/0\nQUIET|AUTO\n50%/0\nCOOL|"                     \
    "AUTO\n50%/0\nQUIET|AUTO\n25%/0\nCOOL|AUTO\n25%/0\nQUIET|ADAPT\n25%|ADAPT\n50%|ADAPT\n75%|ADAPT\n100%|"            \
    "ADAPT\n150%"
#define SETUP_MENU_FAN_ITEM_COUNT 16
#else
#define SETUP_MENU_FAN_ITEMS      "OFF|ON\n100%|AUTO\n100/0\nCOOL|AUTO\n100/0\nQUIET"
#define SETUP_MENU_FAN_ITEM_COUNT 4
#endif
#ifdef SHOW_SPLASH
#define SETUP_MENU_LAST_VALUE_ITEM SETUP_ITEM_SHOW_SPLASH
#else
#define SETUP_MENU_LAST_VALUE_ITEM SETUP_ITEM_INPUT_V_CALIB
#endif

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

enum
{
    SETUP_ITEM_STARTUP_POWER_LEVEL = 0,
    SETUP_ITEM_FAN_MODE,
#if FAN_PWM_ENABLED
    SETUP_ITEM_FAN_SIGNAL_POLARITY,
#endif
    SETUP_ITEM_AUTO_SLEEP,
    SETUP_ITEM_AUTO_DIM,
    SETUP_ITEM_IDLE_DETECT_THRESH,
    SETUP_ITEM_BATTERY_MODE,
    SETUP_ITEM_INPUT_V_CALIB,
#ifdef SHOW_SPLASH
    SETUP_ITEM_SHOW_SPLASH,
#endif
    SETUP_ITEM_SAVE_AND_EXIT,
    SETUP_ITEM_EXIT_NO_SAVE,
    SETUP_MENU_ITEM_COUNT,
};

typedef struct
{
    const char* title;
    const char* items;
    uint8_t     items_cnt;
} setup_menu_item_t;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static const setup_menu_item_t setup_menu_items[SETUP_MENU_ITEM_COUNT] = {
    [SETUP_ITEM_STARTUP_POWER_LEVEL] =
        {
                                          .title     = "START\nPOWER\nLEVEL",
                                          .items     = "SPORT|NORMAL|ECO",
                                          .items_cnt = 3,
                                          },
    [SETUP_ITEM_FAN_MODE] =
        {
                                          .title     = "FAN\nMODE",
                                          .items     = SETUP_MENU_FAN_ITEMS,
                                          .items_cnt = SETUP_MENU_FAN_ITEM_COUNT,
                                          },
#if FAN_PWM_ENABLED
    [SETUP_ITEM_FAN_SIGNAL_POLARITY] =
        {
                                          .title     = "FAN\nSIGNL\nPOLAR",
                                          .items     = "DIRCT|INVRT",
                                          .items_cnt = 2,
                                          },
#endif
    [SETUP_ITEM_AUTO_SLEEP] =
        {
                                          .title     = "AUTO\nSLEEP",
                                          .items     = "OFF|5 MIN|15 MIN|30 MIN",
                                          .items_cnt = 4,
                                          },
    [SETUP_ITEM_AUTO_DIM] =
        {
                                          .title     = "AUTO\nDIM",
                                          .items     = "OFF|15 SEC|30 SEC|60 SEC",
                                          .items_cnt = 4,
                                          },
    [SETUP_ITEM_IDLE_DETECT_THRESH] =
        {
                                          .title     = "ACTIVE\nMIN W",
                                          .items     = "1 W|2 W|5 W|10 W|20 W|30 W|40 W",
                                          .items_cnt = 7,
                                          },
    [SETUP_ITEM_BATTERY_MODE] =
        {
                                          .title     = "BATT\nMODE",
                                          .items     = "NONE|LiPo|LiPo\nSAFER|LiHV|LiHV\nSAFER|LiFE|LiFE\nSAFER",
                                          .items_cnt = 7,
                                          },
    [SETUP_ITEM_INPUT_V_CALIB] =
        {
                                          .title     = "INPUT\nVOLT\nCALIB",
                                          .items     = "0|+1|+2|+3|+4|+5|-1|-2|-3|-4|-5",
                                          .items_cnt = 11,
                                          },
#ifdef SHOW_SPLASH
    [SETUP_ITEM_SHOW_SPLASH] =
        {
                                          .title     = "SHOW\nSPLASH",
                                          .items     = "NO|YES",
                                          .items_cnt = 2,
                                          },
#endif
    [SETUP_ITEM_SAVE_AND_EXIT] =
        {
                                          .title     = "SAVE\nAND\nEXIT",
                                          .items     = "",
                                          .items_cnt = 0,
                                          },
    [SETUP_ITEM_EXIT_NO_SAVE] =
        {
                                          .title     = "EXIT\nDON'T\nSAVE",
                                          .items     = "",
                                          .items_cnt = 0,
                                          },
};

#if !FAN_PWM_ENABLED
/* Display positions are intentionally separate from persistent mode codes in
 * the GPIO-only build because its supported values are noncontiguous. */
static const uint8_t setup_fan_mode_codes[] = {
    FAN_MODE_OFF,
    FAN_MODE_ON_100_RAMPED,
    FAN_MODE_AUTO_BINARY_100_COOL,
    FAN_MODE_AUTO_BINARY_100_QUIET,
};
#endif

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static uint8_t     setup_menu_get_value(const hotwand_setup_nvm_t* settings, uint8_t item);
static uint8_t     setup_menu_get_option_count(uint8_t item);
static void        setup_menu_cycle_value(hotwand_setup_nvm_t* settings, uint8_t item);
static const char* setup_menu_find_option(const char* items, uint8_t option);
static uint8_t     setup_menu_draw_text(u8g2_t* graphics, const char* text, char terminator, uint8_t baseline);
static void        setup_menu_draw_line(u8g2_t* graphics, const char* text, size_t length, uint8_t baseline);
static void        setup_menu_render(u8g2_t* graphics, const hotwand_setup_nvm_t* settings, uint8_t item);
static void        setup_menu_draw_calibrated_voltage(u8g2_t* graphics, uint8_t calibration);
static void        setup_menu_exit(const hotwand_setup_nvm_t* settings, bool save);
#if !FAN_PWM_ENABLED
static uint8_t setup_menu_get_fan_option(uint8_t mode);
#endif

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void setup_menu(void)
{
    hotwand_setup_nvm_t settings;
    u8g2_t*             graphics;
    uint32_t            last_activity_ms;
    uint32_t            last_render_ms;
    uint8_t             selected_item = SETUP_ITEM_STARTUP_POWER_LEVEL;

    /* Both energy-producing outputs must be safe before entering the menu. */
    rfgen_stop();
    pwrlvl_force_minimum();

    btn_init();
    nvm_init();
    if (!nvm_read(&settings))
    {
        nvm_apply_defaults(&settings);
    }
    settings.fan_mode = fan_normalize_mode(settings.fan_mode);

    graphics = OLED_GetGraphics(&oled);
    if (graphics != NULL)
    {
        setup_menu_render(graphics, &settings, selected_item);
    }

    /* In the menu, a short press is an action only after a short release. */
    btn_set_short_press_mode(BTN_SHORT_PRESS_ON_RELEASE);

    /* Discard the events used by boot logic to enter this function. */
    btn_has_short_press(true);
    btn_has_long_press(true);
    last_activity_ms = systick_get_ms();
    last_render_ms   = last_activity_ms;

    for (;;)
    {
        uint32_t now;

        btn_task();
        now = systick_get_ms();

        if ((uint32_t)(now - last_activity_ms) >= SETUP_MENU_TIMEOUT_MS)
        {
            enter_sleep_mode();
        }

        if (btn_has_short_press(true))
        {
            selected_item    = (uint8_t)((selected_item + 1) % SETUP_MENU_ITEM_COUNT);
            last_activity_ms = now;
            last_render_ms   = now;
            setup_menu_render(graphics, &settings, selected_item);
        }

        if (btn_has_long_press(true))
        {
            last_activity_ms = now;

            if (selected_item == SETUP_ITEM_SAVE_AND_EXIT)
            {
                setup_menu_exit(&settings, true);
            }
            else if (selected_item == SETUP_ITEM_EXIT_NO_SAVE)
            {
                setup_menu_exit(&settings, false);
            }
            else
            {
                setup_menu_cycle_value(&settings, selected_item);
                last_render_ms = now;
                setup_menu_render(graphics, &settings, selected_item);
            }
        }

        /* Only the voltage-calibration page contains changing data. Refresh
         * it independently of button input so its preview remains live. */
        if ((selected_item == SETUP_ITEM_INPUT_V_CALIB) &&
            ((uint32_t)(now - last_render_ms) >= SETUP_MENU_VOLTAGE_REFRESH_INTERVAL_MS))
        {
            last_render_ms = now;
            setup_menu_render(graphics, &settings, selected_item);
        }

        HAL_Delay(1);
        watchdog_feed();
    }
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static uint8_t setup_menu_get_value(const hotwand_setup_nvm_t* settings, uint8_t item)
{
    switch (item)
    {
    case SETUP_ITEM_STARTUP_POWER_LEVEL:
        return settings->startup_power_level;
    case SETUP_ITEM_FAN_MODE:
#if FAN_PWM_ENABLED
        return settings->fan_mode;
#else
        return setup_menu_get_fan_option(settings->fan_mode);
#endif
#if FAN_PWM_ENABLED
    case SETUP_ITEM_FAN_SIGNAL_POLARITY:
        return settings->fan_sig_inv;
#endif
    case SETUP_ITEM_AUTO_SLEEP:
        return settings->auto_sleep;
    case SETUP_ITEM_AUTO_DIM:
        return settings->auto_dim;
    case SETUP_ITEM_IDLE_DETECT_THRESH:
        return settings->idle_detect_thresh;
    case SETUP_ITEM_BATTERY_MODE:
        return settings->batt_mode;
    case SETUP_ITEM_INPUT_V_CALIB:
        return settings->input_v_calib;
#ifdef SHOW_SPLASH
    case SETUP_ITEM_SHOW_SPLASH:
        return settings->show_splash;
#endif
    default:
        return 0;
    }
}

static uint8_t setup_menu_get_option_count(uint8_t item)
{
    if (item >= SETUP_MENU_ITEM_COUNT)
    {
        return 0;
    }

    return setup_menu_items[item].items_cnt;
}

static void setup_menu_cycle_value(hotwand_setup_nvm_t* settings, uint8_t item)
{
    uint8_t count = setup_menu_get_option_count(item);
    uint8_t value;

    if ((settings == NULL) || (count == 0))
    {
        return;
    }

    value = (uint8_t)((setup_menu_get_value(settings, item) + 1) % count);

    switch (item)
    {
    case SETUP_ITEM_STARTUP_POWER_LEVEL:
        settings->startup_power_level = value;
        break;
    case SETUP_ITEM_FAN_MODE:
#if FAN_PWM_ENABLED
        settings->fan_mode = value;
#else
        settings->fan_mode = setup_fan_mode_codes[value];
#endif
        break;
#if FAN_PWM_ENABLED
    case SETUP_ITEM_FAN_SIGNAL_POLARITY:
        settings->fan_sig_inv = value;
        break;
#endif
    case SETUP_ITEM_AUTO_SLEEP:
        settings->auto_sleep = value;
        break;
    case SETUP_ITEM_AUTO_DIM:
        settings->auto_dim = value;
        break;
    case SETUP_ITEM_IDLE_DETECT_THRESH:
        settings->idle_detect_thresh = value;
        break;
    case SETUP_ITEM_BATTERY_MODE:
        settings->batt_mode = value;
        break;
    case SETUP_ITEM_INPUT_V_CALIB:
        settings->input_v_calib = value;
        break;
#ifdef SHOW_SPLASH
    case SETUP_ITEM_SHOW_SPLASH:
        settings->show_splash = value;
        break;
#endif
    default:
        break;
    }
}

static const char* setup_menu_find_option(const char* items, uint8_t option)
{
    if (items == NULL)
    {
        return "";
    }

    while ((option > 0) && (*items != '\0'))
    {
        if (*items++ == '|')
        {
            --option;
        }
    }

    return items;
}

static uint8_t setup_menu_draw_text(u8g2_t* graphics, const char* text, char terminator, uint8_t baseline)
{
    const char* line = text;

    while ((*line != '\0') && (*line != terminator))
    {
        const char* end = line;
        size_t      length;

        while ((*end != '\0') && (*end != '\n') && (*end != terminator))
        {
            ++end;
        }

        length = (size_t)(end - line);
        setup_menu_draw_line(graphics, line, length, baseline);
        baseline = (uint8_t)(baseline + OLED_TEXT_LINE_HEIGHT);

        if (*end != '\n')
        {
            break;
        }
        line = end + 1;
    }

    return baseline;
}

static void setup_menu_draw_line(u8g2_t* graphics, const char* text, size_t length, uint8_t baseline)
{
    char   line[SETUP_MENU_CHARS_PER_LINE + 1];
    size_t index;

    if ((graphics == NULL) || (text == NULL))
    {
        return;
    }

    if (length > SETUP_MENU_CHARS_PER_LINE)
    {
        length = SETUP_MENU_CHARS_PER_LINE;
    }

    for (index = 0; index < length; ++index)
    {
        line[index] = text[index];
    }
    line[length] = '\0';

    u8g2_SetFont(graphics, length <= SETUP_MENU_FIVE_CHAR_COUNT ? u8g2_font_6x10_tr : u8g2_font_5x7_tr);
    u8g2_DrawStr(graphics, SETUP_MENU_CENTERED_MARGIN, baseline, line);
}

static void setup_menu_render(u8g2_t* graphics, const hotwand_setup_nvm_t* settings, uint8_t item)
{
    const setup_menu_item_t* menu_item;
    const char*              option;
    uint8_t                  baseline;

    if ((graphics == NULL) || (settings == NULL) || (item >= SETUP_MENU_ITEM_COUNT))
    {
        return;
    }

    menu_item = &setup_menu_items[item];

    u8g2_ClearBuffer(graphics);

    setup_menu_draw_line(graphics, "SETUP", SETUP_MENU_FIVE_CHAR_COUNT, OLED_FIRST_TEXT_BASELINE);

    /* Advance two rows: row two is deliberately blank. */
    baseline = (uint8_t)(OLED_FIRST_TEXT_BASELINE + (2 * OLED_TEXT_LINE_HEIGHT));
    baseline = setup_menu_draw_text(graphics, menu_item->title, '\0', baseline);

    if (item <= SETUP_MENU_LAST_VALUE_ITEM)
    {
        setup_menu_draw_line(graphics, "  =  ", SETUP_MENU_FIVE_CHAR_COUNT, baseline);
        baseline = (uint8_t)(baseline + OLED_TEXT_LINE_HEIGHT);
        option   = setup_menu_find_option(menu_item->items, setup_menu_get_value(settings, item));
        setup_menu_draw_text(graphics, option, '|', baseline);
    }

    if (item == SETUP_ITEM_INPUT_V_CALIB)
    {
        setup_menu_draw_calibrated_voltage(graphics, settings->input_v_calib);
    }

    OLED_SendBuffer(&oled);
}

static void setup_menu_draw_calibrated_voltage(u8g2_t* graphics, uint8_t calibration)
{
    char   voltage[SETUP_MENU_VOLTAGE_BUFFER_SIZE];
    size_t voltage_length;

    /* Apply the unsaved menu value before reading. This deliberately uses the
     * same ADC calibration path as normal operation rather than duplicating
     * its slope and offset calculations in the UI. */
    adc_set_input_voltage_calibration(calibration);
    millivolts_to_str(adc_to_millivolts(DC_SENS_IDX), voltage, 1, &voltage_length);
    voltage[voltage_length++] = 'V';
    voltage[voltage_length]   = '\0';

    setup_menu_draw_line(graphics, voltage, voltage_length, SETUP_MENU_BOTTOM_TEXT_BASELINE);
}

static void setup_menu_exit(const hotwand_setup_nvm_t* settings, bool save)
{
    if (save)
    {
        nvm_save(settings);
    }

    NVIC_SystemReset();

    /* NVIC_SystemReset() does not return, but keep control contained if a
     * debugger suppresses the reset request.
     */
    for (;;)
    {
        /* Deliberately expire IWDG if the requested reset is suppressed. */
    }
}

#if !FAN_PWM_ENABLED
static uint8_t setup_menu_get_fan_option(uint8_t mode)
{
    uint8_t option;

    for (option = 0; option < (sizeof(setup_fan_mode_codes) / sizeof(setup_fan_mode_codes[0])); ++option)
    {
        if (setup_fan_mode_codes[option] == mode)
        {
            return option;
        }
    }

    /* fan_normalize_mode() normally makes this unreachable. Keep the menu on
     * its AUTO COOL entry if a corrupt value reaches this helper. */
    return 2;
}
#endif
