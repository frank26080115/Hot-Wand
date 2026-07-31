#include "adc.h"

#include "pins.h"
#include "stm32f0xx_hal.h"

#include <stdint.h>

#define ADC_INPUT_COUNT          6U
#define ADC_LPF_SCALE            1024U
#define ADC_LPF_ROUNDING         (ADC_LPF_SCALE / 2U)
#define ADC_DEFAULT_LPF_ALPHA    900U
#define ADC_ANALOG_PINS          (DC_SENS_PINn | THERM_1_PINn | THERM_2_PINn | \
                                  BUCK_SENS_PINn | CURR_SENS_PINn)
#define ADC_REFERENCE_MV         3300U
#define ADC_FULL_SCALE           1023U
#define VOLTAGE_DIVIDER_SCALE    11U
#define CURRENT_FULL_SCALE_MA    6037U

#define NTC_TABLE_STEP_C         10U
#define MCU_TEMP_CAL1_ADDRESS    0x1FFFF7B8UL
#define MCU_TEMP_CAL1_C          30L
#define MCU_TEMP_ADC_FULL_SCALE  4095L
#define MCU_TEMP_SLOPE_TENTHS_MV 43L
#define MCU_TEMP_SCALE           (MCU_TEMP_ADC_FULL_SCALE * \
                                  MCU_TEMP_SLOPE_TENTHS_MV)

typedef struct
{
    uint32_t channel;
    uint16_t lpf_alpha_high;
    uint16_t lpf_alpha_low;
} adc_cfg_t;

/*
 * Alpha is the weight retained from the previous filtered value:
 *   0    follows the input immediately
 *   1024 holds the previous value
 *
 * The high and low values allow different filtering rates for rising and
 * falling inputs. They are currently equal, as requested.
 */
static const adc_cfg_t lpf_cfg_table[ADC_INPUT_COUNT] = {
    [DC_SENS_IDX] = {
        .channel = DC_SENS_ADCCHANn,
        .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
        .lpf_alpha_low = ADC_DEFAULT_LPF_ALPHA,
    },
    [BUCK_SENS_IDX] = {
        .channel = BUCK_SENS_ADCCHANn,
        .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
        .lpf_alpha_low = ADC_DEFAULT_LPF_ALPHA,
    },
    [CURR_SENS_IDX] = {
        .channel = CURR_SENS_ADCCHANn,
        .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
        .lpf_alpha_low = ADC_DEFAULT_LPF_ALPHA,
    },
    [MCU_TEMP_IDX] = {
        .channel = MCU_TEMP_ADCCHANn,
        .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
        .lpf_alpha_low = ADC_DEFAULT_LPF_ALPHA,
    },
    [THERM_1_IDX] = {
        .channel = THERM_1_ADCCHANn,
        .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
        .lpf_alpha_low = ADC_DEFAULT_LPF_ALPHA,
    },
    [THERM_2_IDX] = {
        .channel = THERM_2_ADCCHANn,
        .lpf_alpha_high = ADC_DEFAULT_LPF_ALPHA,
        .lpf_alpha_low = ADC_DEFAULT_LPF_ALPHA,
    },
};

/*
 * Expected 10-bit ADC readings for the 10K, beta-3950 NTC thermistors and
 * their 2.2K pull-ups, in 10 C steps from 0 C through 150 C.
 */
static const uint16_t ntc_adc_by_10c[] = {
    960U, 922U, 870U, 803U, 723U, 634U, 543U, 455U,
    374U, 305U, 246U, 198U, 160U, 129U, 105U, 85U,
};

static ADC_HandleTypeDef adc_handle;
static uint32_t lpf_state[ADC_INPUT_COUNT];
static volatile uint16_t result[ADC_INPUT_COUNT];
static volatile uint8_t initialized_channels;
static uint8_t current_idx;

static void adc_filter_sample(uint8_t idx, uint16_t sample);
static void adc_select_and_start(uint8_t idx);
static uint16_t adc_ntc_to_celcius(uint16_t sample);
static uint16_t adc_mcu_to_celcius(uint16_t sample);
static void adc_fault(void);

void adc_init(void)
{
    ADC_ChannelConfTypeDef channel_cfg = {0};
    GPIO_InitTypeDef gpio_cfg = {0};
    uint8_t idx;

    HAL_NVIC_DisableIRQ(ADC1_IRQn);

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    gpio_cfg.Pin = ADC_ANALOG_PINS;
    gpio_cfg.Mode = GPIO_MODE_ANALOG;
    gpio_cfg.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &gpio_cfg);

    for (idx = 0U; idx < ADC_INPUT_COUNT; ++idx) {
        lpf_state[idx] = 0U;
        result[idx] = 0U;
    }

    initialized_channels = 0U;
    /*
     * Configuring the temperature channel through HAL enables its internal
     * path and waits for the required startup time. The ISR wraps to input 0
     * after this first conversion.
     */
    current_idx = MCU_TEMP_IDX;

    adc_handle.Instance = ADC1;
    adc_handle.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    adc_handle.Init.Resolution = ADC_RESOLUTION_10B;
    adc_handle.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    adc_handle.Init.ScanConvMode = ADC_SCAN_DIRECTION_FORWARD;
    adc_handle.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    adc_handle.Init.LowPowerAutoWait = DISABLE;
    adc_handle.Init.LowPowerAutoPowerOff = DISABLE;
    adc_handle.Init.ContinuousConvMode = DISABLE;
    adc_handle.Init.DiscontinuousConvMode = DISABLE;
    adc_handle.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    adc_handle.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    adc_handle.Init.DMAContinuousRequests = DISABLE;
    adc_handle.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    adc_handle.Init.SamplingTimeCommon = ADC_SAMPLETIME_71CYCLES_5;

    if (HAL_ADC_Init(&adc_handle) != HAL_OK) {
        adc_fault();
    }

    if (HAL_ADCEx_Calibration_Start(&adc_handle) != HAL_OK) {
        adc_fault();
    }

    channel_cfg.Channel = lpf_cfg_table[current_idx].channel;
    channel_cfg.Rank = ADC_RANK_CHANNEL_NUMBER;
    channel_cfg.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;

    if (HAL_ADC_ConfigChannel(&adc_handle, &channel_cfg) != HAL_OK) {
        adc_fault();
    }

    HAL_NVIC_ClearPendingIRQ(ADC1_IRQn);
    HAL_NVIC_SetPriority(ADC1_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(ADC1_IRQn);

    if (HAL_ADC_Start_IT(&adc_handle) != HAL_OK) {
        adc_fault();
    }
}

uint16_t adc_get(uint8_t idx)
{
    if (idx >= ADC_INPUT_COUNT) {
        return 0U;
    }

    return result[idx];
}

uint16_t adc_to_millivolts(uint8_t idx)
{
    uint32_t millivolts;

    if ((idx != DC_SENS_IDX) && (idx != BUCK_SENS_IDX)) {
        return 0U;
    }

    millivolts = (uint32_t)adc_get(idx) * ADC_REFERENCE_MV *
                 VOLTAGE_DIVIDER_SCALE;
    millivolts += ADC_FULL_SCALE / 2U;

    return (uint16_t)(millivolts / ADC_FULL_SCALE);
}

uint16_t adc_to_milliamps(uint8_t idx)
{
    uint32_t milliamps;

    if (idx != CURR_SENS_IDX) {
        return 0U;
    }

    milliamps = (uint32_t)adc_get(idx) * CURRENT_FULL_SCALE_MA;
    milliamps += ADC_FULL_SCALE / 2U;

    return (uint16_t)(milliamps / ADC_FULL_SCALE);
}

uint16_t adc_to_celcius(uint8_t idx)
{
    uint8_t initialized_mask;

    switch (idx) {
    case THERM_1_IDX:
    case THERM_2_IDX:
    case MCU_TEMP_IDX:
        break;

    default:
        return 0U;
    }

    initialized_mask = (uint8_t)(1U << idx);
    if ((initialized_channels & initialized_mask) == 0U) {
        return 0U;
    }

    if (idx == MCU_TEMP_IDX) {
        return adc_mcu_to_celcius(adc_get(idx));
    }

    return adc_ntc_to_celcius(adc_get(idx));
}

uint32_t adc_get_milliwatts(void)
{
    uint32_t millivolts = adc_to_millivolts(BUCK_SENS_IDX);
    uint32_t milliamps = adc_to_milliamps(CURR_SENS_IDX);

    return ((millivolts * milliamps) + 500U) / 1000U;
}

/*
 * This symbol must have external linkage because it is referenced by the
 * STM32F030 vector table. It is intentionally not part of adc.h's public API.
 */
void ADC1_IRQHandler(void)
{
    uint32_t flags = adc_handle.Instance->ISR;
    uint16_t sample;
    uint8_t completed_idx;
    uint8_t next_idx;

    if ((flags & (ADC_ISR_EOC | ADC_ISR_EOS)) == 0U) {
        adc_handle.Instance->ISR = flags & ADC_ISR_OVR;

        if ((adc_handle.Instance->CR & ADC_CR_ADSTART) == 0U) {
            adc_select_and_start(current_idx);
        }

        return;
    }

    completed_idx = current_idx;
    sample = (uint16_t)HAL_ADC_GetValue(&adc_handle);

    next_idx = (uint8_t)(completed_idx + 1U);
    if (next_idx >= ADC_INPUT_COUNT) {
        next_idx = 0U;
    }
    current_idx = next_idx;

    /*
     * Clear the completed conversion before starting the next one. No flags
     * are cleared after ADSTART, so a fast next conversion cannot be lost.
     */
    adc_handle.Instance->ISR = ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR;
    adc_select_and_start(next_idx);

    /* Filter while the ADC is already sampling the next channel. */
    adc_filter_sample(completed_idx, sample);
}

static void adc_filter_sample(uint8_t idx, uint16_t sample)
{
    const adc_cfg_t *cfg = &lpf_cfg_table[idx];
    const uint8_t initialized_mask = (uint8_t)(1U << idx);
    const uint32_t target = (uint32_t)sample * ADC_LPF_SCALE;
    uint32_t alpha;
    uint32_t new_weight;
    uint32_t state;
    uint32_t weighted_sum;

    if ((initialized_channels & initialized_mask) == 0U) {
        lpf_state[idx] = target;
        result[idx] = sample;
        initialized_channels |= initialized_mask;
        return;
    }

    state = lpf_state[idx];
    alpha = (target >= state) ? cfg->lpf_alpha_high : cfg->lpf_alpha_low;
    if (alpha > ADC_LPF_SCALE) {
        alpha = ADC_LPF_SCALE;
    }

    new_weight = ADC_LPF_SCALE - alpha;
    weighted_sum = (state * alpha) + (target * new_weight);
    state = (weighted_sum + ADC_LPF_ROUNDING) / ADC_LPF_SCALE;

    lpf_state[idx] = state;
    result[idx] = (uint16_t)((state + ADC_LPF_ROUNDING) / ADC_LPF_SCALE);
}

static void adc_select_and_start(uint8_t idx)
{
    /*
     * HAL_ADC_ConfigChannel() ORs into CHSELR on STM32F0. Assigning the
     * register keeps exactly one channel selected for this conversion.
     */
    adc_handle.Instance->CHSELR = 1UL << lpf_cfg_table[idx].channel;
    adc_handle.Instance->CR |= ADC_CR_ADSTART;
}

static uint16_t adc_ntc_to_celcius(uint16_t sample)
{
    uint8_t table_idx;

    if (sample >= ntc_adc_by_10c[0]) {
        return 0U;
    }

    for (table_idx = 1U;
         table_idx < (uint8_t)(sizeof(ntc_adc_by_10c) /
                               sizeof(ntc_adc_by_10c[0]));
         ++table_idx) {
        if (sample >= ntc_adc_by_10c[table_idx]) {
            uint16_t span = ntc_adc_by_10c[table_idx - 1U] -
                            ntc_adc_by_10c[table_idx];
            uint16_t offset = ntc_adc_by_10c[table_idx - 1U] - sample;
            uint16_t fraction = (uint16_t)
                (((uint32_t)offset * NTC_TABLE_STEP_C + (span / 2U)) / span);

            return (uint16_t)(((uint16_t)table_idx - 1U) *
                              NTC_TABLE_STEP_C + fraction);
        }
    }

    return (uint16_t)((sizeof(ntc_adc_by_10c) /
                       sizeof(ntc_adc_by_10c[0]) - 1U) * NTC_TABLE_STEP_C);
}

static uint16_t adc_mcu_to_celcius(uint16_t sample)
{
    const uint16_t calibration =
        *(const uint16_t *)MCU_TEMP_CAL1_ADDRESS;
    int32_t delta;
    int32_t temperature;

    delta = (int32_t)calibration - ((int32_t)sample << 2);
    delta *= (int32_t)(ADC_REFERENCE_MV * 10U);

    if (delta >= 0L) {
        delta += MCU_TEMP_SCALE / 2L;
    } else {
        delta -= MCU_TEMP_SCALE / 2L;
    }

    temperature = MCU_TEMP_CAL1_C + (delta / MCU_TEMP_SCALE);

    if (temperature <= 0L) {
        return 0U;
    }

    if (temperature > (int32_t)UINT16_MAX) {
        return UINT16_MAX;
    }

    return (uint16_t)temperature;
}

static void adc_fault(void)
{
    HAL_NVIC_DisableIRQ(ADC1_IRQn);

    for (;;) {
    }
}
