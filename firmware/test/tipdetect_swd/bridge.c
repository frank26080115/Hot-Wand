/* Compile the actual detector twice, with and without the SWD harness. */
// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "../../src/tipdetect.c"

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

TIM_TypeDef     mock_tim14;
EXTI_TypeDef    mock_exti;
uint32_t        mock_clock, mock_primask, mock_pin, mock_timer_failure, mock_callbacks;
static uint32_t mock_now, mock_stops, mock_faults;

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

__declspec(dllexport) void bridge_reset(void)
{
    /* Volatile byte stores avoid a CRT memset dependency in this native DLL. */
    volatile uint8_t* timer = (volatile uint8_t*)&mock_tim14;
    volatile uint8_t* exti  = (volatile uint8_t*)&mock_exti;
    uint32_t          idx;
    for (idx = 0; idx < sizeof(mock_tim14); ++idx)
    {
        timer[idx] = 0;
    }
    for (idx = 0; idx < sizeof(mock_exti); ++idx)
    {
        exti[idx] = 0;
    }
    mock_clock = mock_primask = mock_timer_failure = mock_callbacks = 0;
    mock_now = mock_stops = mock_faults = 0;
    mock_pin                            = 1;
    tipdetect_initialized               = false;
    tipdetect_tip_present               = false;
    tipdetect_triggered                 = true;
    tipdetect_edge_count = tipdetect_edge_decay_ms = 0;
#if TIP_SWD_TEST_HARNESS
    tip_swd_virtual_present = true;
    tip_swd_test_command    = TIP_SWD_NO_COMMAND;
    tip_swd_test_state      = TIP_SWD_NOT_READY;
    tip_swd_test_completed  = 0;
#endif
}

// -----------------------------------------------------------------------------
// Feature Logic
// -----------------------------------------------------------------------------

__declspec(dllexport) void bridge_init(void)
{
    tipdetect_init();
}

__declspec(dllexport) void bridge_task(void)
{
    tipdetect_task();
}

__declspec(dllexport) void bridge_irq(void)
{
    EXTI4_15_IRQHandler_Impl();
}

__declspec(dllexport) void bridge_reset_latch(void)
{
    tipdetect_reset();
}

__declspec(dllexport) void bridge_expire(void)
{
    SET_BIT(TIM14->SR, TIM_SR_UIF);
    TIM14_IRQHandler_Impl();
}

__declspec(dllexport) void bridge_physical(uint32_t present)
{
    if ((present != mock_pin) && ((present ? EXTI->RTSR : EXTI->FTSR) & TIP_DET_PINn))
    {
        SET_BIT(EXTI->PR, TIP_DET_PINn);
    }
    mock_pin = present;
}

__declspec(dllexport) void bridge_timer_failure(uint32_t phase)
{
    mock_timer_failure = phase;
}

__declspec(dllexport) void bridge_time(uint32_t now)
{
    mock_now = now;
}

__declspec(dllexport) void bridge_mask(uint32_t mask)
{
    mock_primask = mask;
}

__declspec(dllexport) void bridge_button_irq(void)
{
    SET_BIT(EXTI->IMR, GPIO_PIN_7);
    SET_BIT(EXTI->PR, GPIO_PIN_7);
    EXTI4_15_IRQHandler_Impl();
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

void rfgen_stop(void)
{
    ++mock_stops;
}

uint32_t systick_get_ms(void)
{
    return mock_now;
}

void show_fault(const char* text, bool reset)
{
    ++mock_faults;
}

// -----------------------------------------------------------------------------
// Debug / Logging Helpers
// -----------------------------------------------------------------------------

__declspec(dllexport) uint32_t bridge_read(uint32_t selector)
{
    switch (selector)
    {
    case 0:
        return tipdetect_is_present();
    case 1:
        return tipdetect_has_triggered();
    case 2:
        return tipdetect_is_qualifying();
    case 3:
        return mock_stops;
    case 4:
        return tipdetect_edge_count;
    case 5:
        return (EXTI->IMR & TIP_DET_PINn) != 0;
    case 6:
        return (EXTI->PR & TIP_DET_PINn) != 0;
    case 7:
        return mock_callbacks;
    case 8:
        return mock_primask;
    case 9:
        return TIM14->ARR;
    case 10:
        return TIM14->PSC;
    case 11:
        return mock_faults;
    default:
        return UINT32_MAX;
    }
}
