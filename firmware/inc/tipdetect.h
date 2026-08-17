#pragma once

#include "hotwand.h"
#include <stdbool.h>

/* Time to wait after a TIP_DET edge before sampling the new pin level. The
 * legacy PSC = 0, ARR = 8135 setup at 27.12 MHz was exactly 300 us. Override
 * this with a compiler definition if a different debounce is needed. */
#ifndef TIPDETECT_DEBOUNCE_US
#define TIPDETECT_DEBOUNCE_US 300
#endif

#if (TIPDETECT_DEBOUNCE_US < 1) || (TIPDETECT_DEBOUNCE_US > 1000000)
#error "TIPDETECT_DEBOUNCE_US must be between 1 and 1000000 microseconds"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

void tipdetect_init(void);
void tipdetect_task(void);

/* Returns the latched disconnect state; once true, it requires a reset. */
bool tipdetect_has_triggered(void);

/* Ignored unless the tip is present and no debounce is in progress. */
void tipdetect_reset(void);

#ifdef __cplusplus
}
#endif
