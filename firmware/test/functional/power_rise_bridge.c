/* Actual graph-history, spike controller, and existing buck regulator. */
#if HOT_WAND_TEST_HARNESS
#include "../faults/power_bridge.c"
#else
#include "power_bridge.c"
#endif

__declspec(dllexport) void
rise_observe(uint32_t now, uint32_t power, uint32_t previous, uint32_t segment, uint32_t valid)
{
    mock_now = now;
#if PWRLVL_POWER_RISE_ENABLED
    pwrlvl_observe_power(power, previous, segment != 0, valid != 0);
#endif
    pwrlvl_task();
}
__declspec(dllexport) void rise_pending(uint32_t power, uint32_t previous)
{
#if PWRLVL_POWER_RISE_ENABLED
    pwrlvl_observe_power(power, previous, false, true);
#endif
}
__declspec(dllexport) void rise_mode(uint32_t mode)
{
    pwrlvl_set_mode((pwrlvl_mode_t)mode);
}
__declspec(dllexport) uint32_t rise_value(uint32_t index)
{
    switch (index)
    {
    case 1:
        return pwrlvl_regulator_value;
    case 2:
        return pwrlvl_pwm_value;
#if PWRLVL_POWER_RISE_ENABLED
    case 3:
        return pwrlvl_rise_peak_mw;
    case 4:
        return pwrlvl_rise_pending;
    case 5:
        return pwrmgt_history_reference_valid;
    case 6:
        return pwrmgt_history_peek(1);
    case 7:
        return pwrlvl_last_update_ms;
#endif
    case 8:
        return PWRLVL_PWM_JUMP_LEVEL;
    case 9:
        return PWRLVL_RAMP_UP_STEP;
    case 10:
        return TEMPERATURE_HOT_WARNING_THRESH_C + 1;
    case 11:
        return DC_HIGH_POWER_MINIMUM_MV - 1;
    case 12:
        return TIM3->CCR1;
    default:
        return 0;
    }
}
