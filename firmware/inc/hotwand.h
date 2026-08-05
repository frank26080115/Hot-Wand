#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "conf.h"
#include "defs.h"
#include "typedefs.h"
#include "pins.h"
#include "miscutils.h"
#include "fault.h"

void enter_sleep_mode(void);
void show_fault(const char *text, bool allow_button_reset);
