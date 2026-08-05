#pragma once

#include "hotwand.h"

#if SETUP_MENU_TIMEOUT_MS == 0
#error "SETUP_MENU_TIMEOUT_MS must be greater than zero"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * Runs the blocking setup menu.  The function exits only by resetting the
 * MCU after either of its explicit
 * exit choices.  Inactivity enters sleep
 * without saving.
 */
void setup_menu(void);

#ifdef __cplusplus
}
#endif
