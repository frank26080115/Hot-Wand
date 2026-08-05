// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "nvm.h"

#include "miscutils.h"
#include "stm32f0xx_hal.h"

#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define NVM_EXPECTED_PAGE_SIZE 1024
#define NVM_RECORD_MAGIC 0xA5
#define NVM_ERASED_HALFWORD 0xFFFF
#define NVM_SLOT_SIZE_BYTES ((uint16_t)sizeof(hotwand_setup_nvm_t))

#if FLASH_PAGE_SIZE != NVM_EXPECTED_PAGE_SIZE
#error "The NVM layout requires 1 KiB flash erase pages"
#endif

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

/* These symbols bound the page reserved by the target's NVM linker script. */
extern const uint8_t __nvm_page_start__;
extern const uint8_t __nvm_page_end__;

static hotwand_setup_nvm_t nvm_saved_settings;
static uint16_t            nvm_next_slot;
static bool                nvm_has_saved_settings;
static bool                nvm_erase_required;
static bool                nvm_initialized;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static uintptr_t nvm_page_start(void);
static uintptr_t nvm_page_end(void);
static uint16_t  nvm_slot_count(void);
static uintptr_t nvm_slot_address(uint16_t slot);
static uint16_t  nvm_read_halfword(uintptr_t address);
static void      nvm_read_record(uint16_t slot, hotwand_setup_nvm_t* record);
static bool      nvm_slot_is_erased(uint16_t slot);
static bool      nvm_page_layout_is_valid(void);
static bool      nvm_page_is_erased(void);
static bool      nvm_settings_fields_are_valid(const hotwand_setup_nvm_t* settings);
static uint16_t  nvm_checksum(const hotwand_setup_nvm_t* settings);
static bool      nvm_record_is_valid(const hotwand_setup_nvm_t* record);
static void      nvm_prepare_record(const hotwand_setup_nvm_t* settings, hotwand_setup_nvm_t* record);
static bool      nvm_records_are_equal(const hotwand_setup_nvm_t* left, const hotwand_setup_nvm_t* right);
static void      nvm_scan_page(void);
static bool      nvm_erase_page(void);
static bool      nvm_program_record(uint16_t slot, const hotwand_setup_nvm_t* record);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void nvm_init(void)
{
    if (nvm_initialized)
    {
        return;
    }

    nvm_has_saved_settings = false;
    nvm_erase_required     = false;
    nvm_next_slot          = 0;

    if (!nvm_page_layout_is_valid())
    {
        return;
    }

    nvm_scan_page();
    nvm_initialized = true;
}

void nvm_apply_defaults(hotwand_setup_nvm_t* settings)
{
    if (settings == NULL)
    {
        return;
    }

    *settings                     = (hotwand_setup_nvm_t){0};
    settings->startup_power_level = POWER_LEVEL_MAX;
    settings->fan_mode            = FAN_MODE_AUTO_LOW;
    settings->auto_sleep          = AUTO_SLEEP_OFF;
    settings->auto_dim            = AUTO_DIM_OFF;
    settings->idle_detect_thresh  = IDLE_DETECT_THRESH_10W;
    settings->batt_mode           = BATT_MODE_NONE;
    settings->input_v_calib       = INPUT_VOLTAGE_CALIB_NONE;
}

bool nvm_read(hotwand_setup_nvm_t* settings)
{
    if (!nvm_initialized || !nvm_has_saved_settings || (settings == NULL))
    {
        return false;
    }

    *settings = nvm_saved_settings;
    return true;
}

bool nvm_save(const hotwand_setup_nvm_t* settings)
{
    hotwand_setup_nvm_t record;
    bool                success;

    if (!nvm_initialized || !nvm_settings_fields_are_valid(settings))
    {
        return false;
    }

    nvm_prepare_record(settings, &record);

    /* Do not spend a flash cycle saving an unchanged configuration. */
    if (nvm_has_saved_settings && nvm_records_are_equal(&record, &nvm_saved_settings))
    {
        return true;
    }

    if (nvm_erase_required || (nvm_next_slot >= nvm_slot_count()))
    {
        if (!nvm_erase_page())
        {
            return false;
        }
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }

    success = nvm_program_record(nvm_next_slot, &record);
    HAL_FLASH_Lock();

    if (!success)
    {
        /* Treat a partially programmed slot as consumed on the next try. */
        nvm_scan_page();
        return false;
    }

    nvm_saved_settings     = record;
    nvm_has_saved_settings = true;
    ++nvm_next_slot;
    return true;
}

bool nvm_factory_reset(void)
{
    /* Permit factory reset to be used independently of normal startup while
     * retaining the same reserved-page layout validation. */
    nvm_init();
    if (!nvm_initialized)
    {
        return false;
    }

    return nvm_erase_page();
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static uintptr_t nvm_page_start(void)
{
    return (uintptr_t)&__nvm_page_start__;
}

static uintptr_t nvm_page_end(void)
{
    return (uintptr_t)&__nvm_page_end__;
}

static uint16_t nvm_slot_count(void)
{
    return (uint16_t)(NVM_EXPECTED_PAGE_SIZE / NVM_SLOT_SIZE_BYTES);
}

static uintptr_t nvm_slot_address(uint16_t slot)
{
    return nvm_page_start() + ((uintptr_t)slot * NVM_SLOT_SIZE_BYTES);
}

static uint16_t nvm_read_halfword(uintptr_t address)
{
    return *(const volatile uint16_t*)address;
}

static void nvm_read_record(uint16_t slot, hotwand_setup_nvm_t* record)
{
    const volatile uint8_t* source      = (const volatile uint8_t*)nvm_slot_address(slot);
    uint8_t*                destination = (uint8_t*)record;
    uint16_t                index;

    for (index = 0; index < NVM_SLOT_SIZE_BYTES; ++index)
    {
        destination[index] = source[index];
    }
}

static bool nvm_slot_is_erased(uint16_t slot)
{
    uintptr_t address = nvm_slot_address(slot);
    uint16_t  offset;

    for (offset = 0; offset < NVM_SLOT_SIZE_BYTES; offset += 2)
    {
        if (nvm_read_halfword(address + offset) != NVM_ERASED_HALFWORD)
        {
            return false;
        }
    }

    return true;
}

static bool nvm_page_layout_is_valid(void)
{
    uintptr_t start = nvm_page_start();
    uintptr_t end   = nvm_page_end();

    return (end > start) && ((end - start) == NVM_EXPECTED_PAGE_SIZE) && ((start % NVM_EXPECTED_PAGE_SIZE) == 0) &&
           ((start % 2) == 0) && ((NVM_SLOT_SIZE_BYTES % 2) == 0);
}

static bool nvm_page_is_erased(void)
{
    uintptr_t address;

    for (address = nvm_page_start(); address < nvm_page_end(); address += 2)
    {
        if (nvm_read_halfword(address) != NVM_ERASED_HALFWORD)
        {
            return false;
        }
    }

    return true;
}

static bool nvm_settings_fields_are_valid(const hotwand_setup_nvm_t* settings)
{
    if (settings == NULL)
    {
        return false;
    }

    switch (settings->startup_power_level)
    {
    case POWER_LEVEL_MAX:
    case POWER_LEVEL_75W:
    case POWER_LEVEL_50W:
        break;

    default:
        return false;
    }

    switch (settings->fan_mode)
    {
    case FAN_MODE_OFF:
    case FAN_MODE_ON:
    case FAN_MODE_AUTO_LOW:
    case FAN_MODE_AUTO_HIGH:
        break;

    default:
        return false;
    }

    switch (settings->auto_sleep)
    {
    case AUTO_SLEEP_OFF:
    case AUTO_SLEEP_5MIN:
    case AUTO_SLEEP_15MIN:
    case AUTO_SLEEP_30MIN:
        break;

    default:
        return false;
    }

    switch (settings->auto_dim)
    {
    case AUTO_DIM_OFF:
    case AUTO_DIM_15SEC:
    case AUTO_DIM_30SEC:
    case AUTO_DIM_60SEC:
        break;

    default:
        return false;
    }

    switch (settings->idle_detect_thresh)
    {
    case IDLE_DETECT_THRESH_1W:
    case IDLE_DETECT_THRESH_2W:
    case IDLE_DETECT_THRESH_5W:
    case IDLE_DETECT_THRESH_10W:
    case IDLE_DETECT_THRESH_20W:
    case IDLE_DETECT_THRESH_30W:
    case IDLE_DETECT_THRESH_40W:
        break;

    default:
        return false;
    }

    switch (settings->batt_mode)
    {
    case BATT_MODE_NONE:
    case BATT_MODE_LIPO:
    case BATT_MODE_LIPO_SAFE:
    case BATT_MODE_LIHV:
    case BATT_MODE_LIHV_SAFE:
    case BATT_MODE_LIFE:
    case BATT_MODE_LIFE_SAFE:
        break;

    default:
        return false;
    }

    switch (settings->input_v_calib)
    {
    case INPUT_VOLTAGE_CALIB_NONE:
    case INPUT_VOLTAGE_CALIB_P1:
    case INPUT_VOLTAGE_CALIB_P2:
    case INPUT_VOLTAGE_CALIB_P3:
    case INPUT_VOLTAGE_CALIB_P4:
    case INPUT_VOLTAGE_CALIB_P5:
    case INPUT_VOLTAGE_CALIB_N1:
    case INPUT_VOLTAGE_CALIB_N2:
    case INPUT_VOLTAGE_CALIB_N3:
    case INPUT_VOLTAGE_CALIB_N4:
    case INPUT_VOLTAGE_CALIB_N5:
        break;

    default:
        return false;
    }

    return true;
}

static uint16_t nvm_checksum(const hotwand_setup_nvm_t* settings)
{
    return fletcher16((const uint8_t*)settings, offsetof(hotwand_setup_nvm_t, checksum));
}

static bool nvm_record_is_valid(const hotwand_setup_nvm_t* record)
{
    return (record->magic == NVM_RECORD_MAGIC) && (record->rsvd_1 == 0) && (record->rsvd_2 == 0) &&
           nvm_settings_fields_are_valid(record) && (record->checksum == nvm_checksum(record));
}

static void nvm_prepare_record(const hotwand_setup_nvm_t* settings, hotwand_setup_nvm_t* record)
{
    record->magic               = NVM_RECORD_MAGIC;
    record->startup_power_level = settings->startup_power_level;
    record->fan_mode            = settings->fan_mode;
    record->auto_sleep          = settings->auto_sleep;
    record->auto_dim            = settings->auto_dim;
    record->idle_detect_thresh  = settings->idle_detect_thresh;
    record->batt_mode           = settings->batt_mode;
    record->input_v_calib       = settings->input_v_calib;
    record->rsvd_1              = 0;
    record->rsvd_2              = 0;
    record->checksum            = nvm_checksum(record);
}

static bool nvm_records_are_equal(const hotwand_setup_nvm_t* left, const hotwand_setup_nvm_t* right)
{
    const uint8_t* left_bytes  = (const uint8_t*)left;
    const uint8_t* right_bytes = (const uint8_t*)right;
    uint16_t       index;

    for (index = 0; index < NVM_SLOT_SIZE_BYTES; ++index)
    {
        if (left_bytes[index] != right_bytes[index])
        {
            return false;
        }
    }

    return true;
}

static void nvm_scan_page(void)
{
    hotwand_setup_nvm_t record;
    uint16_t            slot;
    bool                found_erased_slot = false;

    nvm_has_saved_settings = false;
    nvm_erase_required     = false;
    nvm_next_slot          = nvm_slot_count();

    for (slot = 0; slot < nvm_slot_count(); ++slot)
    {
        if (nvm_slot_is_erased(slot))
        {
            if (!found_erased_slot)
            {
                nvm_next_slot     = slot;
                found_erased_slot = true;
            }
            continue;
        }

        /* Data after a blank slot indicates an interrupted page erase. */
        if (found_erased_slot)
        {
            nvm_erase_required = true;
            continue;
        }

        nvm_read_record(slot, &record);
        if (nvm_record_is_valid(&record))
        {
            nvm_saved_settings     = record;
            nvm_has_saved_settings = true;
        }
    }

    if (!found_erased_slot)
    {
        nvm_erase_required = true;
    }
}

static bool nvm_erase_page(void)
{
    FLASH_EraseInitTypeDef erase_cfg = {0};
    HAL_StatusTypeDef      status;
    uint32_t               page_error = 0;

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return false;
    }

    erase_cfg.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase_cfg.PageAddress = (uint32_t)nvm_page_start();
    erase_cfg.NbPages     = 1;

    status = HAL_FLASHEx_Erase(&erase_cfg, &page_error);
    HAL_FLASH_Lock();

    if ((status != HAL_OK) || (page_error != 0xFFFFFFFF) || !nvm_page_is_erased())
    {
        nvm_scan_page();
        return false;
    }

    nvm_next_slot          = 0;
    nvm_has_saved_settings = false;
    nvm_erase_required     = false;
    return true;
}

static bool nvm_program_record(uint16_t slot, const hotwand_setup_nvm_t* record)
{
    const uint8_t*      bytes   = (const uint8_t*)record;
    uintptr_t           address = nvm_slot_address(slot);
    hotwand_setup_nvm_t verify;
    HAL_StatusTypeDef   status;
    uint16_t            halfword;
    uint16_t            offset;

    if ((slot >= nvm_slot_count()) || !nvm_slot_is_erased(slot))
    {
        return false;
    }

    /*
     * Program the magic-containing halfword last.  Until that succeeds, a
     * reset can leave only an invalid, consumed journal slot.
     */
    for (offset = 2; offset < NVM_SLOT_SIZE_BYTES; offset += 2)
    {
        halfword = (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1] << 8);
        status   = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, (uint32_t)(address + offset), halfword);
        if ((status != HAL_OK) || (nvm_read_halfword(address + offset) != halfword))
        {
            return false;
        }
    }

    halfword = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
    status   = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, (uint32_t)address, halfword);
    if ((status != HAL_OK) || (nvm_read_halfword(address) != halfword))
    {
        return false;
    }

    nvm_read_record(slot, &verify);
    return nvm_record_is_valid(&verify) && nvm_records_are_equal(&verify, record);
}
