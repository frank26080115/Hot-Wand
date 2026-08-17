/*
The ADC code will sample all connected ADC channels in a round-robin fashion.
The completion of one conversion triggers the next channel's conversion, using the ISR.
A digital low pass filter is automatically applied. This filter can be configured on a per-channel basis.
This file also contains some convenience functions for unit conversions.
*/

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "adc.h"

#include "conf.h"
#include "pins.h"
#include "rfgen.h"
#include "stm32f0xx_hal.h"
#include "stm32f0xx_ll_adc.h"
#include "typedefs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define ADC_INPUT_COUNT        6
#define ADC_VREFINT_IDX        ADC_INPUT_COUNT
#define ADC_TABLE_ENTRY_COUNT  (ADC_INPUT_COUNT + 1)
#define ADC_LPF_SCALE          1024
#define ADC_LPF_ROUNDING       (ADC_LPF_SCALE / 2)
#define ADC_DEFAULT_LPF_ALPHA  900
#define ADC_IRQ_PRIORITY       1
#define ADC_ANALOG_PINS        (DC_SENS_PINn | THERM_1_PINn | THERM_2_PINn | BUCK_SENS_PINn | CURR_SENS_PINn)
#define CURRENT_FULL_SCALE_MA  6037
#define INPUT_V_CALIB_SCALE    1024
#define INPUT_V_CALIB_ROUNDING (INPUT_V_CALIB_SCALE / 2)

#define NTC_TABLE_STEP_C         10
#define MCU_TEMP_CAL1_ADDRESS    0x1FFFF7B8
#define MCU_TEMP_CAL1_C          30
#define MCU_TEMP_ADC_FULL_SCALE  4095
#define MCU_TEMP_SLOPE_TENTHS_MV 43
#define MCU_TEMP_SCALE           (MCU_TEMP_ADC_FULL_SCALE * MCU_TEMP_SLOPE_TENTHS_MV)

// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

typedef struct
{
    uint32_t channel;
    uint16_t lpf_alpha_high;
    uint16_t lpf_alpha_low;
} adc_cfg_t;

typedef struct
{
    uint16_t slope_q10;
    int8_t   offset_mv;
} input_voltage_calib_t;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

/*
 * Alpha is the weight retained from the previous filtered value:
 *   0    follows the input immediately
 *   1024 holds the previous value
 *
 * The high and low values allow different filtering rates for rising and
 * falling inputs. They are currently equal, as requested.
 */
static const adc_cfg_t lpf_cfg_table[ADC_TABLE_ENTRY_COUNT] = {
    [DC_SENS_IDX] =
        {
                       .channel        = DC_SENS_ADCCHANn,
                       .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
                       .lpf_alpha_low  = ADC_DEFAULT_LPF_ALPHA,
                       },
    [BUCK_SENS_IDX] =
        {
                       .channel        = BUCK_SENS_ADCCHANn,
                       .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
                       .lpf_alpha_low  = ADC_DEFAULT_LPF_ALPHA,
                       },
    [CURR_SENS_IDX] =
        {
                       .channel        = CURR_SENS_ADCCHANn,
                       .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
                       .lpf_alpha_low  = ADC_DEFAULT_LPF_ALPHA,
                       },
    [MCU_TEMP_IDX] =
        {
                       .channel        = MCU_TEMP_ADCCHANn,
                       .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
                       .lpf_alpha_low  = ADC_DEFAULT_LPF_ALPHA,
                       },
    [THERM_1_IDX] =
        {
                       .channel        = THERM_1_ADCCHANn,
                       .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
                       .lpf_alpha_low  = ADC_DEFAULT_LPF_ALPHA,
                       },
    [THERM_2_IDX] =
        {
                       .channel        = THERM_2_ADCCHANn,
                       .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
                       .lpf_alpha_low  = ADC_DEFAULT_LPF_ALPHA,
                       },
    [ADC_VREFINT_IDX] =
        {
                       .channel        = ADC_CHANNEL_VREFINT,
                       .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
                       .lpf_alpha_low  = ADC_DEFAULT_LPF_ALPHA,
                       },
};

/*
 * Expected 10-bit ADC readings for the 10K, beta-3950 NTC thermistors and
 * their 2.2K pull-ups, in 10 C steps from 0 C through 150 C.
 * use "tools\ntc_table_gen.py" to generate a new table for different thermistors
 */
static const uint16_t ntc_adc_by_10c[] = {
    960,
    922,
    870,
    803,
    723,
    634,
    543,
    455,
    374,
    305,
    246,
    198,
    160,
    129,
    105,
    85,
};

/* Q10 slopes are the nearest integers to the requested multipliers. */
static const input_voltage_calib_t input_voltage_calib_table[] = {
    [INPUT_VOLTAGE_CALIB_NONE] = {1024, 0  },
    [INPUT_VOLTAGE_CALIB_P1]   = {1029, 5  },
    [INPUT_VOLTAGE_CALIB_P2]   = {1034, 10 },
    [INPUT_VOLTAGE_CALIB_P3]   = {1039, 15 },
    [INPUT_VOLTAGE_CALIB_P4]   = {1044, 20 },
    [INPUT_VOLTAGE_CALIB_P5]   = {1050, 25 },
    [INPUT_VOLTAGE_CALIB_N1]   = {1019, -5 },
    [INPUT_VOLTAGE_CALIB_N2]   = {1014, -10},
    [INPUT_VOLTAGE_CALIB_N3]   = {1009, -15},
    [INPUT_VOLTAGE_CALIB_N4]   = {1004, -20},
    [INPUT_VOLTAGE_CALIB_N5]   = {998,  -25},
};

_Static_assert((sizeof(input_voltage_calib_table) / sizeof(input_voltage_calib_table[0])) ==
                   (INPUT_VOLTAGE_CALIB_N5 + 1),
               "Input-voltage calibration table is incomplete");

static ADC_HandleTypeDef adc_handle;
static uint32_t          lpf_state[ADC_TABLE_ENTRY_COUNT];
static volatile uint16_t result[ADC_TABLE_ENTRY_COUNT];
static volatile uint16_t raw[ADC_TABLE_ENTRY_COUNT];
static volatile uint8_t  initialized_channels;
static volatile uint8_t  current_idx;
static volatile uint16_t adc_reference_mv;
static volatile uint16_t dc_voltage_history[ADC_DC_VOLTAGE_HISTORY_COUNT];
static volatile uint8_t  dc_voltage_history_write_idx;
static volatile uint8_t  dc_voltage_history_count;
static volatile uint32_t dc_voltage_history_generation;
static volatile uint32_t completed_round_count;
static volatile bool     adc_power_loss_shutdown;
static volatile bool     adc_buck_voltage_spike_shutdown;
static uint32_t          dc_voltage_accumulator;
static uint16_t          dc_voltage_accumulator_count;
static bool              adc_round_started;
static volatile uint32_t system_rand_seed          = 0;
static volatile uint8_t  input_voltage_calibration = INPUT_VOLTAGE_CALIB_NONE;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void     adc_filter_sample(uint8_t idx, uint16_t sample);
static void     adc_select_and_start(uint8_t idx);
static void     adc_complete_round(void);
static void     adc_publish_dc_voltage_average(void);
static uint16_t adc_input_sample_to_millivolts(uint16_t sample);
static void     adc_check_fast_power_loss(uint16_t sample);
static void     adc_update_reference_voltage(uint16_t sample);
static uint16_t adc_calibrate_input_voltage(uint16_t millivolts);
static uint16_t adc_ntc_to_celcius(uint16_t sample);
static uint16_t adc_mcu_to_celcius(uint16_t sample);
static void     adc_fault(void);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void adc_init(void)
{
    ADC_ChannelConfTypeDef channel_cfg = {0};
    GPIO_InitTypeDef       gpio_cfg    = {0};
    uint8_t                idx;

    HAL_NVIC_DisableIRQ(ADC1_IRQn);

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    gpio_cfg.Pin  = ADC_ANALOG_PINS;
    gpio_cfg.Mode = GPIO_MODE_ANALOG;
    gpio_cfg.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio_cfg);

    for (idx = 0; idx < ADC_TABLE_ENTRY_COUNT; ++idx)
    {
        lpf_state[idx] = 0;
        result[idx]    = 0;
    }

    initialized_channels            = 0;
    dc_voltage_history_write_idx    = 0;
    dc_voltage_history_count        = 0;
    dc_voltage_history_generation   = 0;
    completed_round_count           = 0;
    adc_reference_mv                = ADC_REFERENCE_MV;
    adc_power_loss_shutdown         = false;
    adc_buck_voltage_spike_shutdown = false;
    dc_voltage_accumulator          = 0;
    dc_voltage_accumulator_count    = 0;
    adc_round_started               = false;
    /* Start with the internal temperature channel so HAL can enable and
     * stabilize it before the normal external-input rounds begin. */
    current_idx = MCU_TEMP_IDX;

    adc_handle.Instance                   = ADC1;
    adc_handle.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV4;
    adc_handle.Init.Resolution            = ADC_RESOLUTION_10B;
    adc_handle.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    adc_handle.Init.ScanConvMode          = ADC_SCAN_DIRECTION_FORWARD;
    adc_handle.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    adc_handle.Init.LowPowerAutoWait      = DISABLE;
    adc_handle.Init.LowPowerAutoPowerOff  = DISABLE;
    adc_handle.Init.ContinuousConvMode    = DISABLE;
    adc_handle.Init.DiscontinuousConvMode = DISABLE;
    adc_handle.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    adc_handle.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
    adc_handle.Init.DMAContinuousRequests = DISABLE;
    adc_handle.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    adc_handle.Init.SamplingTimeCommon    = ADC_SAMPLETIME_71CYCLES_5;

    if (HAL_ADC_Init(&adc_handle) != HAL_OK)
    {
        adc_fault();
    }

    if (HAL_ADCEx_Calibration_Start(&adc_handle) != HAL_OK)
    {
        adc_fault();
    }

    /* Enable both internal measurement paths through HAL. The round-robin
     * sequencer below still selects exactly one channel at a time. */
    channel_cfg.Channel      = lpf_cfg_table[current_idx].channel;
    channel_cfg.Rank         = ADC_RANK_CHANNEL_NUMBER;
    channel_cfg.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;

    if (HAL_ADC_ConfigChannel(&adc_handle, &channel_cfg) != HAL_OK)
    {
        adc_fault();
    }

    channel_cfg.Channel = lpf_cfg_table[ADC_VREFINT_IDX].channel;
    if (HAL_ADC_ConfigChannel(&adc_handle, &channel_cfg) != HAL_OK)
    {
        adc_fault();
    }

    adc_handle.Instance->CHSELR = 1UL << lpf_cfg_table[current_idx].channel;

    HAL_NVIC_ClearPendingIRQ(ADC1_IRQn);
    /* Tip detection uses priority 0; ADC completion is deliberately second. */
    HAL_NVIC_SetPriority(ADC1_IRQn, ADC_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(ADC1_IRQn);

    if (HAL_ADC_Start_IT(&adc_handle) != HAL_OK)
    {
        adc_fault();
    }
}

/*
 * Implementation target for the strong vector shim in interrupt_vectors.S.
 * It is intentionally not part of
 * adc.h's public API.
 */
void ADC1_IRQHandler_Impl(void)
{
    uint32_t flags = adc_handle.Instance->ISR;
    uint16_t sample;
    uint8_t  completed_idx;
    uint8_t  next_idx;

    if ((flags & (ADC_ISR_EOC | ADC_ISR_EOS)) == 0)
    {
        adc_handle.Instance->ISR = flags & ADC_ISR_OVR;

        if ((adc_handle.Instance->CR & ADC_CR_ADSTART) == 0)
        {
            adc_select_and_start(current_idx);
        }

        return;
    }

    completed_idx      = current_idx;
    sample             = (uint16_t)HAL_ADC_GetValue(&adc_handle);
    raw[completed_idx] = sample;

    next_idx = (uint8_t)(completed_idx + 1);
    if (((next_idx >= ADC_INPUT_COUNT) &&
         ((completed_round_count % ADC_VREFINT_SAMPLE_INTERVAL_ROUNDS) != 0)) ||
        (next_idx > ADC_VREFINT_IDX))
    {
        next_idx = 0;
    }
    current_idx = next_idx;

    /*
     * Clear the completed conversion before starting the next one. No flags
     * are cleared after ADSTART, so
     * a fast next conversion cannot be lost.
     */
    adc_handle.Instance->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
    adc_select_and_start(next_idx);

    if (next_idx == 0)
    {
        adc_complete_round();
    }

    /* Filter while the ADC is already sampling the next channel. */
    adc_filter_sample(completed_idx, sample);
    if (completed_idx == ADC_VREFINT_IDX)
    {
        adc_update_reference_voltage(result[ADC_VREFINT_IDX]);
    }

    if (next_idx == 0)
    {
        /* Update the random seed with the latest ADC readings. Take advantage of electrical noise. */
        system_rand_seed = (HAL_GetTick() & 0x3) | (((uint32_t)raw[0] & 0x03) << 2) | (((uint32_t)raw[1] & 0x03) << 4) |
                           (((uint32_t)raw[2] & 0x03) << 6) | (((uint32_t)raw[3] & 0x03) << 8) |
                           (((uint32_t)raw[4] & 0x03) << 10);
    }
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

uint16_t adc_to_millivolts(uint8_t idx)
{
    uint32_t millivolts;
    uint16_t reference_mv;

    if ((idx != DC_SENS_IDX) && (idx != BUCK_SENS_IDX))
    {
        return 0;
    }

    if (idx == DC_SENS_IDX)
    {
        return adc_input_sample_to_millivolts(adc_get(idx));
    }

    reference_mv = adc_reference_mv;
    millivolts   = (uint32_t)adc_get(idx) * reference_mv * VOLTAGE_DIVIDER_SCALE;
    millivolts += ADC_FULL_SCALE / 2;

    millivolts /= ADC_FULL_SCALE;

    return (uint16_t)millivolts;
}

uint16_t adc_to_milliamps(uint8_t idx)
{
    uint32_t full_scale_ma;
    uint32_t milliamps;
    uint16_t reference_mv;

    if (idx != CURR_SENS_IDX)
    {
        return 0;
    }

    reference_mv = adc_reference_mv;
    full_scale_ma =
        ((uint32_t)CURRENT_FULL_SCALE_MA * reference_mv + (ADC_REFERENCE_MV / 2)) / ADC_REFERENCE_MV;

    milliamps = (uint32_t)adc_get(idx) * full_scale_ma;
    milliamps += ADC_FULL_SCALE / 2;

    return (uint16_t)(milliamps / ADC_FULL_SCALE);
}

uint16_t adc_to_celcius(uint8_t idx)
{
    uint8_t initialized_mask;

    switch (idx)
    {
    case THERM_1_IDX:
    case THERM_2_IDX:
    case MCU_TEMP_IDX:
        break;

    default:
        return 0;
    }

    initialized_mask = (uint8_t)(1 << idx);
    if ((initialized_channels & initialized_mask) == 0)
    {
        return 0;
    }

    if (idx == MCU_TEMP_IDX)
    {
        return adc_mcu_to_celcius(adc_get(idx));
    }

    return adc_ntc_to_celcius(adc_get(idx));
}

uint32_t adc_get_milliwatts(void)
{
    uint32_t millivolts = adc_to_millivolts(BUCK_SENS_IDX);
    uint32_t milliamps  = adc_to_milliamps(CURR_SENS_IDX);

    return ((millivolts * milliamps) + 500) / 1000;
}

// -----------------------------------------------------------------------------
// Getters and Setters
// -----------------------------------------------------------------------------

uint8_t adc_copy_dc_voltage_history(uint16_t* history, uint8_t capacity)
{
    uint32_t generation_before;
    uint32_t generation_after;
    uint8_t  available;
    uint8_t  copy_count;
    uint8_t  write_idx;
    uint8_t  age;
    uint8_t  idx;

    if ((history == NULL) || (capacity == 0))
    {
        return 0;
    }

    /* The ISR publishes a generation only after its record and ring metadata
     * are complete. Retry if publication
     * overlaps this lock-free copy, keeping
     * the priority-0 tip detector unblocked. */
    do
    {
        generation_before = dc_voltage_history_generation;
        available         = dc_voltage_history_count;
        write_idx         = dc_voltage_history_write_idx;
        copy_count        = available < capacity ? available : capacity;

        for (age = 0; age < copy_count; ++age)
        {
            if (write_idx > age)
            {
                idx = (uint8_t)(write_idx - age - 1);
            }
            else
            {
                idx = (uint8_t)(ADC_DC_VOLTAGE_HISTORY_COUNT + write_idx - age - 1);
            }
            history[age] = dc_voltage_history[idx];
        }

        generation_after = dc_voltage_history_generation;
    } while (generation_before != generation_after);

    return copy_count;
}

uint32_t adc_get_completed_round_count(void)
{
    return completed_round_count;
}

bool adc_has_power_loss_shutdown(void)
{
    return adc_power_loss_shutdown;
}

bool adc_has_buck_voltage_spike_shutdown(void)
{
    return adc_buck_voltage_spike_shutdown;
}

uint16_t adc_get(uint8_t idx)
{
    if (idx >= ADC_INPUT_COUNT)
    {
        return 0;
    }

    return result[idx];
}

void adc_set_input_voltage_calibration(uint8_t calibration)
{
    if (calibration > INPUT_VOLTAGE_CALIB_N5)
    {
        calibration = INPUT_VOLTAGE_CALIB_NONE;
    }

    input_voltage_calibration = calibration;
}

uint32_t adc_get_rand_seed(void)
{
    return (int32_t)system_rand_seed;
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static uint16_t adc_calibrate_input_voltage(uint16_t millivolts)
{
    const input_voltage_calib_t* calibration;
    int32_t                      calibrated_mv;
    uint32_t                     scaled_mv;

    if (input_voltage_calibration == INPUT_VOLTAGE_CALIB_NONE)
    {
        return millivolts;
    }

    calibration   = &input_voltage_calib_table[input_voltage_calibration];
    scaled_mv     = (uint32_t)calibration->slope_q10 * millivolts;
    calibrated_mv = (int32_t)((scaled_mv + INPUT_V_CALIB_ROUNDING) >> 10) + calibration->offset_mv;

    if (calibrated_mv <= 0)
    {
        return 0;
    }
    if (calibrated_mv > (int32_t)UINT16_MAX)
    {
        return UINT16_MAX;
    }

    return (uint16_t)calibrated_mv;
}

static void adc_filter_sample(uint8_t idx, uint16_t sample)
{
    const adc_cfg_t* cfg              = &lpf_cfg_table[idx];
    const uint8_t    initialized_mask = (uint8_t)(1 << idx);
    const uint32_t   target           = (uint32_t)sample * ADC_LPF_SCALE;
    uint32_t         alpha;
    uint32_t         new_weight;
    uint32_t         state;
    uint32_t         weighted_sum;
    uint16_t         new_value;

    if (idx == DC_SENS_IDX)
    {
        /* Accumulate raw samples so each history entry represents all DC
         * conversions in its 128-round
         * interval, independent of the published
         * low-pass filter. */
        dc_voltage_accumulator += sample;
        ++dc_voltage_accumulator_count;

        /* This runs in ADC interrupt context after the next conversion has
         * already started. A sharp collapse
         * therefore disables RF without
         * waiting for the foreground power-management task. */
        adc_check_fast_power_loss(sample);
    }
    else if ((idx == BUCK_SENS_IDX) && !adc_buck_voltage_spike_shutdown &&
             (sample >= PWRMGT_BUCK_VOLTAGE_SPIKE_ADC_COUNTS))
    {
        /* Use the raw sample so the normal low-pass filter cannot delay this
         * terminal protection. The
         * foreground power-management task owns the
         * fault display, while the interrupt immediately latches
         * RF off. */
        adc_buck_voltage_spike_shutdown = true;
        rfgen_emergency_stop();
    }

    if ((initialized_channels & initialized_mask) == 0)
    {
        lpf_state[idx] = target;
        result[idx]    = sample;
        initialized_channels |= initialized_mask;
        return;
    }

    state = lpf_state[idx];
    alpha = (target >= state) ? cfg->lpf_alpha_high : cfg->lpf_alpha_low;
    if (alpha > ADC_LPF_SCALE)
    {
        alpha = ADC_LPF_SCALE;
    }

    new_weight   = ADC_LPF_SCALE - alpha;
    weighted_sum = (state * alpha) + (target * new_weight);
    state        = (weighted_sum + ADC_LPF_ROUNDING) / ADC_LPF_SCALE;

    lpf_state[idx] = state;
    new_value      = (uint16_t)((state + ADC_LPF_ROUNDING) / ADC_LPF_SCALE);
    result[idx]    = new_value;
}

static void adc_complete_round(void)
{
    /* Initialization begins with one temperature conversion so its internal
     * path can settle. The first wrap
     * starts, rather than completes, a full
     * six-channel round. */
    if (!adc_round_started)
    {
        adc_round_started = true;
        return;
    }

    ++completed_round_count;
    if ((completed_round_count % ADC_DC_VOLTAGE_HISTORY_ROUNDS_PER_ENTRY) == 0)
    {
        adc_publish_dc_voltage_average();
    }
}

static void adc_publish_dc_voltage_average(void)
{
    uint32_t average_sample;

    if (dc_voltage_accumulator_count == 0)
    {
        return;
    }

    average_sample = (dc_voltage_accumulator + (dc_voltage_accumulator_count / 2)) / dc_voltage_accumulator_count;
    dc_voltage_history[dc_voltage_history_write_idx] = (uint16_t)average_sample;

    ++dc_voltage_history_write_idx;
    if (dc_voltage_history_write_idx >= ADC_DC_VOLTAGE_HISTORY_COUNT)
    {
        dc_voltage_history_write_idx = 0;
    }
    if (dc_voltage_history_count < ADC_DC_VOLTAGE_HISTORY_COUNT)
    {
        ++dc_voltage_history_count;
    }

    dc_voltage_accumulator       = 0;
    dc_voltage_accumulator_count = 0;

    /* Publish last so foreground readers can detect an overlapping update. */
    ++dc_voltage_history_generation;
}

static uint16_t adc_input_sample_to_millivolts(uint16_t sample)
{
    uint32_t millivolts;
    uint16_t reference_mv;

    reference_mv = adc_reference_mv;
    millivolts   = (uint32_t)sample * reference_mv * VOLTAGE_DIVIDER_SCALE;
    millivolts += ADC_FULL_SCALE / 2;
    millivolts /= ADC_FULL_SCALE;

    return adc_calibrate_input_voltage((uint16_t)millivolts);
}

static void adc_check_fast_power_loss(uint16_t sample)
{
    uint16_t reference_sample;
    uint8_t  newest_idx;

    if (adc_power_loss_shutdown || (dc_voltage_history_count == 0))
    {
        return;
    }

    newest_idx       = dc_voltage_history_write_idx == 0 ? (ADC_DC_VOLTAGE_HISTORY_COUNT - 1)
                                                         : (uint8_t)(dc_voltage_history_write_idx - 1);
    reference_sample = dc_voltage_history[newest_idx];

    if ((reference_sample > sample) &&
        (((uint32_t)reference_sample - sample) >= PWRMGT_POWER_LOSS_FAST_DROP_ADC_COUNTS))
    {
        adc_power_loss_shutdown = true;
        rfgen_emergency_stop();
    }
}

static void adc_update_reference_voltage(uint16_t sample)
{
    uint32_t reference_mv;

    if (sample == 0)
    {
        return;
    }

    reference_mv = __LL_ADC_CALC_VREFANALOG_VOLTAGE(sample, LL_ADC_RESOLUTION_10B);
    if ((reference_mv == 0) || (reference_mv > UINT16_MAX))
    {
        return;
    }

    adc_reference_mv = (uint16_t)reference_mv;
}

static void adc_select_and_start(uint8_t idx)
{
    /*
     * HAL_ADC_ConfigChannel() ORs into CHSELR on STM32F0. Assigning the
     * register keeps exactly one channel selected for this conversion.
     */
    adc_handle.Instance->CHSELR = 1 << lpf_cfg_table[idx].channel;
    adc_handle.Instance->CR |= ADC_CR_ADSTART;
}

static uint16_t adc_ntc_to_celcius(uint16_t sample)
{
    uint8_t table_idx;

    // this will catch a disconnected/missing NTC thermistor, which will read as a very high ADC value
    if (sample >= ntc_adc_by_10c[0])
    {
        return 0;
    }

    for (table_idx = 1; table_idx < (uint8_t)(sizeof(ntc_adc_by_10c) / sizeof(ntc_adc_by_10c[0])); ++table_idx)
    {
        if (sample >= ntc_adc_by_10c[table_idx])
        {
            uint16_t span     = ntc_adc_by_10c[table_idx - 1] - ntc_adc_by_10c[table_idx];
            uint16_t offset   = ntc_adc_by_10c[table_idx - 1] - sample;
            uint16_t fraction = (uint16_t)(((uint32_t)offset * NTC_TABLE_STEP_C + (span / 2)) / span);

            return (uint16_t)(((uint16_t)table_idx - 1) * NTC_TABLE_STEP_C + fraction);
        }
    }

    return (uint16_t)((sizeof(ntc_adc_by_10c) / sizeof(ntc_adc_by_10c[0]) - 1) * NTC_TABLE_STEP_C);
}

static uint16_t adc_mcu_to_celcius(uint16_t sample)
{
    const uint16_t calibration = *(const uint16_t*)MCU_TEMP_CAL1_ADDRESS;
    int32_t        delta;
    int32_t        temperature;
    uint32_t       normalized_sample;
    uint32_t       rounded_delta;
    uint16_t       reference_mv;

    reference_mv      = adc_reference_mv;
    normalized_sample = ((uint32_t)sample << 2) * reference_mv;
    normalized_sample += ADC_REFERENCE_MV / 2;
    normalized_sample /= ADC_REFERENCE_MV;

    delta = (int32_t)calibration - (int32_t)normalized_sample;
    delta *= (int32_t)(ADC_REFERENCE_MV * 10);

    if (delta >= 0)
    {
        rounded_delta = (uint32_t)delta + (MCU_TEMP_SCALE / 2);
        temperature   = MCU_TEMP_CAL1_C + (int32_t)(rounded_delta / MCU_TEMP_SCALE);
    }
    else
    {
        rounded_delta = (uint32_t)(-delta) + (MCU_TEMP_SCALE / 2);
        temperature   = MCU_TEMP_CAL1_C - (int32_t)(rounded_delta / MCU_TEMP_SCALE);
    }

    if (temperature <= 0)
    {
        return 0;
    }

    if (temperature > (int32_t)UINT16_MAX)
    {
        return UINT16_MAX;
    }

    return (uint16_t)temperature;
}

// -----------------------------------------------------------------------------
// Debug / Fault Helpers
// -----------------------------------------------------------------------------

static void adc_fault(void)
{
    /*
    Under the present boot sequence, all these preconditions are controlled:

    adc_handle starts zero-initialized; The ADC clock is explicitly enabled; Calibration runs while the ADC is disabled;
    Channel configuration happens before conversion starts; adc_init() is called only once;

    Therefore, on healthy hardware and uncorrupted firmware, adc_fault() should effectively never happen. Realistic
    causes would be:

    Damaged or malfunctioning ADC peripheral; ADC clock-domain failure; Severe undervoltage or clock instability during
    startup; RAM/register corruption; Calling adc_init() again while conversions are running; A future
    initialization-order regression;
    */
    HAL_NVIC_DisableIRQ(ADC1_IRQn);

    for (;;)
    {
    }
}
