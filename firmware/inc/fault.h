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
/* Same stopped-output screen; returns after qualified input recovery, with
 * buck/fan still stopped so normal supervision can authorize their release. */
void show_power_loss_fault(void);
/* Same recovery qualification, retaining the LOW / VOLT / FAULT message. */
void show_undervoltage_fault(void);
/* Copies up to 71 characters; a null/empty message or zero duration cancels. */
void show_short_msg(const char* text, uint32_t duration_ms);
/* Returns true while a cached short message owns the display. */
bool short_msg_task(void);
/* Draws the voltage and newline-delimited five-character fault lines. */
void fault_render(u8g2_t* graphics, const char* text, uint8_t x_offset, int16_t y_offset);

#ifdef __cplusplus
}
#endif
