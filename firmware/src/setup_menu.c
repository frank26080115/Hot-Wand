// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "setup_menu.h"

#include "button.h"
#include "nvm.h"
#include "oled.h"
#include "pwrlvl.h"
#include "rfgen.h"
#include "stm32f0xx_hal.h"
#include "systick.h"
#include "typedefs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define SETUP_MENU_CHARS_PER_LINE  6
#define SETUP_MENU_LAST_VALUE_ITEM SETUP_ITEM_INPUT_V_CALIB
#define SETUP_MENU_LINE_HEIGHT     10
#define SETUP_MENU_FIRST_BASELINE  9

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

enum
{
    SETUP_ITEM_STARTUP_POWER_LEVEL = 0,
    SETUP_ITEM_FAN_MODE,
    SETUP_ITEM_AUTO_SLEEP,
    SETUP_ITEM_AUTO_DIM,
    SETUP_ITEM_IDLE_DETECT_THRESH,
    SETUP_ITEM_BATTERY_MODE,
    SETUP_ITEM_INPUT_V_CALIB,
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
            .items     = "SPORT|NORM|ECO",
            .items_cnt = 3,
        },
    [SETUP_ITEM_FAN_MODE] =
        {
            .title     = "FAN\nMODE",
            .items     = "OFF|ON|AUTO\nLOW|AUTO\nHIGH",
            .items_cnt = 4,
        },
    [SETUP_ITEM_AUTO_SLEEP] =
        {
            .title     = "AUTO\nSLEEP",
            .items     = "OFF|5 MIN|15MIN|30MIN",
            .items_cnt = 4,
        },
    [SETUP_ITEM_AUTO_DIM] =
        {
            .title     = "AUTO\nDIM",
            .items     = "OFF|15 S|30 S|60 S",
            .items_cnt = 4,
        },
    [SETUP_ITEM_IDLE_DETECT_THRESH] =
        {
            .title     = "ACTIV\nMIN W",
            .items     = "1 W|2 W|5 W|10 W|20 W|30 W|40 W",
            .items_cnt = 7,
        },
    [SETUP_ITEM_BATTERY_MODE] =
        {
            .title     = "BATT\nMODE",
            .items     = "NONE|LIPO|LIPO\nSAFER|LIHV|LIHV\nSAFER|LIFE|LIFE\nSAFER",
            .items_cnt = 7,
        },
    [SETUP_ITEM_INPUT_V_CALIB] =
        {
            .title     = "INPUT\nVOLT\nCALIB",
            .items     = "0|+1|+2|+3|+4|+5|-1|-2|-3|-4|-5",
            .items_cnt = 11,
        },
    [SETUP_ITEM_SAVE_AND_EXIT] =
        {
            .title     = "SAVE\nAND\nEXIT",
            .items     = "",
            .items_cnt = 0,
        },
    [SETUP_ITEM_EXIT_NO_SAVE] =
        {
            .title     = "EXIT\nNO\nSAVE",
            .items     = "",
            .items_cnt = 0,
        },
};

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
static void        setup_menu_exit(const hotwand_setup_nvm_t* settings, bool save);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void setup_menu(void)
{
    hotwand_setup_nvm_t settings;
    u8g2_t*             graphics;
    uint32_t            last_activity_ms;
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
                setup_menu_render(graphics, &settings, selected_item);
            }
        }

        HAL_Delay(1);
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
        return settings->fan_mode;
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
        settings->fan_mode = value;
        break;
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

        while ((*end != '\0') && (*end != '\n') && (*end != terminator))
        {
            ++end;
        }

        setup_menu_draw_line(graphics, line, (size_t)(end - line), baseline);
        baseline = (uint8_t)(baseline + SETUP_MENU_LINE_HEIGHT);

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

    u8g2_DrawStr(graphics, 1, baseline, line);
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

    setup_menu_draw_line(graphics, "SETUP", 5, SETUP_MENU_FIRST_BASELINE);

    /* Advance two rows: row two is deliberately blank. */
    baseline = (uint8_t)(SETUP_MENU_FIRST_BASELINE + (2 * SETUP_MENU_LINE_HEIGHT));
    baseline = setup_menu_draw_text(graphics, menu_item->title, '\0', baseline);

    if (item <= SETUP_MENU_LAST_VALUE_ITEM)
    {
        setup_menu_draw_line(graphics, "  =  ", 5, baseline);
        baseline = (uint8_t)(baseline + SETUP_MENU_LINE_HEIGHT);
        option   = setup_menu_find_option(menu_item->items, setup_menu_get_value(settings, item));
        setup_menu_draw_text(graphics, option, '|', baseline);
    }

    OLED_SendBuffer(&oled);
}

static void setup_menu_exit(const hotwand_setup_nvm_t* settings, bool save)
{
    if (save)
    {
        nvm_save(settings);
    }

    NVIC_SystemReset();

    /* NVIC_SystemReset() does not return, but keep control contained if a
     * debugger suppresses the reset request. */
    for (;;)
    {
    }
}
