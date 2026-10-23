/* Actual supervision and buck-control C with engineering-value ADC inputs.
 * Mocked UI/peripherals are containment boundaries, not electrical evidence. */
#include "../../src/pwrmgt.c"
#include "../../src/pwrlvl.c"

#if HOT_WAND_TEST_HARNESS
volatile fault_test_state_t fault_test;
uint32_t                    mock_pa6_mode, mock_pa6_level, mock_pb1_mode, mock_pb1_level, mock_af_calls;
uint32_t                    adc_get_completed_round_count(void)
{
    return 0;
}
#endif
TIM_TypeDef        mock_tim3;
static uint32_t    mock_now, mock_power;
static uint16_t    mock_dc, mock_current, mock_temperature, mock_bucket, mock_reference;
static bool        mock_spike, mock_loss, mock_battery_ok, mock_allowed;
static const char* mock_fault;
const uint8_t      u8g2_font_6x10_tr[1] = {0};

__declspec(dllexport) void bridge_reset(uint32_t now)
{
#if HOT_WAND_TEST_HARNESS
    mock_pa6_mode = mock_pa6_level = mock_af_calls = 0;
#endif
    mock_now     = now;
    mock_dc      = 24000;
    mock_current = mock_power = 0;
    mock_temperature = mock_bucket = 25;
    mock_reference                 = 3300;
    mock_spike = mock_loss = mock_allowed = false;
    mock_battery_ok                       = true;
    mock_fault                            = 0;
    pwrlvl_initialized = pwrlvl_forced_minimum = false;
    pwrlvl_mode                                = PWRLVL_MODE_100_PERCENT;
    pwrmgt_desired_power_level                 = PWRLVL_MODE_100_PERCENT;
    pwrmgt_temperature_limited = pwrmgt_temperature_shutdown_pending = pwrmgt_low_dc_limited = false;
    pwrmgt_pending_blocked_message                                                           = 0;
    pwrmgt_blocked_release_pending                                                           = false;
    pwrlvl_init();
}

/* One call observes one real task pass. The fault mock returns, so tests stop
 * after the first captured diagnosis rather than pretending execution blocks. */
__declspec(dllexport) void bridge_step(uint32_t now, uint32_t full)
{
    mock_now = now;
    if (full)
    {
        pwrmgt_task();
    }
    else
    {
        pwrlvl_task();
    }
}

__declspec(dllexport) void bridge_input(uint32_t selector, uint32_t value)
{
    switch (selector)
    {
    case 0:
        mock_current = value;
        break;
    case 1:
        mock_temperature = value;
        break;
    case 2:
        mock_dc = value;
        break;
    case 3:
        mock_reference = value;
        break;
    case 4:
        mock_power = value;
        break;
    case 5:
        pwrlvl_set_mode(value);
        break;
    case 6:
        mock_spike = value;
        break;
    case 7:
        mock_loss = value;
        break;
    case 8:
        mock_battery_ok = value;
        break;
    case 9:
        if (value)
        {
            pwrlvl_force_minimum();
        }
        else
        {
            pwrlvl_release_minimum();
        }
        break;
    case 10:
        mock_bucket = value;
        break;
    }
}

__declspec(dllexport) uint32_t bridge_value(uint32_t selector)
{
    switch (selector)
    {
    case 0:
        return pwrlvl_pwm_value;
    case 1:
        return pwrlvl_is_current_limiting();
    case 2:
        return pwrmgt_attenuation_reasons;
    case 3:
        return pwrmgt_applied_power_level;
    case 4:
        return mock_allowed;
#if HOT_WAND_TEST_HARNESS
    case 5:
        return mock_pa6_mode;
    case 6:
        return mock_pa6_level;
    case 7:
        return mock_af_calls;
    case 8:
        return mock_tim3.CR1;
#endif
    default:
        return 0;
    }
}
__declspec(dllexport) const char* bridge_fault(void)
{
    return mock_fault;
}

uint32_t systick_get_ms(void)
{
    return mock_now;
}
uint16_t adc_get(uint8_t idx)
{
    return 0;
}
uint16_t adc_to_millivolts(uint8_t idx)
{
    return mock_dc;
}
uint16_t adc_to_milliamps(uint8_t idx)
{
    return mock_current;
}
uint16_t adc_to_celcius(uint8_t idx)
{
    return mock_temperature;
}
uint32_t adc_get_milliwatts(void)
{
    return mock_power;
}
bool adc_get_reference_millivolts(uint16_t* value)
{
    *value = mock_reference;
    return true;
}
bool adc_has_power_loss_shutdown(void)
{
    return mock_loss;
}
bool adc_has_buck_voltage_spike_shutdown(void)
{
    return mock_spike;
}
uint8_t adc_copy_dc_voltage_history(uint16_t* data, uint8_t capacity)
{
    return 0;
}
#ifndef NATIVE_FUNCTIONAL_BUTTON
bool btn_has_long_press(bool clear)
{
    return false;
}

bool btn_is_down(void)
{
    return false;
}

void btn_reset_consecutive_presses(void) {}

uint32_t btn_get_consecutive_presses(void)
{
    return 0;
}

#endif /* NATIVE_FUNCTIONAL_BUTTON */

void show_fault(const char* text, bool reset)
{
    if (!mock_fault)
    {
        mock_fault   = text;
        mock_allowed = reset;
    }
}

#ifndef NATIVE_FUNCTIONAL_BUTTON
void show_short_msg(const char* text, uint32_t duration) {}
#endif

bool battery_check(void)
{
    return mock_battery_ok;
}

bool battery_can_override(void)
{
    return false;
}

void battery_show_fault(bool override)
{
    show_fault("BATTERY", false);
}

void leaky_bucket_task(void) {}

uint16_t leaky_bucket_get_temperature_c(void)
{
    return mock_bucket;
}

uint16_t hotwand_rand(void)
{
    return 0;
}

#ifndef NATIVE_FUNCTIONAL_GRAPH
void u8g2_SetDrawColor(u8g2_t* g, uint8_t color) {}

void u8g2_SetFont(u8g2_t* g, const uint8_t* font) {}

void u8g2_DrawBox(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h) {}

void u8g2_DrawPixel(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y) {}

void u8g2_DrawVLine(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t h) {}

u8g2_uint_t u8g2_DrawStr(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y, const char* text)
{
    return 0;
}

#endif /* NATIVE_FUNCTIONAL_GRAPH */
