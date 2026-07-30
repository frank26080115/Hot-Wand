#ifndef HOT_WAND_SYSTICK_H
#define HOT_WAND_SYSTICK_H

#include <stdint.h>

void systick_init(void);
uint32_t systick_get_ms(void);

#endif
