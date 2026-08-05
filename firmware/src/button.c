// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "button.h"

#include "pins.h"
#include "stm32f0xx_hal.h"
#include "systick.h"

#include <stdbool.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define BTN_EXTI_IRQ_PRIORITY 3

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static volatile bool                   btn_initialized;
static volatile bool                   btn_down;
static volatile bool                   btn_release_pending;
static volatile bool                   btn_short_press;
static volatile bool                   btn_long_press;
static volatile bool                   btn_long_press_emitted;
static volatile uint32_t               btn_down_since_ms;
static volatile uint32_t               btn_release_since_ms;
static volatile btn_short_press_mode_t btn_short_press_mode = BTN_SHORT_PRESS_ON_PRESS;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void btn_accept_down(uint32_t now);
static void btn_accept_release(void);
static bool btn_get_event(volatile bool* event, bool clear_flag);

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void btn_init(void)
{
    GPIO_InitTypeDef gpio_cfg = {0};
    uint32_t         interrupt_state;
    uint32_t         now;

    if (btn_initialized)
    {
        return;
    }

    interrupt_state = __get_PRIMASK();
    __disable_irq();

    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*
     * SW1 shorts PA7 to ground.  The schematic has no external pull-up, so
     * use the MCU pull-up and
     * interrupt on both press and release edges.
     */
    gpio_cfg.Pin   = BTN_PINn;
    gpio_cfg.Mode  = GPIO_MODE_IT_RISING_FALLING;
    gpio_cfg.Pull  = GPIO_PULLUP;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BTN_GPIOx, &gpio_cfg);

    __HAL_GPIO_EXTI_CLEAR_IT(BTN_PINn);

    now                    = systick_get_ms();
    btn_down               = (HAL_GPIO_ReadPin(BTN_GPIOx, BTN_PINn) == GPIO_PIN_RESET);
    btn_release_pending    = false;
    btn_short_press        = false;
    btn_long_press         = false;
    btn_long_press_emitted = false;
    btn_down_since_ms      = now;
    btn_release_since_ms   = now;
    btn_initialized        = true;

    /*
     * PA7 shares EXTI4_15 with tip detection.  Use the same priority and do
     * not clear the shared NVIC
     * pending bit, which could discard a tip edge.
     */
    HAL_NVIC_SetPriority(EXTI4_15_IRQn, BTN_EXTI_IRQ_PRIORITY, 0);
    HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

    if (interrupt_state == 0)
    {
        __enable_irq();
    }
}

void btn_task(void)
{
    bool     pin_is_down;
    uint32_t interrupt_state;
    uint32_t now;

    if (!btn_initialized)
    {
        return;
    }

    interrupt_state = __get_PRIMASK();
    __disable_irq();
    now = systick_get_ms();

    pin_is_down = (HAL_GPIO_ReadPin(BTN_GPIOx, BTN_PINn) == GPIO_PIN_RESET);

    if (btn_down && btn_release_pending)
    {
        if (pin_is_down)
        {
            /*
             * The falling-edge callback may still be pending while this task
             * owns the
             * critical section. Preserve a new press if the high
             * interval already qualified as a real
             * release; otherwise cancel
             * it as bounce.
             */
            if ((uint32_t)(now - btn_release_since_ms) >= BTN_DEBOUNCE_MS)
            {
                btn_accept_release();
                btn_accept_down(now);
            }
            else
            {
                btn_release_pending = false;
            }
        }
        else if ((uint32_t)(now - btn_release_since_ms) >= BTN_DEBOUNCE_MS)
        {
            btn_accept_release();
        }
    }

    if (btn_down && !btn_release_pending && pin_is_down && !btn_long_press_emitted &&
        ((uint32_t)(now - btn_down_since_ms) >= BTN_LONG_PRESS_MS))
    {
        btn_long_press         = true;
        btn_long_press_emitted = true;
    }

    if (interrupt_state == 0)
    {
        __enable_irq();
    }
}

/*
 * The EXTI4_15 handler implementation lives in tipdetect.c because PA5 and
 * PA7 share that vector. Its non-tip
 * dispatch calls this standard HAL hook.
 */
void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    bool     pin_is_down;
    uint32_t now;

    if (!btn_initialized || (gpio_pin != BTN_PINn))
    {
        return;
    }

    now         = systick_get_ms();
    pin_is_down = (HAL_GPIO_ReadPin(BTN_GPIOx, BTN_PINn) == GPIO_PIN_RESET);

    if (pin_is_down)
    {
        if (!btn_down)
        {
            btn_accept_down(now);
        }
        else if (btn_release_pending)
        {
            /*
             * If the high interval was long enough, it was a real release and
             * this is a
             * new press.  Otherwise it was contact bounce belonging
             * to the existing hold.
             */
            if ((uint32_t)(now - btn_release_since_ms) >= BTN_DEBOUNCE_MS)
            {
                btn_accept_release();
                btn_accept_down(now);
            }
            else
            {
                btn_release_pending = false;
            }
        }
    }
    else if (btn_down && !btn_release_pending)
    {
        btn_release_since_ms = now;
        btn_release_pending  = true;
    }
}

// -----------------------------------------------------------------------------
// Getters and Setters
// -----------------------------------------------------------------------------

void btn_set_short_press_mode(btn_short_press_mode_t mode)
{
    uint32_t interrupt_state;

    if (mode != BTN_SHORT_PRESS_ON_RELEASE)
    {
        mode = BTN_SHORT_PRESS_ON_PRESS;
    }

    interrupt_state = __get_PRIMASK();
    __disable_irq();
    btn_short_press_mode = mode;
    if (interrupt_state == 0)
    {
        __enable_irq();
    }
}

bool btn_is_down(void)
{
    if (!btn_initialized)
    {
        return false;
    }

    return HAL_GPIO_ReadPin(BTN_GPIOx, BTN_PINn) == GPIO_PIN_RESET;
}

bool btn_has_short_press(bool clear_flag)
{
    return btn_get_event(&btn_short_press, clear_flag);
}

bool btn_has_long_press(bool clear_flag)
{
    return btn_get_event(&btn_long_press, clear_flag);
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static void btn_accept_down(uint32_t now)
{
    btn_down               = true;
    btn_release_pending    = false;
    btn_down_since_ms      = now;
    btn_long_press_emitted = false;

    if (btn_short_press_mode == BTN_SHORT_PRESS_ON_PRESS)
    {
        btn_short_press = true;
    }
}

static void btn_accept_release(void)
{
    if ((btn_short_press_mode == BTN_SHORT_PRESS_ON_RELEASE) && !btn_long_press_emitted &&
        ((uint32_t)(btn_release_since_ms - btn_down_since_ms) < BTN_LONG_PRESS_MS))
    {
        btn_short_press = true;
    }

    btn_down               = false;
    btn_release_pending    = false;
    btn_long_press_emitted = false;
}

static bool btn_get_event(volatile bool* event, bool clear_flag)
{
    bool     occurred;
    uint32_t interrupt_state;

    if (event == NULL)
    {
        return false;
    }

    interrupt_state = __get_PRIMASK();
    __disable_irq();

    occurred = *event;
    if (clear_flag)
    {
        *event = false;
    }

    if (interrupt_state == 0)
    {
        __enable_irq();
    }

    return occurred;
}
