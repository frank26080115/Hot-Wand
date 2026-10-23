#include "../../src/rfgen.c"
RF_TIM_TypeDef             mock_tim1;
uint32_t                   mock_pa6_mode, mock_pa6_level, mock_pb1_mode, mock_pb1_level, mock_af_calls;
static bool                present, qualifying, triggered;
__declspec(dllexport) void rf_reset(uint32_t input_present, uint32_t input_qualifying)
{
    rfgen_clock_fault_logged = rfgen_emergency_stop_latched = rfgen_power_loss_stop_latched = false;
    rfgen_active                                                                            = false;
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
