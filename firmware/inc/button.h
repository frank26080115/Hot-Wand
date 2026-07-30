#ifndef HOT_WAND_BUTTON_H
#define HOT_WAND_BUTTON_H

#include <stdbool.h>

#ifndef BTN_DEBOUNCE_MS
#define BTN_DEBOUNCE_MS 50UL
#endif

#ifndef BTN_LONG_PRESS_MS
#define BTN_LONG_PRESS_MS 1000UL
#endif

#if (BTN_DEBOUNCE_MS < 50UL)
#error "BTN_DEBOUNCE_MS must be at least 50 milliseconds"
#endif

#if (BTN_LONG_PRESS_MS < BTN_DEBOUNCE_MS)
#error "BTN_LONG_PRESS_MS must not be shorter than BTN_DEBOUNCE_MS"
#endif

#ifdef __cplusplus
extern "C" {
#endif

void btn_init(void);
bool btn_is_down(void);

/*
 * Passing true returns and clears the latched event atomically.  Passing false
 * only peeks at it.
 */
bool btn_has_short_press(bool clear_flag);
bool btn_has_long_press(bool clear_flag);

void btn_task(void);

#ifdef __cplusplus
}
#endif

#endif
