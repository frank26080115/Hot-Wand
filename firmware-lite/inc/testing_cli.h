#pragma once

#include <stdint.h>

// Register all test commands after the USB Serial port has been started.
void cli_init();

// Poll the CLI and service active tests. Returns true only when an exclusive
// test has taken permanent ownership of loop(); simulation overrides leave the
// normal application running.
bool testing_task();

// Return true and copy the reset-persistent CLI override when it is enabled.
bool testing_get_simulated_mode(uint8_t* modeNumber);
bool testing_get_simulated_voltage_mv(uint32_t* voltageMv);
