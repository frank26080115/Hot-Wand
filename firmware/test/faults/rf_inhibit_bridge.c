/* Run actual shared-build RF entry points with observable GPIO/timer mocks. */
#include "../../src/rfgen.c"

volatile fault_test_state_t fault_test;
RF_TIM_TypeDef              mock_tim1;
uint32_t                    mock_pa6_mode, mock_pa6_level, mock_pb1_mode, mock_pb1_level, mock_af_calls;

__declspec(dllexport) void rf_check(uint32_t bypass)
{
    /* A hostile previous timer state must be stopped, including by the
     * diagnostic entry point that normally bypasses the tip interlock. */
    mock_tim1.CR1  = 1;
    mock_tim1.CCER = TIM_CCER_CC3E | TIM_CCER_CC3NE;
    mock_tim1.BDTR = TIM_BDTR_MOE;
    mock_pb1_mode  = GPIO_MODE_AF_PP;
    mock_pb1_level = 1;
    mock_af_calls  = 0;
    if (bypass)
    {
        rfgen_start_with_tip_bypass_for_test();
    }
    else
    {
        rfgen_start();
    }
}
__declspec(dllexport) uint32_t rf_value(uint32_t selector)
{
    switch (selector)
    {
    case 0:
        return rfgen_is_active();
    case 1:
        return mock_pb1_mode;
    case 2:
        return mock_pb1_level;
    case 3:
        return mock_tim1.CR1 | mock_tim1.CCER | mock_tim1.BDTR;
    case 4:
        return mock_af_calls;
    default:
        return fault_test.rf_attempts;
    }
}
bool fault_test_inject(uint32_t point)
{
    return false;
}
bool tipdetect_is_present(void)
{
    return true;
}
bool tipdetect_is_qualifying(void)
{
    return false;
}
bool tipdetect_has_triggered(void)
{
    return false;
}
