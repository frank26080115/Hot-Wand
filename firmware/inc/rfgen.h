#pragma once

#include "hotwand.h"
#include "pwrlvl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

bool rfgen_clock_init(void);
bool rfgen_has_fault(void);
bool rfgen_has_clock_fault(void);
bool rfgen_is_active(void);
/* Foreground runtime gate: applied mode selects a system-clock RF phase.
 * Legacy wattage-regulation builds allow continuous RF instead. Direct start
 * APIs below deliberately remain available for standalone bring-up tests. */
void rfgen_task(pwrlvl_mode_t mode);
/* Shared automated-test firmware inhibits physical starts through both APIs. */
void rfgen_start(void);
/* Bring-up-only entry point: retains clock and emergency-stop checks, but
 * deliberately ignores the external tip-presence interlock. */
void rfgen_start_with_tip_bypass_for_test(void);
void rfgen_stop(void);
/* Immediately disables TIM1 and latches RF restart inhibition until reset. */
void rfgen_emergency_stop(void);
/* Same immediate shutdown, with a selective input-fault restart latch shared
 * by power-loss and undervoltage recovery. */
void rfgen_power_loss_stop(void);
/* Clears only the power-loss latch while RF is off and no emergency/clock
 * fault is latched. Does not start RF. */
bool rfgen_rearm_power_loss(void);
/* CT diagnostic only: clears just the power-loss latch while RF is off.
 * Clock faults and other emergency stops still require a reset. */
bool rfgen_rearm_power_loss_for_test(void);
bool rfgen_tip_allows_start(void);

#ifdef __cplusplus
}
#endif
