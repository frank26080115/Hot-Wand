/* Actual power supervision, buck regulation, and RF generator in one fixture.
 * Only UI, sensor inputs, and hardware registers are mocked. */
#define NATIVE_RF_INTEGRATION 1
#include "../faults/power_bridge.c"
#define RF_BRIDGE_EXTERNAL_CLOCK 1
#include "rf_bridge.c"

__declspec(dllexport) void integrated_reset(uint32_t now, uint32_t mode)
{
    bridge_reset(now);
    rf_reset(1, 0);
    pwrmgt_set_desired_power_level((pwrlvl_mode_t)mode);
}
__declspec(dllexport) uint32_t integrated_step(uint32_t now)
{
    mock_now = now;
    // Contain the non-returning fault mock so terminal supervision cannot
    // accidentally fall through to the RF restart path in native tests.
    mock_abort_terminal = mock_guard_active = true;
    if (__builtin_setjmp(mock_terminal_context) == 0)
    {
        pwrmgt_task();
        rfgen_task(pwrmgt_get_applied_power_level());
    }
    mock_guard_active = false;
    return rfgen_is_active();
}

// Inspect eligibility after the real supervision/RF restart ordering.
__declspec(dllexport) uint32_t integrated_activity_guard(void)
{
    return pwrmgt_activity_on_guard_segments;
}
