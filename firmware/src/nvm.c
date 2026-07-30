#include "nvm.h"

#include "stm32f0xx_hal.h"
#include "systick.h"

#include <stddef.h>
#include <stdint.h>

#define NVM_SAVE_DELAY_MS        5000UL
#define NVM_EXPECTED_PAGE_SIZE   1024UL
#define NVM_SLOT_SIZE_BYTES      2UL
#define NVM_ERASED_SLOT          0xFFFFU
#define NVM_COMPLETION_MARKER    0x00U

#if FLASH_PAGE_SIZE != NVM_EXPECTED_PAGE_SIZE
#error "The NVM layout requires 1 KiB flash erase pages"
#endif

/*
 * These symbols bound the page excluded from application FLASH by
 * stm32f030f4_nvm.ld.
 */
extern const uint8_t __nvm_page_start__;
extern const uint8_t __nvm_page_end__;

static pwrlvl_mode_t nvm_saved_mode;
static pwrlvl_mode_t nvm_pending_mode;
static uint32_t nvm_save_requested_ms;
static uint16_t nvm_next_slot;
static bool nvm_has_saved_mode;
static bool nvm_erase_required;
static bool nvm_save_pending;
static bool nvm_initialized;

static uintptr_t nvm_page_start(void);
static uintptr_t nvm_page_end(void);
static uint16_t nvm_slot_count(void);
static uint16_t nvm_read_slot(uint16_t slot);
static bool nvm_page_layout_is_valid(void);
static bool nvm_page_is_erased(void);
static void nvm_scan_page(void);
static bool nvm_encode_mode(pwrlvl_mode_t mode, uint8_t *encoded);
static bool nvm_decode_mode(uint8_t encoded, pwrlvl_mode_t *mode);
static bool nvm_write_mode(pwrlvl_mode_t mode);

void nvm_init(void)
{
    if (nvm_initialized) {
        return;
    }

    nvm_has_saved_mode = false;
    nvm_erase_required = false;
    nvm_save_pending = false;
    nvm_next_slot = 0U;

    if (!nvm_page_layout_is_valid()) {
        return;
    }

    nvm_scan_page();
    nvm_initialized = true;
}

bool nvm_read(pwrlvl_mode_t *mode)
{
    if (!nvm_initialized || !nvm_has_saved_mode || (mode == NULL)) {
        return false;
    }

    *mode = nvm_saved_mode;
    return true;
}

void nvm_save(pwrlvl_mode_t mode)
{
    uint8_t encoded;

    if (!nvm_initialized || !nvm_encode_mode(mode, &encoded)) {
        return;
    }

    /*
     * Returning to the value already in flash cancels an obsolete queued
     * write and avoids spending a flash-program cycle unnecessarily.
     */
    if (nvm_has_saved_mode && (mode == nvm_saved_mode)) {
        nvm_save_pending = false;
        return;
    }

    /*
     * Repeatedly reporting an unchanged mode must not postpone its write
     * forever if the caller invokes nvm_save() from polling logic.
     */
    if (nvm_save_pending && (mode == nvm_pending_mode)) {
        return;
    }

    nvm_pending_mode = mode;
    nvm_save_requested_ms = systick_get_ms();
    nvm_save_pending = true;
}

void nvm_task(void)
{
    pwrlvl_mode_t mode;
    uint32_t now;

    if (!nvm_initialized || !nvm_save_pending) {
        return;
    }

    now = systick_get_ms();
    if ((uint32_t)(now - nvm_save_requested_ms) < NVM_SAVE_DELAY_MS) {
        return;
    }

    mode = nvm_pending_mode;
    if (nvm_write_mode(mode)) {
        nvm_saved_mode = mode;
        nvm_has_saved_mode = true;
        nvm_save_pending = false;
        return;
    }

    /*
     * A failed or interrupted program operation may have consumed a slot.
     * Re-scan before retrying, and delay the retry to avoid hammering a flash
     * fault on every main-loop iteration.
     */
    nvm_scan_page();
    if (nvm_has_saved_mode && (nvm_saved_mode == mode)) {
        nvm_save_pending = false;
    } else {
        nvm_save_requested_ms = now;
    }
}

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

static uint16_t nvm_read_slot(uint16_t slot)
{
    uintptr_t address =
        nvm_page_start() + ((uintptr_t)slot * NVM_SLOT_SIZE_BYTES);

    return *(const volatile uint16_t *)address;
}

static bool nvm_page_layout_is_valid(void)
{
    uintptr_t start = nvm_page_start();
    uintptr_t end = nvm_page_end();

    return (end > start) &&
           ((end - start) == NVM_EXPECTED_PAGE_SIZE) &&
           ((start % NVM_EXPECTED_PAGE_SIZE) == 0U) &&
           ((start % NVM_SLOT_SIZE_BYTES) == 0U);
}

static bool nvm_page_is_erased(void)
{
    uint16_t slot;

    for (slot = 0U; slot < nvm_slot_count(); ++slot) {
        if (nvm_read_slot(slot) != NVM_ERASED_SLOT) {
            return false;
        }
    }

    return true;
}

static void nvm_scan_page(void)
{
    pwrlvl_mode_t mode;
    uint16_t raw;
    uint16_t slot;
    uint8_t encoded;
    uint8_t marker;
    bool found_free_slot = false;

    nvm_has_saved_mode = false;
    nvm_erase_required = false;
    nvm_next_slot = nvm_slot_count();

    for (slot = 0U; slot < nvm_slot_count(); ++slot) {
        raw = nvm_read_slot(slot);
        if (raw == NVM_ERASED_SLOT) {
            if (!found_free_slot) {
                nvm_next_slot = slot;
                found_free_slot = true;
            }
            continue;
        }

        /*
         * Data after the first erased slot can result from a reset during
         * page erase.  It is stale and must never become the journal tail.
         */
        if (found_free_slot) {
            nvm_erase_required = true;
            continue;
        }

        encoded = (uint8_t)(raw & 0xFFU);
        marker = (uint8_t)(raw >> 8U);

        /*
         * Invalid non-erased slots are treated as consumed.  This preserves
         * the preceding valid value after a reset during a flash write.
         */
        if ((marker == NVM_COMPLETION_MARKER) &&
            nvm_decode_mode(encoded, &mode)) {
            nvm_saved_mode = mode;
            nvm_has_saved_mode = true;
        }
    }

    if (!found_free_slot) {
        nvm_erase_required = true;
    }
}

static bool nvm_encode_mode(pwrlvl_mode_t mode, uint8_t *encoded)
{
    uint8_t mode_nibble;

    if (encoded == NULL) {
        return false;
    }

    switch (mode) {
    case PWRLVL_MODE_100_PERCENT:
        mode_nibble = 1U;
        break;

    case PWRLVL_MODE_75_PERCENT:
        mode_nibble = 2U;
        break;

    case PWRLVL_MODE_50_PERCENT:
        mode_nibble = 3U;
        break;

    default:
        return false;
    }

    *encoded = (uint8_t)((mode_nibble << 4U) |
                         ((~mode_nibble) & 0x0FU));
    return true;
}

static bool nvm_decode_mode(uint8_t encoded, pwrlvl_mode_t *mode)
{
    uint8_t mode_nibble = (uint8_t)(encoded >> 4U);
    uint8_t inverse_nibble = (uint8_t)(encoded & 0x0FU);

    if ((mode == NULL) ||
        (mode_nibble == 0U) ||
        (mode_nibble == 0x0FU) ||
        (inverse_nibble != ((~mode_nibble) & 0x0FU))) {
        return false;
    }

    switch (mode_nibble) {
    case 1U:
        *mode = PWRLVL_MODE_100_PERCENT;
        return true;

    case 2U:
        *mode = PWRLVL_MODE_75_PERCENT;
        return true;

    case 3U:
        *mode = PWRLVL_MODE_50_PERCENT;
        return true;

    default:
        return false;
    }
}

static bool nvm_write_mode(pwrlvl_mode_t mode)
{
    FLASH_EraseInitTypeDef erase_cfg = {0};
    HAL_StatusTypeDef status;
    uintptr_t address;
    uint32_t page_error = 0U;
    uint16_t value;
    uint8_t encoded;
    bool success = false;

    if (!nvm_encode_mode(mode, &encoded)) {
        return false;
    }

    value = (uint16_t)(((uint16_t)NVM_COMPLETION_MARKER << 8U) |
                       encoded);

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return false;
    }

    if (nvm_erase_required || (nvm_next_slot >= nvm_slot_count())) {
        erase_cfg.TypeErase = FLASH_TYPEERASE_PAGES;
        erase_cfg.PageAddress = (uint32_t)nvm_page_start();
        erase_cfg.NbPages = 1U;

        status = HAL_FLASHEx_Erase(&erase_cfg, &page_error);
        if ((status != HAL_OK) ||
            (page_error != 0xFFFFFFFFUL) ||
            !nvm_page_is_erased()) {
            (void)HAL_FLASH_Lock();
            return false;
        }

        nvm_next_slot = 0U;
        nvm_has_saved_mode = false;
        nvm_erase_required = false;
    }

    address = nvm_page_start() +
              ((uintptr_t)nvm_next_slot * NVM_SLOT_SIZE_BYTES);

    if (*(const volatile uint16_t *)address == NVM_ERASED_SLOT) {
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                   (uint32_t)address,
                                   value);
        success = (status == HAL_OK) &&
                  (*(const volatile uint16_t *)address == value);
    }

    (void)HAL_FLASH_Lock();

    if (success) {
        ++nvm_next_slot;
    }

    return success;
}
