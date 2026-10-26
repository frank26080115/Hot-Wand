/* The real fan state machine with peripheral commands captured in RAM.
 * Real output timing/polarity still needs the
 * separate electrical fixture. */
#include "../../src/fan.c"

static uint32_t now_ms, output_duty, init_calls, output_calls, topology;
static uint16_t temperatures[6];

__declspec(dllexport) void fan_reset(uint32_t mode, uint32_t inverted)
{
    now_ms = output_duty = init_calls = output_calls = 0;
    fan_output_initialized                           = false;
    for (uint32_t i = 0; i < 6; ++i)
    {
        temperatures[i] = 25;
    }
    fan_init(mode, inverted != 0);
}
__declspec(dllexport) void fan_step(uint32_t now, uint32_t temperature)
{
    now_ms                    = now;
    temperatures[THERM_1_IDX] = temperature;
    fan_task();
}
__declspec(dllexport) void fan_sensor(uint32_t channel, uint32_t temperature)
{
    temperatures[channel] = temperature;
}
__declspec(dllexport) void fan_control(uint32_t action)
{
    if (action == 0)
    {
        fan_stop();
    }
    if (action == 1)
    {
        fan_resume();
    }
    if (action == 2)
    {
        fan_on_wake();
    }
}
__declspec(dllexport) uint32_t fan_value(uint32_t selector)
{
    switch (selector)
    {
    case 0:
        return output_duty;
    case 1:
        return init_calls;
    case 2:
        return output_calls;
    case 3:
        return topology;
    case 4:
        return fan_mode;
    case 7:
        // Expose configuration, not the controller decision: Python still
        // independently checks both sides of the configured boundary.
        return FAN_TEMPERATURE_HYSTERESIS_C;
#if FAN_PWM_ENABLED
    case 5:
        return fan_requested_duty_percent;
    case 6:
        return fan_automatic_active;
#endif
    }
    return 0;
}
uint32_t systick_get_ms(void)
{
    return now_ms;
}
uint16_t adc_to_celcius(uint8_t channel)
{
    return temperatures[channel];
}
#if FAN_PWM_ENABLED
bool fanpwm_init(fanpwm_mode_t mode)
{
    ++init_calls;
    topology    = mode;
    output_duty = 0;
    return true;
}
void fanpwm_set(uint8_t duty)
{
    ++output_calls;
    output_duty = duty;
}
#endif
void show_short_msg(const char* text, uint32_t ms) {}

static uint16_t            virtual_temperature;
__declspec(dllexport) void fan_bucket(uint32_t temperature)
{
    virtual_temperature = temperature;
}
uint16_t leaky_bucket_get_temperature_c(void)
{
    return virtual_temperature;
}
