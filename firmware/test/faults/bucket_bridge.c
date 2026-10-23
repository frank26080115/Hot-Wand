/* The model remains real firmware C; only its clock and measured power vary. */
#include "../../src/leaky_bucket.c"
static uint32_t mock_now, mock_power;

__declspec(dllexport) void bucket_reset(uint32_t now, uint64_t energy)
{
    mock_now                      = now;
    leaky_bucket_energy_uj        = energy;
    leaky_bucket_time_initialized = false;
    leaky_bucket_temperature_c    = 25;
    leaky_bucket_task();
}
__declspec(dllexport) uint32_t bucket_step(uint32_t now, uint32_t power)
{
    mock_now   = now;
    mock_power = power;
    leaky_bucket_task();
    return leaky_bucket_get_temperature_c();
}
__declspec(dllexport) uint64_t bucket_energy(void)
{
    return leaky_bucket_energy_uj;
}
uint32_t systick_get_ms(void)
{
    return mock_now;
}
uint32_t adc_get_milliwatts(void)
{
    return mock_power;
}
