/*
 * Independent watchdog support for the STM32F0 targets.
 *
 * Feeding is deliberately owned by foreground application loops. Never feed
 * from an ISR: interrupts may continue running while foreground code is stuck.
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "watchdog.h"

#include "stm32f0xx_hal.h"

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#if !defined(HOT_WAND_TARGET_STM32F030) && !defined(HOT_WAND_TARGET_STM32F042)
#error "Watchdog support is missing for the selected Hot Wand target"
#endif

/*
 * 2500 counts at LSI / 64 is nominally four seconds. The STM32F042's
 * specified 30-50 kHz LSI range makes the actual timeout 3.2-5.33 seconds.
 */
#define WATCHDOG_RELOAD 2499

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static IWDG_HandleTypeDef watchdog_handle;

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

bool watchdog_init(void)
{
    /* DBGMCU freeze bits survive system reset. Safety takes priority over
     * breakpoint convenience, so explicitly keep IWDG running while halted. */
    __HAL_RCC_DBGMCU_CLK_ENABLE();
    __HAL_DBGMCU_UNFREEZE_IWDG();

    watchdog_handle.Instance       = IWDG;
    watchdog_handle.Init.Prescaler = IWDG_PRESCALER_64;
    watchdog_handle.Init.Reload    = WATCHDOG_RELOAD;
    watchdog_handle.Init.Window    = IWDG_WINDOW_DISABLE;

    return HAL_IWDG_Init(&watchdog_handle) == HAL_OK;
}

void watchdog_feed(void)
{
    HAL_IWDG_Refresh(&watchdog_handle);
}
