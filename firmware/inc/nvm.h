#ifndef HOT_WAND_NVM_H
#define HOT_WAND_NVM_H

#include "pwrlvl.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void nvm_init(void);

/*
 * Returns true and writes the last valid saved mode to *mode.
 * Returns false, without modifying *mode, when no valid mode has been saved.
 */
bool nvm_read(pwrlvl_mode_t *mode);

/*
 * Queues a valid mode for saving.  The five-second delay is restarted when
 * the queued mode changes, allowing rapid mode changes to be coalesced.
 */
void nvm_save(pwrlvl_mode_t mode);

/* Call once per main-loop iteration to service a pending delayed save. */
void nvm_task(void);

#ifdef __cplusplus
}
#endif

#endif
