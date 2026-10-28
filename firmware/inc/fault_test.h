#pragma once

#include "conf.h"
#include <stdbool.h>
#include <stdint.h>

/* Shared stm32f042_test ABI. All commands are single-shot and acknowledged only
 * at their named injection point, before the real operation's side effects.
 * Commands and packed setup are written at the ADC boot gate, command last.
 * setup: cells[7:0] (zero = real guess), mode[10:8], power[12:11],
 * calibration[16:13], auto-sleep[18:17], auto-dim[20:19], absent tip[21],
 * auto-dim bits are ignored when OLED_DIMMING_ENABLED=0.
 * optional idle profile[24:22], idle override valid[25],
 * RAM settings override valid[31]. No NVM writes are needed for ordinary cases. */
enum
{
    FT_HANG         = 1,
    FT_HARDFAULT    = 2,
    FT_CLOCK        = 3,
    FT_TIP_STORM    = 4,
    FT_MARK         = 5,
    FT_OSC_FAIL     = 10,
    FT_CLOCK_SWITCH = 11,
    FT_CLOCK_READY  = 12,
    FT_ADC_INIT     = 20,
    FT_ADC_CAL      = 21,
    FT_ADC_CHANNEL  = 22,
    FT_ADC_VREF     = 23,
    FT_ADC_START    = 24,
    FT_OLED_INIT    = 30,
    FT_GRAPHICS     = 31,
    FT_TIP_BASE     = 32,
    FT_TIP_PULSE    = 33,
    FT_NVM_SAVE     = 40,
};
enum
{
    FT_BOOT     = 1,
    FT_ADC      = 2,
    FT_OLED     = 3,
    FT_SETUP    = 4,
    FT_RUNTIME  = 5,
    FT_FAULT    = 6,
    FT_BATTERY  = 7,
    FT_RESUMING = 8 /* Outputs remain stopped until normal supervision passes. */
};

#if HOT_WAND_TEST_HARNESS
/* 128 bytes, aligned words; use offsets/version validation in fault_target.py.
 * flags: reset allowed(0), RF active(1), fan stopped(2), raw spike(3), raw loss(4),
 * warning visible(5), physical outputs inhibited(6), snapshot busy(7). Snapshot arrays freeze on a terminal
 * diagnosis. event_* preserves the first raw-ISR trigger, ahead of filtering.
 * ABI 2: pwm is the requested logical attenuation; PA6 remains GPIO-high,
 * and RF starts are physically inhibited even when the test requests them.
 * Tick/live fields are read only with busy clear in one brief debugger halt.
 * qualify_ms is target milliseconds for thermal/current, or the activation
 * round recorded by FT_MARK. event_raw packs kind[31:16] and count[15:0]:
 * 1 = spike, 2 = fast loss, 3 = sustained history (no raw ISR frame).
 * History is newest-first. raw[] freezes the seven decoded values on the
 * first ISR trigger; it remains zero for a foreground-only history trigger.
 * reserved[0] is the target-paced tip burst count; reserved[1] is the high-input overlay: active bit 1, visible bit 0.
 */
typedef struct
{
    uint32_t abi, command, ack, stage, reset_flags, message, tick;
    uint32_t trip_ms, trip_round, trip_generation, rf_attempts, rf_at_fault, qualify_ms, setup;
    uint16_t filtered[6], history[7], raw[7];
    uint32_t energy_uj;
    uint16_t dc_mv, current_ma, temperature_c, reference_mv;
    uint8_t  cells, mode, power, reasons, pwm, flags, reserved[2];
    uint32_t event_ms, event_round, event_raw;
} fault_test_state_t;
extern volatile fault_test_state_t fault_test;
extern const uint32_t              fault_test_dimming_enabled;
extern const uint32_t              fault_test_power_rise_enabled;
extern volatile uint32_t           fault_test_inactive_ms;
void                               fault_test_init(void);
void                               fault_test_task(void);
void                               fault_test_snapshot(void);
void                               fault_test_fault(const char* message, bool allow_reset);
void                               fault_test_raw(uint16_t sample, uint8_t kind);
bool                               fault_test_inject(uint32_t point);
#define FT_SET(member, value) (fault_test.member = (value))
#else
#define fault_test_init()
#define fault_test_task()
#define fault_test_snapshot()
#define fault_test_fault(message, allow_reset)
#define fault_test_raw(sample, kind)
#define fault_test_inject(point) false
#define FT_SET(member, value)
#endif
