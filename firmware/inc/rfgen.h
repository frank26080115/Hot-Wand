#pragma once

#include "hotwand.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

bool rfgen_clock_init(void);
bool rfgen_has_fault(void);
bool rfgen_is_active(void);
void rfgen_start(void);
void rfgen_stop(void);
bool rfgen_tip_allows_start(void);

#ifdef __cplusplus
}
#endif
