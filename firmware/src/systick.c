#include "systick.h"

#include "stm32f0xx_hal.h"

void systick_init(void)
{
    (void)HAL_InitTick(TICK_INT_PRIORITY);
}

uint32_t systick_get_ms(void)
{
    return HAL_GetTick();
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}
