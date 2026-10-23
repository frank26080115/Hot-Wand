/* ADC-clocked, single-producer simulation queue. See adc_sim.h for the SWD ABI. */
// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "adc_sim.h"
#include "fault_test.h"

#if ADC_SIM_ENABLED

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

/* Volatile accesses plus a compiler publication barrier suffice on this single-core, uncached
 * Cortex-M0. SWD batches are written while halted. */
#define ADC_SIM_PUBLISH_BARRIER() __asm__ volatile("" ::: "memory")
#if defined(__GNUC__) && !defined(__clang__)
#define ADC_SIM_VISIBLE __attribute__((used, externally_visible))
#else
#define ADC_SIM_VISIBLE __attribute__((used))
#endif

_Static_assert(sizeof(adc_sim_frame_t) == 12, "ADC simulation frame ABI changed");
_Static_assert(offsetof(adc_sim_frame_t, repeat_rounds) == 8, "repeat word must be aligned");
_Static_assert(offsetof(adc_sim_state_t, decoded) == 52, "ADC simulation status ABI changed");
_Static_assert(offsetof(adc_sim_state_t, fifo) == 68, "ADC simulation FIFO ABI changed");

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

/* Uninitialized storage is intentional: the FIFO must not occupy flash too. */
volatile adc_sim_state_t adc_sim ADC_SIM_VISIBLE;
const uint16_t                   adc_sim_decode_table[256] ADC_SIM_VISIBLE = {
    0,   2,   4,   6,   7,   9,   11,  13,  15,  17,  19,   21,   23,   25,   27,  29,  31,  33,  35,  37,  39,  41,
    43,  45,  48,  50,  52,  54,  56,  58,  60,  63,  65,   67,   69,   71,   74,  76,  78,  81,  83,  85,  87,  90,
    92,  95,  97,  99,  102, 104, 107, 109, 111, 114, 116,  119,  121,  124,  126, 129, 132, 134, 137, 139, 142, 145,
    147, 150, 153, 155, 158, 161, 163, 166, 169, 172, 174,  177,  180,  183,  186, 189, 192, 194, 197, 200, 203, 206,
    209, 212, 215, 218, 221, 224, 227, 231, 234, 237, 240,  243,  246,  249,  253, 256, 259, 262, 266, 269, 272, 276,
    279, 282, 286, 289, 293, 296, 300, 303, 307, 310, 314,  317,  321,  325,  328, 332, 335, 339, 343, 347, 350, 354,
    358, 362, 366, 369, 373, 377, 381, 385, 389, 393, 397,  401,  405,  409,  413, 417, 421, 426, 430, 434, 438, 442,
    447, 451, 455, 460, 464, 468, 473, 477, 482, 486, 491,  495,  500,  504,  509, 514, 518, 523, 528, 532, 537, 542,
    547, 552, 556, 561, 566, 571, 576, 581, 586, 591, 596,  601,  607,  612,  617, 622, 627, 633, 638, 643, 649, 654,
    660, 665, 670, 676, 682, 687, 693, 698, 704, 710, 715,  721,  727,  733,  739, 745, 750, 756, 762, 768, 774, 781,
    787, 793, 799, 805, 811, 818, 824, 830, 837, 843, 850,  856,  863,  869,  876, 882, 889, 896, 903, 909, 916, 923,
    930, 937, 944, 951, 958, 965, 972, 979, 986, 994, 1001, 1008, 1016, 1023,
};
static bool adc_sim_empty_episode;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static bool adc_sim_take_frame(void);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool adc_sim_round_boundary(bool completed_round)
{
    if (adc_sim.state == ADC_SIM_ARMED)
    {
        if (!adc_sim_take_frame())
        {
            adc_sim.state = ADC_SIM_WAITING;
            return false;
        }
        adc_sim.state = ADC_SIM_ACTIVE;
        return true;
    }
    if ((adc_sim.state != ADC_SIM_ACTIVE) || !completed_round)
    {
        return false;
    }

    ++adc_sim.rounds_completed;
    if (adc_sim.rounds_remaining > 0)
    {
        --adc_sim.rounds_remaining;
    }
    if ((adc_sim.rounds_remaining == 0) && !adc_sim_take_frame() && !adc_sim_empty_episode)
    {
        ++adc_sim.underruns;
        adc_sim_empty_episode = true;
    }
    return false;
}

ADC_SIM_VISIBLE bool __attribute__((noinline)) adc_sim_enqueue(const adc_sim_frame_t* frame)
{
    uint32_t producer;
    uint32_t slot;
    uint8_t  idx;

    adc_sim_init();
    if ((frame == NULL) || (frame->repeat_rounds == 0) || (frame->reserved != 0))
    {
        return false;
    }

    producer = adc_sim.producer_count;
    if ((uint32_t)(producer - adc_sim.consumer_count) >= ADC_SIM_FIFO_CAPACITY)
    {
        return false;
    }

    slot = producer & (ADC_SIM_FIFO_CAPACITY - 1);
    for (idx = 0; idx < ADC_SIM_CHANNEL_COUNT; ++idx)
    {
        adc_sim.fifo[slot].channels[idx] = frame->channels[idx];
    }
    adc_sim.fifo[slot].reserved      = 0;
    adc_sim.fifo[slot].repeat_rounds = frame->repeat_rounds;
    ADC_SIM_PUBLISH_BARRIER();
    adc_sim.producer_count = producer + 1;
    return true;
}

ADC_SIM_VISIBLE bool __attribute__((noinline)) adc_sim_start(void)
{
    uint32_t queued;
    uint32_t slot;

    adc_sim_init();
    if (adc_sim.state != ADC_SIM_WAITING)
    {
        return true;
    }

    queued = (uint32_t)(adc_sim.producer_count - adc_sim.consumer_count);
    slot   = adc_sim.consumer_count & (ADC_SIM_FIFO_CAPACITY - 1);
    if ((queued == 0) || (queued > ADC_SIM_FIFO_CAPACITY) || (adc_sim.fifo[slot].repeat_rounds == 0) ||
        (adc_sim.fifo[slot].reserved != 0))
    {
        return false;
    }

    /* Publish only a request here. The ISR switches sources at a boundary, never halfway through
 * processing the current physical/simulated round. */
    ADC_SIM_PUBLISH_BARRIER();
    adc_sim.state = ADC_SIM_ARMED;
    return true;
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static bool adc_sim_take_frame(void)
{
    uint32_t consumer = adc_sim.consumer_count;
    uint32_t queued   = (uint32_t)(adc_sim.producer_count - consumer);
    uint32_t slot     = consumer & (ADC_SIM_FIFO_CAPACITY - 1);
    uint32_t repeats;
    uint8_t  idx;

    if (queued == 0)
    {
        return false;
    }
    if (queued > ADC_SIM_FIFO_CAPACITY)
    {
        ++adc_sim.protocol_errors;
        return false;
    }
    repeats = adc_sim.fifo[slot].repeat_rounds;
    if ((repeats == 0) || (adc_sim.fifo[slot].reserved != 0))
    {
        /* A malformed SWD frame is consumed without changing the last valid values. This prevents
 * underflow or a permanently blocked queue. */
        ++adc_sim.protocol_errors;
        adc_sim.consumer_count = consumer + 1;
        return false;
    }

    for (idx = 0; idx < ADC_SIM_CHANNEL_COUNT; ++idx)
    {
        adc_sim.decoded[idx] = adc_sim_decode_table[adc_sim.fifo[slot].channels[idx]];
    }
    adc_sim.rounds_remaining = repeats;
    ++adc_sim.frames_dequeued;
    adc_sim_empty_episode = false;
    ADC_SIM_PUBLISH_BARRIER();
    if (fault_test_inject(FT_MARK))
    {
        FT_SET(qualify_ms, adc_sim.rounds_completed); /* Actual waveform activation round. */
    }
    adc_sim.consumer_count = consumer + 1; /* Free only after copying/decoding. */
    return true;
}

void adc_sim_init(void)
{
    if (adc_sim.abi_version != 0)
    {
        return;
    }
    adc_sim.capacity      = ADC_SIM_FIFO_CAPACITY;
    adc_sim.frame_size    = sizeof(adc_sim_frame_t);
    adc_sim.channel_count = ADC_SIM_CHANNEL_COUNT;
    adc_sim_empty_episode = false;
    ADC_SIM_PUBLISH_BARRIER();
    adc_sim.abi_version = ADC_SIM_ABI_VERSION; /* Publish readiness last. */
}

#endif
