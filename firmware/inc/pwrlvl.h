#pragma once

#include "hotwand.h"

/* Buck output sweep measured through BUCK_SENS_IDX on the STM32F042 board.
 * Idle voltage was set near 20 V with the adjustment pot. RF was off; each
 * PWM setting settled for 1 s before sampling (0% settled during the 5 s
 * startup delay). TIM3 has 32 counts per period. Values came from the
 * completed 33-row test_pwrlvl_sweep_rows table read through GDB.
 *
 * CCR1  Duty (%)  Buck output (mV)
 *    0         0             20033
 *    1         3             20014
 *    2         6             19979
 *    3         9             20014
 *    4        13             20014
 *    5        16             19979
 *    6        19             19979
 *    7        22             20014
 *    8        25             19979
 *    9        28             19783
 *   10        31             18982
 *   11        34             18091
 *   12        38             17094
 *   13        41             16097
 *   14        44             15100
 *   15        47             14141
 *   16        50             13106
 *   17        53             12073
 *   18        56             11141
 *   19        59             10106
 *   20        63              9046
 *   21        66              7977
 *   22        69              6909
 *   23        72              5841
 *   24        75              4737
 *   25        78              3642
 *   26        81              2564
 *   27        84              1781
 *   28        88              1282
 *   29        91               962
 *   30        94               783
 *   31        97               677
 *   32       100               641
 */

typedef enum
{
    PWRLVL_MODE_100_PERCENT = 0,
    PWRLVL_MODE_75_PERCENT,
    PWRLVL_MODE_50_PERCENT,
} pwrlvl_mode_t;

#ifdef __cplusplus
extern "C"
{
#endif

void pwrlvl_init(void);
void pwrlvl_task(void);
#if PWRLVL_POWER_RISE_ENABLED
/* One filtered-power observation per foreground supervision pass. The history
 * reference is fixed for this graph segment; false excludes unwritten slots. */
void pwrlvl_observe_power(uint32_t current_mw, uint32_t previous_peak_mw, bool segment_started, bool reference_valid);
#endif
void pwrlvl_set_mode(pwrlvl_mode_t mode);
bool pwrlvl_is_current_limiting(void);
void pwrlvl_force_minimum(void);
/* Releases a forced minimum and resumes regulation in the selected mode.
 * The shared automated-test build releases only the logical override; its
 * physical attenuation output remains GPIO-high / minimum buck voltage. */
void pwrlvl_release_minimum(void);

#ifdef __cplusplus
}
#endif
