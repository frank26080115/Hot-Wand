#pragma once

#include "hotwand.h"
#include "typedefs.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void nvm_init(void);

/* Replaces *settings with the explicit factory-default configuration. */
void nvm_apply_defaults(hotwand_setup_nvm_t *settings);

/*
 * Returns true and writes the latest complete, valid journal entry to
 * *settings.  Returns false without modifying *settings when no valid entry
 * has been saved.
 */
bool nvm_read(hotwand_setup_nvm_t *settings);

/*
 * Immediately appends settings to the flash journal.  The magic, reserved,
 * and checksum fields are owned by this module and need not be initialized by
 * the caller.  Returns false for invalid settings or a flash operation error.
 */
bool nvm_save(const hotwand_setup_nvm_t *settings);

/* Erases the complete reserved settings page.  Returns false on a layout or
 * flash operation error; after success, nvm_read() reports no saved data. */
bool nvm_factory_reset(void);

#ifdef __cplusplus
}
#endif
