#pragma once

#include <stdbool.h>

/* UART output is disabled after reset until explicitly allowed. */
void UART_SetAllowed(bool allowed);
void UART_Write(const char *text);
/* Call repeatedly; emits one filtered ADC snapshot every 200 ms when allowed. */
void UART_debug_task(void);
