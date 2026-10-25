#include "../../src/rfgen.c"
RF_TIM_TypeDef  mock_tim1;
uint32_t        mock_pa6_mode, mock_pa6_level, mock_pb1_mode, mock_pb1_level, mock_af_calls;
static bool     present, qualifying, triggered;
static uint32_t rf_now;
#ifndef RF_BRIDGE_EXTERNAL_CLOCK
uint32_t systick_get_ms(void)
{
    return rf_now;
}
#endif
__declspec(dllexport) uint32_t rf_step(uint32_t now, uint32_t mode)
{
    rf_now = now;
    rfgen_task((pwrlvl_mode_t)mode);
    return rfgen_is_active();
}
__declspec(dllexport) void rf_interlocks(uint32_t flags)
{
    present                       = (flags & 1) == 0;
    qualifying                    = (flags & 2) != 0;
    triggered                     = (flags & 4) != 0;
    rfgen_clock_fault_logged      = (flags & 8) != 0;
    rfgen_emergency_stop_latched  = (flags & 16) != 0;
    rfgen_power_loss_stop_latched = (flags & 32) != 0;
}
__declspec(dllexport) uint32_t rf_handoffs(void)
{
    return mock_af_calls;
}
__declspec(dllexport) void rf_reset(uint32_t input_present, uint32_t input_qualifying)
{
    rfgen_clock_fault_logged = rfgen_emergency_stop_latched = rfgen_power_loss_stop_latched = false;
    rfgen_active                                                                            = false;
    rf_now                                                                                  = 0;
    mock_af_calls                                                                           = 0;
    present                                                                                 = input_present != 0;
    qualifying                                                                              = input_qualifying != 0;
    triggered                                                                               = false;
    rfgen_stop();
}
__declspec(dllexport) uint32_t rf_run(uint32_t action)
{
    if (action == 0)
    {
        rfgen_start();
    }
    if (action == 1)
    {
        rfgen_stop();
    }
    if (action == 2)
    {
        rfgen_emergency_stop();
    }
    return rfgen_is_active();
}
__declspec(dllexport) uint32_t rf_enabled(void)
{
    return ((TIM1->CR1 & TIM_CR1_CEN) != 0) || ((TIM1->CCER & (TIM_CCER_CC3E | TIM_CCER_CC3NE)) != 0) ||
           ((TIM1->BDTR & TIM_BDTR_MOE) != 0);
}
__declspec(dllexport) uint32_t rf_bits(void)
{
    return mock_tim1.CR1 | mock_tim1.CCER | mock_tim1.BDTR;
}
bool tipdetect_is_present(void)
{
    return present;
}
bool tipdetect_is_qualifying(void)
{
    return qualifying;
}
bool tipdetect_has_triggered(void)
{
    return triggered;
}

__declspec(dllexport) uint32_t rf_rearm_case(uint32_t flags)
{
    rfgen_active                  = flags & 1;
    rfgen_clock_fault_logged      = flags & 2;
    rfgen_emergency_stop_latched  = flags & 4;
    rfgen_power_loss_stop_latched = flags & 8;
    return rfgen_rearm_power_loss();
}
__declspec(dllexport) uint32_t rf_latches(void)
{
    return rfgen_active | (rfgen_clock_fault_logged << 1) | (rfgen_emergency_stop_latched << 2) |
           (rfgen_power_loss_stop_latched << 3);
}
