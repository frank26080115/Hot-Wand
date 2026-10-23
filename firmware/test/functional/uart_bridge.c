#include "../../src/uart_debug.c"
#include "../../src/miscutils.c"
static uint32_t now_ms, init_calls, reference_valid, selected_power;
static char     output[512];
static uint32_t output_length;

__declspec(dllexport) void uart_reset(void)
{
    now_ms = init_calls = output_length = 0;
    uart_allowed = uart_initialized = false;
    uart_debug_last_task_ms         = 0;
    reference_valid                 = 1;
    selected_power                  = 0;
    output[0]                       = 0;
}
__declspec(dllexport) void uart_step(uint32_t now, uint32_t allowed)
{
    now_ms = now;
    UART_SetAllowed(allowed != 0);
    UART_debug_task();
}
__declspec(dllexport) void uart_options(uint32_t mode, uint32_t valid)
{
    selected_power  = mode;
    reference_valid = valid;
    output_length   = 0;
    output[0]       = 0;
}
__declspec(dllexport) const char* uart_text(void)
{
    return output;
}
__declspec(dllexport) uint32_t uart_count(void)
{
    return init_calls;
}
uint32_t systick_get_ms(void)
{
    return now_ms;
}
uint16_t adc_to_millivolts(uint8_t channel)
{
    return channel == DC_SENS_IDX ? 24000 : 20000;
}
uint16_t adc_to_milliamps(uint8_t channel)
{
    return 1500;
}
uint16_t adc_to_celcius(uint8_t channel)
{
    return 30;
}
bool adc_get_reference_millivolts(uint16_t* value)
{
    *value = 3300;
    return reference_valid != 0;
}
pwrlvl_mode_t pwrmgt_get_applied_power_level(void)
{
    return selected_power;
}
uint8_t pwrmgt_get_attenuation_reasons(void)
{
    return 5;
}
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef* handle)
{
    ++init_calls;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef* handle, const uint8_t* bytes, uint16_t length, uint32_t timeout)
{
    for (uint32_t i = 0; i < length && output_length < sizeof(output) - 1; ++i)
    {
        output[output_length++] = bytes[i];
    }
    output[output_length] = 0;
    return HAL_OK;
}
