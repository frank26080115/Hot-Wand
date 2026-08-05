#pragma once

#include <stddef.h>
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
    IDLE_DETECT_THRESH_1W  = 0,
    IDLE_DETECT_THRESH_2W  = 1,
    IDLE_DETECT_THRESH_5W  = 2,
    IDLE_DETECT_THRESH_10W = 3,
    IDLE_DETECT_THRESH_20W = 4,
    IDLE_DETECT_THRESH_30W = 5,
    IDLE_DETECT_THRESH_40W = 6,
};

enum
{
    BATT_MODE_NONE      = 0,
    BATT_MODE_LIPO      = 1,
    BATT_MODE_LIPO_SAFE = 2,
    BATT_MODE_LIHV      = 3,
    BATT_MODE_LIHV_SAFE = 4,
    BATT_MODE_LIFE      = 5,
    BATT_MODE_LIFE_SAFE = 6,
};

enum
{
    INPUT_VOLTAGE_CALIB_NONE = 0,
    INPUT_VOLTAGE_CALIB_P1   = 1,
    INPUT_VOLTAGE_CALIB_P2   = 2,
    INPUT_VOLTAGE_CALIB_P3   = 3,
    INPUT_VOLTAGE_CALIB_P4   = 4,
    INPUT_VOLTAGE_CALIB_P5   = 5,
    INPUT_VOLTAGE_CALIB_N1   = 6,
    INPUT_VOLTAGE_CALIB_N2   = 7,
    INPUT_VOLTAGE_CALIB_N3   = 8,
    INPUT_VOLTAGE_CALIB_N4   = 9,
    INPUT_VOLTAGE_CALIB_N5   = 10,
};

typedef struct __attribute__((packed, aligned(2)))
{
    uint8_t magic; // 0xFF or 0x00 is invalid

    uint8_t startup_power_level : 2;
    uint8_t fan_mode : 2;
    uint8_t auto_sleep : 2;
    uint8_t auto_dim : 2;
    uint8_t idle_detect_thresh : 3;
    uint8_t batt_mode : 3;
    uint8_t rsvd_1 : 2;

    uint8_t  input_v_calib : 4;
    uint8_t  rsvd_2 : 4;
    uint16_t checksum;
} hotwand_setup_nvm_t;

#if defined(__cplusplus)
static_assert(offsetof(hotwand_setup_nvm_t, checksum) == 4U, "Unexpected setup NVM checksum offset");
static_assert(sizeof(hotwand_setup_nvm_t) == 6U, "Unexpected setup NVM record size");
static_assert(alignof(hotwand_setup_nvm_t) == 2U, "Setup NVM records must be halfword aligned");
#else
_Static_assert(offsetof(hotwand_setup_nvm_t, checksum) == 4U, "Unexpected setup NVM checksum offset");
_Static_assert(sizeof(hotwand_setup_nvm_t) == 6U, "Unexpected setup NVM record size");
_Static_assert(_Alignof(hotwand_setup_nvm_t) == 2U, "Setup NVM records must be halfword aligned");
#endif
