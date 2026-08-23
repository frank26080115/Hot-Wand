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
#include "testing_cli.h"

// -----------------------------------------------------------------------------
// Main Flow
// -----------------------------------------------------------------------------

void setup()
{
    // RF must remain off until the power manager's startup delay expires.
    rfgen_set(0);
    // for safety concerns, driving the RF generator pin low has the highest priority

    // Serial is the XIAO's USB CDC port; the baud rate is nominal for USB.
    Serial.begin(115200);

    blink_init();

    cli_init();
    watchdog_init();
}

void loop()
{
    // Feed before every possible return path, including exclusive test mode.
    watchdog_feed();

    // A started test owns the application until the board is reset.
    if (testing_task())
    {
        return;
    }

    // Apply power-selection inputs and update the status indication.
    pwrmgt_task();
    blink_task();
}
