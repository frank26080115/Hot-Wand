#pragma once

#include "hotwand.h"

#ifdef __cplusplus
extern "C"
{
#endif

bool rfgen_clock_init(void);
bool rfgen_has_fault(void);
void rfgen_start(void);
void rfgen_stop(void);

#ifdef __cplusplus
}
#endif
