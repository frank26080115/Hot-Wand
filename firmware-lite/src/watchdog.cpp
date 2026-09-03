/*
 * Cross-platform watchdog support.
 *
 * Feeding is deliberately owned by the application main loop. In particular,
 * do not feed from an ISR: peripheral interrupts can continue running while
 * the foreground application is deadlocked.
 */

#include <Arduino.h>

#include "watchdog.h"

#if defined(HOTWANDLITE_MCU_SAMD21)

namespace
{
void wait_for_samd21_watchdog_sync()
{
    while (WDT->STATUS.bit.SYNCBUSY)
    {
    }
}
} // namespace

void watchdog_init()
{
    // GCLK generator 2 supplies the WDT with its reset-default 1.024 kHz
    // clock. 4096 clocks produce a timeout of approximately four seconds.
    WDT->CTRL.reg = 0;
    wait_for_samd21_watchdog_sync();

    WDT->CONFIG.reg = WDT_CONFIG_PER_4K;
    wait_for_samd21_watchdog_sync();

    WDT->CTRL.reg = WDT_CTRL_ENABLE;
    wait_for_samd21_watchdog_sync();
}

void watchdog_feed()
{
    wait_for_samd21_watchdog_sync();
    WDT->CLEAR.reg = WDT_CLEAR_CLEAR_KEY;
}

#elif defined(HOTWANDLITE_MCU_RP2040)

void watchdog_init()
{
    rp2040.wdt_begin(4000);
}

void watchdog_feed()
{
    rp2040.wdt_reset();
}

#elif defined(HOTWANDLITE_TARGET_XIAO_ESP32S3) || defined(HOTWANDLITE_MCU_ESP32C3)

void watchdog_init()
{
    // Arduino-ESP32 initializes the FreeRTOS task watchdog with a five-second
    // timeout, but does not subscribe the Arduino loop task by default.
    enableLoopWDT();
}

void watchdog_feed()
{
    feedLoopWDT();
}

#else
#error "Watchdog support is missing for the selected Hot Wand Lite target"
#endif
