#pragma once

// Start the target's watchdog. Call once after application initialization.
void watchdog_init();

// Prove that the main loop is still making progress.
void watchdog_feed();
