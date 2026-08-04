#pragma once

#include "typedefs.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void nvm_init(void);

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

#ifdef __cplusplus
}
#endif
