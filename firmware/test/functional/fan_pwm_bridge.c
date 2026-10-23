#include "../../src/fan_pwm.c"
FunctionalTimer mock_tim16, mock_tim17;

__declspec(dllexport) uint32_t pwm_init(uint32_t mode)
{
    return fanpwm_init(mode);
}
__declspec(dllexport) void pwm_set(uint32_t duty)
{
    fanpwm_set(duty);
}
__declspec(dllexport) uint32_t pwm_value(uint32_t selector)
{
    switch (selector)
    {
    case 0:
        return mock_tim17.CCR1;
    case 1:
        return mock_tim17.ARR;
    case 2:
        return mock_tim17.CR1;
    case 3:
        return mock_tim17.BDTR;
    case 4:
        return mock_tim16.CR1;
    }
    return 0;
}
