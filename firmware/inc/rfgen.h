#pragma once

#include "hotwand.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

bool rfgen_clock_init(void);
bool rfgen_has_fault(void);
bool rfgen_has_clock_fault(void);
bool rfgen_is_active(void);
void rfgen_start(void);
/* Bring-up-only entry point: retains clock and emergency-stop checks, but
 * deliberately ignores the external tip-presence interlock. */
void rfgen_start_with_tip_bypass_for_test(void);
void rfgen_stop(void);
/* Immediately disables TIM1 and latches RF restart inhibition until reset. */
void rfgen_emergency_stop(void);
/* Same immediate shutdown, with a separate power-loss restart latch. */
void rfgen_power_loss_stop(void);
/* CT diagnostic only: clears just the power-loss latch while RF is off.
 * Clock faults and other emergency stops still require a reset. */
bool rfgen_rearm_power_loss_for_test(void);
bool rfgen_tip_allows_start(void);

#ifdef __cplusplus
}
#endif
