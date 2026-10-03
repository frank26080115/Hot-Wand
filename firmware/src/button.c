/*
 * Handles button presses and detects press, release, and long-hold events using
 * interrupts and debouncing.
 */

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

/* EXTI4_15 contains the safety-critical tip detector, so every user of the
 * shared IRQ must preserve the tip detector's highest maskable priority. */
#define BTN_EXTI_IRQ_PRIORITY 0

#ifndef BTN_SWD_TEST_HARNESS
#define BTN_SWD_TEST_HARNESS 0
#endif

#if (BTN_SWD_TEST_HARNESS != 0) && (BTN_SWD_TEST_HARNESS != 1)
#error "BTN_SWD_TEST_HARNESS must be 0 or 1"
#endif

#if BTN_SWD_TEST_HARNESS
/* The host writes one aligned word through SWD. UINT32_MAX means no command;
 * zero releases the automatic boot hold; a positive value requests a press
 * lasting that many firmware milliseconds. Write only when state is IDLE. */
#define BTN_SWD_NO_COMMAND UINT32_MAX
#define BTN_SWD_BOOT_HELD  0
#define BTN_SWD_IDLE       1
#define BTN_SWD_DOWN       2
#define BTN_SWD_RELEASING  3
#endif

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
static volatile uint32_t               btn_last_short_press_ms;
static volatile uint32_t               btn_consecutive_presses;
static volatile btn_short_press_mode_t btn_short_press_mode = BTN_SHORT_PRESS_ON_PRESS;

#if BTN_SWD_TEST_HARNESS
/* Externally visible, volatile words remain addressable through the ELF's
 * symbols even with LTO. The harness build enters setup with a held button. */
volatile uint32_t btn_swd_test_command_ms __attribute__((used, externally_visible)) = BTN_SWD_NO_COMMAND;
volatile uint32_t btn_swd_test_state __attribute__((used, externally_visible))      = BTN_SWD_BOOT_HELD;
volatile uint32_t btn_swd_test_completed __attribute__((used, externally_visible));

static uint32_t btn_swd_press_started_ms;
static uint32_t btn_swd_press_duration_ms;
static bool     btn_swd_virtual_down = true;
#endif

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static void btn_accept_down(uint32_t now);
static void btn_accept_release(void);
static void btn_record_short_press(uint32_t now);
static bool btn_get_event(volatile bool* event, bool clear_flag);
#if BTN_SWD_TEST_HARNESS
static void btn_swd_task(uint32_t now);
#endif

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

    /* SW1 shorts PA7 to ground. The schematic has no external pull-up, so use
     * the MCU pull-up and interrupt on both press and release edges. */
    gpio_cfg.Pin   = BTN_PINn;
    gpio_cfg.Mode  = GPIO_MODE_IT_RISING_FALLING;
    gpio_cfg.Pull  = GPIO_PULLUP;
    gpio_cfg.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BTN_GPIOx, &gpio_cfg);

    __HAL_GPIO_EXTI_CLEAR_IT(BTN_PINn);

    now                     = systick_get_ms();
#if BTN_SWD_TEST_HARNESS
    /* Every reset begins with a synthetic held button. A zero command from
     * SWD releases it after the setup screen has appeared. */
    btn_swd_test_command_ms = BTN_SWD_NO_COMMAND;
    btn_swd_test_state      = BTN_SWD_BOOT_HELD;
    btn_swd_test_completed  = 0;
    btn_swd_virtual_down    = true;
    btn_down                = true;
#else
    btn_down                = (HAL_GPIO_ReadPin(BTN_GPIOx, BTN_PINn) == GPIO_PIN_RESET);
#endif
    btn_release_pending     = false;
    btn_short_press         = false;
    btn_long_press          = false;
    btn_long_press_emitted  = false;
    btn_down_since_ms       = now;
    btn_release_since_ms    = now;
    btn_last_short_press_ms = now;
    btn_consecutive_presses = 0;
    btn_initialized         = true;

    /* PA7 shares EXTI4_15 with tip detection. Use the same priority and do not
     * clear the shared NVIC pending bit, which could discard a tip edge. */
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

#if BTN_SWD_TEST_HARNESS
    btn_swd_task(now);
    pin_is_down = btn_swd_virtual_down;
#else
    pin_is_down = (HAL_GPIO_ReadPin(BTN_GPIOx, BTN_PINn) == GPIO_PIN_RESET);
#endif

    if (btn_down && btn_release_pending)
    {
        if (pin_is_down)
        {
            /* The falling-edge callback may still be pending while this task
             * owns the critical section. Preserve a new press if the high
             * interval already qualified as a real release; otherwise cancel
             * it as bounce. */
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
        btn_long_press          = true;
        btn_long_press_emitted  = true;
        btn_consecutive_presses = 0;
    }

    if (interrupt_state == 0)
    {
        __enable_irq();
    }
}

/* The EXTI4_15 handler implementation lives in tipdetect.c because PA5 and PA7
 * share that vector. Its non-tip dispatch calls this standard HAL hook. */
void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    bool     pin_is_down;
    uint32_t now;

    if (!btn_initialized || (gpio_pin != BTN_PINn))
    {
        return;
    }

#if BTN_SWD_TEST_HARNESS
    /* The test image takes button edges only from the SWD-driven virtual pin.
     * Physical EXTI traffic must not disturb its state machine. */
    return;
#endif

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
            /* If the high interval was long enough, it was a real release and
             * this is a new press. Otherwise it was contact bounce belonging
             * to the existing hold. */
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

#if BTN_SWD_TEST_HARNESS
    return btn_swd_virtual_down;
#else
    return HAL_GPIO_ReadPin(BTN_GPIOx, BTN_PINn) == GPIO_PIN_RESET;
#endif
}

bool btn_has_short_press(bool clear_flag)
{
    return btn_get_event(&btn_short_press, clear_flag);
}

bool btn_has_long_press(bool clear_flag)
{
    return btn_get_event(&btn_long_press, clear_flag);
}

uint32_t btn_get_consecutive_presses(void)
{
    uint32_t count;
    uint32_t interrupt_state;
    uint32_t now;

    interrupt_state = __get_PRIMASK();
    __disable_irq();

    now = systick_get_ms();
    if ((btn_consecutive_presses != 0) &&
        ((uint32_t)(now - btn_last_short_press_ms) > BTN_CONSECUTIVE_PRESS_TIMEOUT_MS))
    {
        btn_consecutive_presses = 0;
    }
    count = btn_consecutive_presses;

    if (interrupt_state == 0)
    {
        __enable_irq();
    }

    return count;
}

void btn_reset_consecutive_presses(void)
{
    uint32_t interrupt_state;

    interrupt_state = __get_PRIMASK();
    __disable_irq();
    btn_consecutive_presses = 0;
    if (interrupt_state == 0)
    {
        __enable_irq();
    }
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

#if BTN_SWD_TEST_HARNESS
static void btn_swd_task(uint32_t now)
{
    uint32_t command_ms;

    if (btn_swd_test_state == BTN_SWD_BOOT_HELD)
    {
        if (btn_swd_test_command_ms == 0)
        {
            btn_swd_test_command_ms = BTN_SWD_NO_COMMAND;
            btn_swd_virtual_down    = false;
            btn_release_since_ms    = now;
            btn_release_pending     = true;
            btn_swd_test_state      = BTN_SWD_RELEASING;
        }
        return;
    }

    if (btn_swd_test_state == BTN_SWD_DOWN)
    {
        if ((uint32_t)(now - btn_swd_press_started_ms) >= btn_swd_press_duration_ms)
        {
            btn_swd_virtual_down = false;
            btn_release_since_ms = now;
            btn_release_pending  = true;
            btn_swd_test_state   = BTN_SWD_RELEASING;
        }
        return;
    }

    if (btn_swd_test_state == BTN_SWD_RELEASING)
    {
        /* btn_task() completes the ordinary release debounce below. */
        if (!btn_down && !btn_release_pending)
        {
            btn_swd_test_state = BTN_SWD_IDLE;
            ++btn_swd_test_completed;
        }
        return;
    }

    if (btn_swd_test_state != BTN_SWD_IDLE)
    {
        return;
    }

    command_ms = btn_swd_test_command_ms;
    if (command_ms == BTN_SWD_NO_COMMAND)
    {
        return;
    }

    btn_swd_test_command_ms = BTN_SWD_NO_COMMAND;
    if (command_ms == 0)
    {
        return;
    }

    /* Drive the normal edge and duration logic. The host supplies a duration,
     * but all event timing and release debounce run on the target's tick. */
    btn_swd_press_duration_ms = command_ms;
    btn_swd_press_started_ms  = now;
    btn_swd_virtual_down      = true;
    btn_accept_down(now);
    btn_swd_test_state = BTN_SWD_DOWN;
}
#endif

static void btn_accept_down(uint32_t now)
{
    btn_down               = true;
    btn_release_pending    = false;
    btn_down_since_ms      = now;
    btn_long_press_emitted = false;

    if (btn_short_press_mode == BTN_SHORT_PRESS_ON_PRESS)
    {
        btn_record_short_press(now);
    }
}

static void btn_accept_release(void)
{
    if ((btn_short_press_mode == BTN_SHORT_PRESS_ON_RELEASE) && !btn_long_press_emitted &&
        ((uint32_t)(btn_release_since_ms - btn_down_since_ms) < BTN_LONG_PRESS_MS))
    {
        btn_record_short_press(btn_release_since_ms);
    }

    btn_down               = false;
    btn_release_pending    = false;
    btn_long_press_emitted = false;
}

static void btn_record_short_press(uint32_t now)
{
    if ((btn_consecutive_presses == 0) ||
        ((uint32_t)(now - btn_last_short_press_ms) > BTN_CONSECUTIVE_PRESS_TIMEOUT_MS))
    {
        btn_consecutive_presses = 1;
    }
    else if (btn_consecutive_presses < UINT32_MAX)
    {
        btn_consecutive_presses++;
    }

    btn_last_short_press_ms = now;
    btn_short_press         = true;
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
