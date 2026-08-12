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
    // RF must remain off until the power manager confirms the boot inputs.
    rfgen_set(RFGEN_POWER_OFF);
    blink_init();

    // Serial is the XIAO's USB CDC port; the baud rate is nominal for USB.
    Serial.begin(115200);
}

void loop()
{
    // Confirm power-selection inputs and update the status indication.
    pwrmgt_task();
    blink_task();
}
