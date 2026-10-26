/* Compile actual firmware sources into a native DLL for Python tests. Only peripheral
 * access/RF side effects are mocked; the ISR and supervision predicates below are the
 * production implementations. */
// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "../../../src/adc.c"
#include "../../../src/adc_sim.c"
#include "../../../src/pwrmgt.c"

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

ADC_TypeDef     mock_adc;
static uint32_t emergency_stops;
static uint32_t power_loss_stops;
static bool     mock_rf_active, mock_rf_rearm_allowed;

const uint8_t u8g2_font_6x10_tr[1] = {0};

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

/* No CRT dependency: the DLL has no entry point and initializes no hardware. */
__declspec(dllexport) void bridge_reset(void)
{
    uint32_t          idx;
    volatile uint8_t* bytes = (volatile uint8_t*)&adc_sim;
    for (idx = 0; idx < sizeof(adc_sim); ++idx)
    {
        bytes[idx] = 0;
    }
    mock_rf_active        = false;
    mock_rf_rearm_allowed = true;
    adc_sim_init();
    adc_reset_sim_signal_state();
#if PWRMGT_POWER_LOSS_FAST_DROP_ADC_COUNTS > 0
    adc_power_loss_shutdown         = false;
#endif
#if PWRMGT_BUCK_VOLTAGE_SPIKE_MV > 0
    adc_buck_voltage_spike_shutdown = false;
#endif
    adc_round_started               = false;
    input_voltage_calibration       = INPUT_VOLTAGE_CALIB_NONE;
    current_idx                     = MCU_TEMP_IDX;
    emergency_stops                 = 0;
    power_loss_stops                = 0;
    adc_handle.Instance             = &mock_adc;
}

__declspec(dllexport) uint32_t bridge_conversion(uint16_t hardware_sample)
{
    uint8_t channel = current_idx;
    mock_adc.DR     = hardware_sample;
    mock_adc.ISR    = ADC_ISR_EOC;
    ADC1_IRQHandler_Impl();
    return channel;
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

__declspec(dllexport) bool bridge_enqueue(uint8_t code, uint32_t repeats)
{
    adc_sim_frame_t frame;
    uint8_t         idx;
    for (idx = 0; idx < ADC_SIM_CHANNEL_COUNT; ++idx)
    {
        frame.channels[idx] = code;
    }
    frame.reserved      = 0;
    frame.repeat_rounds = repeats;
    return adc_sim_enqueue(&frame);
}

__declspec(dllexport) bool bridge_start(void)
{
    return adc_sim_start();
}

__declspec(dllexport) bool bridge_boundary(bool completed)
{
    return adc_sim_round_boundary(completed);
}

__declspec(dllexport) void bridge_init_adc(void)
{
    adc_init();
}

__declspec(dllexport) void bridge_set_latches(void)
{
#if PWRMGT_POWER_LOSS_FAST_DROP_ADC_COUNTS > 0
    adc_power_loss_shutdown         = true;
#endif
#if PWRMGT_BUCK_VOLTAGE_SPIKE_MV > 0
    adc_buck_voltage_spike_shutdown = true;
#endif
    adc_set_input_voltage_calibration(INPUT_VOLTAGE_CALIB_P5);
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void rfgen_emergency_stop(void)
{
    ++emergency_stops;
}

void rfgen_power_loss_stop(void)
{
    ++power_loss_stops;
}

bool rfgen_is_active(void)
{
    return mock_rf_active;
}

bool rfgen_rearm_power_loss(void)
{
    return mock_rf_rearm_allowed;
}

bool rfgen_rearm_power_loss_for_test(void)
{
    return mock_rf_rearm_allowed;
}

/* Unused power-manager UI/plant dependencies. The tests call its real power-loss predicate
 * only, not these unrelated render/control functions. */
uint32_t systick_get_ms(void)
{
    return 0;
}

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

void show_fault(const char* text, bool reset) {}

void show_short_msg(const char* text, uint32_t duration) {}

bool battery_check(void)
{
    return true;
}

bool battery_can_override(void)
{
    return false;
}

void battery_show_fault(bool override) {}

void leaky_bucket_task(void) {}

uint16_t leaky_bucket_get_temperature_c(void)
{
    return 25;
}

void pwrlvl_set_mode(pwrlvl_mode_t mode) {}

void pwrlvl_task(void) {}

bool pwrlvl_is_current_limiting(void)
{
    return false;
}

uint16_t hotwand_rand(void)
{
    return 0;
}

void u8g2_SetDrawColor(u8g2_t* g, uint8_t color) {}

void u8g2_SetFont(u8g2_t* g, const uint8_t* font) {}

void u8g2_DrawBox(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t w, u8g2_uint_t h) {}

void u8g2_DrawPixel(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y) {}

void u8g2_DrawVLine(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t h) {}

u8g2_uint_t u8g2_DrawStr(u8g2_t* g, u8g2_uint_t x, u8g2_uint_t y, const char* text)
{
    return 0;
}

// -----------------------------------------------------------------------------
// Debug / Logging Helpers
// -----------------------------------------------------------------------------

__declspec(dllexport) uint32_t bridge_value(uint32_t selector, uint32_t idx)
{
    switch (selector)
    {
    case 0:
        return raw[idx];
    case 1:
        return result[idx];
    case 2:
        return adc_get_completed_round_count();
    case 3:
        return emergency_stops;
    case 4:
        return power_loss_stops;
    case 5:
        return pwrmgt_power_loss_detected();
    case 6:
        return adc_get_rand_seed();
    case 7:
        return adc_reference_result_valid;
    case 8:
        return adc_reference_result_mv;
    case 9:
        return dc_voltage_history_count;
    case 10:
        return adc_to_millivolts(DC_SENS_IDX);
    case 11:
        return current_idx;
    default:
        return 0;
    }
}

__declspec(dllexport) uint32_t bridge_latches(void)
{
    return adc_has_power_loss_shutdown() + 2 * adc_has_buck_voltage_spike_shutdown() +
           4 * (input_voltage_calibration == INPUT_VOLTAGE_CALIB_P5);
}

__declspec(dllexport) uint32_t bridge_history(uint32_t age)
{
    uint16_t history[ADC_DC_VOLTAGE_HISTORY_COUNT];
    uint8_t  count = adc_copy_dc_voltage_history(history, ADC_DC_VOLTAGE_HISTORY_COUNT);
    return age < count ? history[age] : UINT16_MAX;
}

/* Native-only exact-count entry point: the LUT cannot represent every count.
 * Publish a newest-first history through the same ring layout, then run the
 * production ISR or foreground predicate. No target-side hook is added. */
__declspec(dllexport) void bridge_exact_history(const uint16_t* values, uint32_t count)
{
    dc_voltage_history_count     = count;
    dc_voltage_history_write_idx = count;
    for (uint32_t index = 0; index < count; ++index)
    {
        dc_voltage_history[count - 1 - index] = values[index];
    }
    ++dc_voltage_history_generation;
}

__declspec(dllexport) uint32_t bridge_reference(uint32_t sample)
{
    adc_update_reference_voltage(sample);
    return adc_reference_mv;
}

void show_power_loss_fault(void)
{
#if PWRMGT_POWER_LOSS_FAST_DROP_ADC_COUNTS > 0
    adc_power_loss_shutdown = false;
#endif
}
void pwrlvl_release_minimum(void) {}
void fan_resume(void) {}

__declspec(dllexport) uint32_t bridge_rearm(uint32_t diagnostic)
{
    return diagnostic ? adc_rearm_power_loss_for_test() : adc_rearm_power_loss();
}
__declspec(dllexport) void bridge_rearm_conditions(uint32_t active, uint32_t allowed, uint32_t spike)
{
#if PWRMGT_POWER_LOSS_FAST_DROP_ADC_COUNTS > 0
    adc_power_loss_shutdown         = true;
#endif
    mock_rf_active                  = active;
    mock_rf_rearm_allowed           = allowed;
#if PWRMGT_BUCK_VOLTAGE_SPIKE_MV > 0
    adc_buck_voltage_spike_shutdown = spike;
#endif
}

void show_undervoltage_fault(void) {}
