/*
 * Hot Wand Lite application entry point.
 *
 * setup() establishes safe outputs before starting USB serial.
 * loop() keeps the power manager and status indicator responsive.
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
    // RF must remain off until the power manager's startup delay expires.
    rfgen_set(0);
    blink_init();

    // Serial is the XIAO's USB CDC port; the baud rate is nominal for USB.
    Serial.begin(115200);
}

void loop()
{
    // Apply power-selection inputs and update the status indication.
    pwrmgt_task();
    blink_task();
}
