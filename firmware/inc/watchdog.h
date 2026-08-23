#pragma once

#include <stdbool.h>

/* Start the independent watchdog. Call once after forcing the RF output low. */
bool watchdog_init(void);

/* Prove that the foreground application completed a healthy iteration. */
void watchdog_feed(void);
