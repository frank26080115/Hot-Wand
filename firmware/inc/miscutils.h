#pragma once

#include "hotwand.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define HOTWAND_RAND_MAX 32767

void     hotwand_srand(uint32_t seed);
uint16_t hotwand_rand(void);

/*
 * Converts value to a null-terminated string using base 2 through 36.
 * The caller must provide a sufficiently
 * large buffer.  Invalid bases produce
 * an empty string.  Negative values receive a minus sign only in base 10.
 * When length is not NULL, it receives the
 * number of characters excluding
 * the null terminator.
 *
 */
char* int_to_str(int value, char* str, int base, size_t* length);

/*
 * Format milli-units as their base units.  Values are rounded when fewer
 * than three decimal places are
 * requested; places beyond three are zero.
 * The caller must provide enough space for the result and terminator.
 * When length is not NULL, it receives the
 * number of characters excluding
 * the null terminator.
 *
 */
char* millivolts_to_str(uint32_t millivolts, char* str, uint8_t decimal_places, size_t* length);
char* milliamps_to_str(uint32_t milliamps, char* str, uint8_t decimal_places, size_t* length);
char* milliwatts_to_str(uint32_t milliwatts, char* str, uint8_t decimal_places, size_t* length);

char* celcius_to_str(int celcius, char* str, size_t* length);

/* Returns zero if data is NULL while length is nonzero. */
uint16_t fletcher16(const uint8_t* data, size_t length);

#ifdef __cplusplus
}
#endif
