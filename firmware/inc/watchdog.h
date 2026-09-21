#pragma once

#include <stdbool.h>

#define TEST_WATCHDOG_ENABLED                 1

#if (TEST_WATCHDOG_ENABLED != 0) && (TEST_WATCHDOG_ENABLED != 1)
#error "TEST_WATCHDOG_ENABLED must be 0 or 1"
#endif

/* Keep the switch local to the test harness. With the watchdog disabled,
 * existing test setup and foreground-feed calls become harmless no-ops. */
#if !TEST_WATCHDOG_ENABLED
#define watchdog_init() (true)
#define watchdog_feed() ((void)0)
#else
/* Start the independent watchdog. Call once after forcing the RF output low. */
bool watchdog_init(void);

/* Prove that the foreground application completed a healthy iteration. */
void watchdog_feed(void);
#endif