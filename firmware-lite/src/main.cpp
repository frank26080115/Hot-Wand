/*
 * Hot Wand Lite application entry point.
 *
 * setup() establishes safe outputs before starting USB serial.
 * loop() keeps the power manager and status indicator responsive without blocking.
 *
 */

// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include <Arduino.h>
#include "hotwandlite.h"

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void setup()
{
    // RF must remain off until the power manager confirms the boot inputs.
    rfgen_set(0);
    blink_init();

    // Serial is the XIAO's USB CDC port; the baud rate is nominal for USB.
    Serial.begin(115200);
}

void loop()
{
    // Run input management first so a newly confirmed pattern can start now.
    pwrmgt_task();
    blink_task();
}
