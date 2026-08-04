#ifndef HOT_WAND_TYPEDEFS_H
#define HOT_WAND_TYPEDEFS_H

#include <stdint.h>

enum
{
    FAN_MODE_OFF       = 0,
    FAN_MODE_ON        = 1,
    FAN_MODE_AUTO_LOW  = 2,
    FAN_MODE_AUTO_HIGH = 3,
};

enum
{
    POWER_LEVEL_MAX = 0,
    POWER_LEVEL_75W = 1,
    POWER_LEVEL_50W = 2,
};

enum
{
    AUTO_SLEEP_OFF   = 0,
    AUTO_SLEEP_5MIN  = 1,
    AUTO_SLEEP_15MIN = 2,
    AUTO_SLEEP_30MIN = 3,
};

enum
{
    AUTO_DIM_OFF   = 0,
    AUTO_DIM_15SEC = 1,
    AUTO_DIM_30SEC = 2,
    AUTO_DIM_60SEC = 3,
};

enum
{
    IDLE_DETECT_THRESH_1W   = 0,
    IDLE_DETECT_THRESH_2W   = 1,
    IDLE_DETECT_THRESH_5W   = 2,
    IDLE_DETECT_THRESH_10W  = 3,
    IDLE_DETECT_THRESH_20W  = 4,
    IDLE_DETECT_THRESH_40W  = 5,
};

typedef struct
{
    uint8_t startup_power_level : 2;
    uint8_t fan_mode            : 2;
    uint8_t auto_sleep          : 2;
    uint8_t auto_dim            : 2;
    uint8_t idle_detect_thresh  : 3;

    uint16_t checksum;
}
hotwand_setup_nvm_t;

enum
{
    SETUP_ITEM_STARTUP_POWER_LEVEL = 0,
    SETUP_ITEM_FAN_MODE            = 1,
    SETUP_ITEM_AUTO_SLEEP          = 2,
    SETUP_ITEM_AUTO_DIM            = 3,
    SETUP_ITEM_IDLE_DETECT_THRESH  = 4,
    SETUP_ITEM_SAVE_AND_EXIT       = 5,
    SETUP_ITEM_EXIT_NO_SAVE        = 6,
};

enum
{
    SETUP_MENU_CHARS_PER_LINE     = 5,
    SETUP_MENU_TITLE_MAX_LINES    = 3,
    SETUP_MENU_MAX_OPTION_COUNT   = 6,
    SETUP_MENU_TITLE_CAPACITY     =
        (SETUP_MENU_CHARS_PER_LINE * SETUP_MENU_TITLE_MAX_LINES) +
        (SETUP_MENU_TITLE_MAX_LINES - 1) + 1,
    SETUP_MENU_OPTIONS_CAPACITY   =
        (SETUP_MENU_CHARS_PER_LINE * SETUP_MENU_MAX_OPTION_COUNT) +
        (SETUP_MENU_MAX_OPTION_COUNT - 1) + 1,
};

typedef struct
{
    char title[SETUP_MENU_TITLE_CAPACITY];
    char items[SETUP_MENU_OPTIONS_CAPACITY];
}
setup_menu_item_t;

setup_menu_item_t setup_menu_items[] = {
    [SETUP_ITEM_STARTUP_POWER_LEVEL] = {
        .title = "START\nPOWER\nLEVEL",
        .items = "MAX|75W|50W",
    },
    [SETUP_ITEM_FAN_MODE] = {
        .title = "FAN\nMODE",
        .items = "OFF|ON|AUTOL|AUTOH",
    },
    [SETUP_ITEM_AUTO_SLEEP] = {
        .title = "AUTO\nSLEEP",
        .items = "OFF|5 MIN|15MIN|30MIN",
    },
    [SETUP_ITEM_AUTO_DIM] = {
        .title = "AUTO\nDIM",
        .items = "OFF|15SEC|30SEC|60SEC",
    },
    [SETUP_ITEM_IDLE_DETECT_THRESH] = {
        .title = "ACTIV\nMIN W",
        .items = "1W|2W|5W|10W|20W|40W",
    },
    [SETUP_ITEM_SAVE_AND_EXIT] = {
        .title = "SAVE\nAND\nEXIT",
        .items = "",
    },
    [SETUP_ITEM_EXIT_NO_SAVE] = {
        .title = "EXIT\nNO\nSAVE",
        .items = "",
    },
};

#endif
