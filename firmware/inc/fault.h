#pragma once

#include "hotwand.h"

#include "u8g2.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

void show_fault(const char* text, bool allow_button_reset);
/* Copies up to 71 characters; a null/empty message or zero duration cancels. */
void show_short_msg(const char* text, uint32_t duration_ms);
/* Returns true while a cached short message owns the display. */
bool short_msg_task(void);
/* Draws the voltage and newline-delimited five-character fault lines. */
void fault_render(u8g2_t* graphics, const char* text, uint8_t x_offset, int16_t y_offset);

#ifdef __cplusplus
}
#endif
