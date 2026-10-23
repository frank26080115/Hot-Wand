/* Compact observation/injection layer for the shared automated-test image.
 * It records real detection paths; host expectations live in faults_cases.py. */
// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------
#include "fault_test.h"
#if HOT_WAND_TEST_HARNESS
#include "adc.h"
#include "adc_sim.h"
#include "rfgen.h"
#include "oled.h"
#include "systick.h"
#include "stm32f0xx_hal.h"

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
volatile fault_test_state_t fault_test __attribute__((used, externally_visible));
_Static_assert(sizeof(fault_test_state_t) == 128, "fault test ABI size changed");

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------
void fault_test_task(void)
{
    if (fault_test_inject(FT_HANG))
    {
        for (;;)
        { /* No foreground feed; leave interrupts running. */
        }
    }
    if (fault_test_inject(FT_HARDFAULT))
    {
        __asm__ volatile(".short 0xde00"); /* Undefined Thumb instruction: real exception entry. */
    }
    if (fault_test_inject(FT_CLOCK))
    {
        HAL_RCC_CSSCallback(); /* Callback containment only, not a crystal failure. */
    }
    if (fault_test_inject(FT_TIP_STORM))
    {
        fault_test.reserved[0] = 12;
    }
}

void fault_test_snapshot(void)
{
    uint8_t  idx;
    uint16_t reference = 0;
    fault_test.flags |= 128;
    for (idx = 0; idx < 6; ++idx)
    {
        fault_test.filtered[idx] = adc_get(idx);
    }
    if ((fault_test.event_raw >> 16) != 3)
    {
        adc_copy_dc_voltage_history((uint16_t*)fault_test.history, 7);
    }
    fault_test.dc_mv      = adc_to_millivolts(DC_SENS_IDX);
    fault_test.current_ma = adc_to_milliamps(CURR_SENS_IDX);
    adc_get_reference_millivolts(&reference);
    fault_test.reference_mv = reference;
    fault_test.tick         = systick_get_ms();
    fault_test.flags = (fault_test.flags & 101) | (rfgen_is_active() ? 2 : 0) |
                       (adc_has_buck_voltage_spike_shutdown() ? 8 : 0) | (adc_has_power_loss_shutdown() ? 16 : 0);
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------
void fault_test_fault(const char* message, bool allow_reset)
{
    if (fault_test.stage != FT_FAULT)
    {
        fault_test_snapshot();
        fault_test.trip_ms         = systick_get_ms();
        fault_test.trip_round      = adc_get_completed_round_count();
        fault_test.trip_generation = vision_test_oled_sent_generation;
        fault_test.rf_at_fault     = fault_test.rf_attempts;
        fault_test.flags           = (fault_test.flags & ~1) | (allow_reset ? 1 : 0);
        fault_test.message         = (uint32_t)message;
        fault_test.stage           = FT_FAULT; /* Publish completed first-diagnosis snapshot last. */
    }
}

void fault_test_raw(uint16_t sample, uint8_t kind)
{
    uint8_t idx;
    if (fault_test.event_raw == 0)
    {
        for (idx = 0; idx < 7; ++idx)
        {
            fault_test.raw[idx] = adc_sim.decoded[idx];
        }
        fault_test.event_round = adc_get_completed_round_count();
        fault_test.event_raw   = ((uint32_t)kind << 16) | sample;
        fault_test.event_ms    = systick_get_ms();
    }
}

// -----------------------------------------------------------------------------
// Small Helpers
// -----------------------------------------------------------------------------
bool fault_test_inject(uint32_t point)
{
    if (fault_test.command != point)
    {
        return false;
    }
    fault_test.command = 0;
    fault_test.ack     = point;
    return true;
}

void fault_test_init(void)
{
    fault_test.reset_flags = RCC->CSR;
    __HAL_RCC_CLEAR_RESET_FLAGS();
    fault_test.stage = FT_BOOT;
    fault_test.flags |= 64; /* Physical RF/buck outputs are inhibited for all test cases. */
    fault_test.abi = 2;
}
#endif
