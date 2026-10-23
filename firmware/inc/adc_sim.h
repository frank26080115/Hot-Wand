#pragma once

#include "conf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if ADC_SIM_ENABLED

#ifndef ADC_SIM_FIFO_CAPACITY
#define ADC_SIM_FIFO_CAPACITY 128
#endif

#if (ADC_SIM_FIFO_CAPACITY < 2) || (ADC_SIM_FIFO_CAPACITY > 65536) ||                                                  \
    ((ADC_SIM_FIFO_CAPACITY & (ADC_SIM_FIFO_CAPACITY - 1)) != 0)
#error "ADC simulation capacity must be a power of two between 2 and 65536"
#endif

#define ADC_SIM_CHANNEL_COUNT 7
#define ADC_SIM_ABI_VERSION   1

enum
{
    ADC_SIM_WAITING = 0,
    ADC_SIM_ARMED   = 1,
    ADC_SIM_ACTIVE  = 2,
};

/* Codes are raw-count inputs, not engineering units. Channel order is DC, buck, current,
 * NTC1, NTC2, MCU temperature, VREFINT. The reserved byte must be zero and repeat_rounds must
 * be positive. No packed/unaligned words. */
typedef struct
{
    uint8_t  channels[ADC_SIM_CHANNEL_COUNT];
    uint8_t  reserved;
    uint32_t repeat_rounds;
} adc_sim_frame_t;

/* SWD ABI v1, little endian. One producer owns producer_count and unpublished FIFO slots; the
 * ADC ISR owns consumer_count and execution/progress fields. Counters are monotonically
 * increasing uint32_t values, wrapping naturally. queued = (uint32_t)(producer_count -
 * consumer_count), at most capacity. Write complete frames at counter & (capacity - 1), then
 * publish the aligned producer_count word LAST. Never edit a committed slot. Use either the C
 * enqueue API or an SWD producer, not both concurrently. SWD writers halt once around each
 * batch; do not call target functions from GDB.
 *
 * Wait for abi_version == 1 before writing. To start, enqueue then write 1 to start_request;
 * boot clears it after attempting adc_sim_start(). Empty starts leave WAITING. With
 * ARMED/ACTIVE, reset the MCU for another independent test. Empty playback holds decoded
 * values and counts one underrun per episode. Holding values does not stop ADC rounds,
 * filtering, or fault detection. Metadata is initialized in BSS to avoid a flash copy of the
 * large FIFO. */
typedef struct
{
    uint32_t        abi_version;
    uint32_t        capacity;
    uint32_t        frame_size;
    uint32_t        channel_count;
    uint32_t        producer_count;
    uint32_t        consumer_count;
    uint32_t        start_request;
    uint32_t        state;
    uint32_t        rounds_remaining;
    uint32_t        rounds_completed;
    uint32_t        frames_dequeued;
    uint32_t        underruns;
    uint32_t        protocol_errors;
    uint16_t        decoded[ADC_SIM_CHANNEL_COUNT];
    uint16_t        reserved_header;
    adc_sim_frame_t fifo[ADC_SIM_FIFO_CAPACITY];
} adc_sim_state_t;

#ifdef __cplusplus
extern "C"
{
#endif

extern volatile adc_sim_state_t adc_sim;
/* round(341 * (4^(code / 255) - 1)); exact endpoints 0 and 1023. Host encoders choose the
 * nearest table entry, preferring the lower on ties. */
extern const uint16_t adc_sim_decode_table[256];

void adc_sim_init(void);
bool adc_sim_start(void);
bool adc_sim_enqueue(const adc_sim_frame_t* frame);

/* ADC integration only. Call before initial conversions and after processing the old round's
 * final sample. completed_round excludes startup warm-up. Returns true only on activation,
 * requesting fresh ADC signal state. */
bool adc_sim_round_boundary(bool completed_round);

#ifdef __cplusplus
}
#endif

#endif
