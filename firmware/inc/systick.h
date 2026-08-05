#pragma once

#include "hotwand.h"
#include <stdint.h>

void     systick_init(void);
uint32_t systick_get_ms(void);
