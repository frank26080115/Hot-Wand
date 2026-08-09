#pragma once

#include "hotwand.h"
#include <stdbool.h>

#ifndef BTN_DEBOUNCE_MS
#define BTN_DEBOUNCE_MS 50
#endif

#if (BTN_DEBOUNCE_MS < 50)
#error "BTN_DEBOUNCE_MS must be at least 50 milliseconds"
#endif

#if (BTN_LONG_PRESS_MS < BTN_DEBOUNCE_MS)
#error "BTN_LONG_PRESS_MS must not be shorter than BTN_DEBOUNCE_MS"
#endif

typedef enum
{
    BTN_SHORT_PRESS_ON_PRESS = 0,
    BTN_SHORT_PRESS_ON_RELEASE
} btn_short_press_mode_t;

#ifdef __cplusplus
extern "C"
{
#endif

void btn_init(void);

/*
 * The default mode emits a short press on the falling edge. Release mode
 * emits it after a debounced release only

 * * when the hold was shorter than
 * BTN_LONG_PRESS_MS.
 */
void btn_set_short_press_mode(btn_short_press_mode_t mode);
bool btn_is_down(void);

/*
 * Passing true returns and clears the latched event atomically.  Passing false
 * only peeks at it.
 */
bool btn_has_short_press(bool clear_flag);
bool btn_has_long_press(bool clear_flag);

/*
 * Returns the number of short presses in the current sequence. A sequence
 * expires
 * BTN_CONSECUTIVE_PRESS_TIMEOUT_MS after its latest short press.
 */
uint32_t btn_get_consecutive_presses(void);
void     btn_reset_consecutive_presses(void);

void btn_task(void);

#ifdef __cplusplus
}
#endif
