#pragma once

#include <stdbool.h>

/* UART output is disabled after reset until explicitly allowed. */
void UART_SetAllowed(bool allowed);
void UART_Write(const char *text);
