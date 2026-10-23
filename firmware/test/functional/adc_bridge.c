/* Existing actual ISR/filter fixture plus normal conversion entry points. */
#include "../adc_sim/bridge.c"
__declspec(dllexport) void adc_normal_values(uint32_t channel, uint32_t sample)
{
    result[channel] = sample;
    initialized_channels |= 1 << channel;
}
__declspec(dllexport) uint32_t adc_normal_convert(uint32_t kind, uint32_t channel)
{
    if (kind == 0)
    {
        return adc_to_millivolts(channel);
    }
    if (kind == 1)
    {
        return adc_to_milliamps(channel);
    }
    if (kind == 2)
    {
        return adc_to_celcius(channel);
    }
    return adc_get_milliwatts();
}
__declspec(dllexport) void adc_normal_calibration(uint32_t selection)
{
    adc_set_input_voltage_calibration(selection);
}
