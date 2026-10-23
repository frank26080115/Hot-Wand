#pragma once

#include "hotwand.h"
#include <stdbool.h>

#ifndef TIP_SWD_TEST_HARNESS
#define TIP_SWD_TEST_HARNESS 0
#endif

#if (TIP_SWD_TEST_HARNESS != 0) && (TIP_SWD_TEST_HARNESS != 1)
#error "TIP_SWD_TEST_HARNESS must be 0 or 1"
#endif

/* Time to wait after a TIP_DET edge before sampling the new pin level. The
 * legacy PSC = 0, ARR = 8135 setup at 27.12 MHz was exactly 300 us. Override
 * this with a compiler definition if a different debounce is needed. */
#ifndef TIPDETECT_DEBOUNCE_US
#define TIPDETECT_DEBOUNCE_US 300
#endif

#if (TIPDETECT_DEBOUNCE_US < 1) || (TIPDETECT_DEBOUNCE_US > 1000000)
#error "TIPDETECT_DEBOUNCE_US must be between 1 and 1000000 microseconds"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

void tipdetect_init(void);
void tipdetect_task(void);

/* Instantaneous selected input, before debounce: PA5 in production, the
 * virtual SWD level in a tip-harness build. Does not clear a fault latch. */
bool tipdetect_is_present(void);

/* Returns the latched disconnect state; once true, it requires a reset. */
bool tipdetect_has_triggered(void);

/* Returns true while an input edge is undergoing timed qualification. */
bool tipdetect_is_qualifying(void);

/* Ignored unless the tip is present and no debounce is in progress. */
void tipdetect_reset(void);

#if TIP_SWD_TEST_HARNESS
#define TIP_SWD_NO_COMMAND UINT32_MAX
enum
{
    TIP_SWD_ABSENT     = 0,
    TIP_SWD_PRESENT    = 1,
    TIP_SWD_QUALIFYING = 2,
    TIP_SWD_NOT_READY  = 3,
};

/* SWD: after init, the virtual tip starts PRESENT regardless of PA5. Write
 * command = 0 to remove it, or 1 to insert it, only when state is PRESENT or
 * ABSENT and command is UINT32_MAX. tipdetect_task() consumes the command and
 * raises a software EXTI; normal TIM14 debounce (300 us by default) samples it.
 * Physical PA5 edges are ignored in this opt-in build. Same-level requests
 * complete immediately without an edge. Invalid commands are consumed without
 * changing state or completed. completed counts accepted, finished requests.
 *
 * Read state/completed to observe qualification. Reinsertion never clears a
 * latched removal fault: normal operation stays on TIP FAULT until MCU reset,
 * and this mailbox is not serviced inside a terminal fault screen. */
extern volatile uint32_t tip_swd_test_command;
extern volatile uint32_t tip_swd_test_state;
extern volatile uint32_t tip_swd_test_completed;
#endif

#ifdef __cplusplus
}
#endif
