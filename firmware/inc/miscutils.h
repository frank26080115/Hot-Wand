#ifndef HOT_WAND_MISCUTILS_H
#define HOT_WAND_MISCUTILS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Converts value to a null-terminated string using base 2 through 36.
 * The caller must provide a sufficiently large buffer.  Invalid bases produce
 * an empty string.  Negative values receive a minus sign only in base 10.
 */
char *int_to_str(int value, char *str, int base);

/*
 * Format milli-units as their base units.  Values are rounded when fewer
 * than three decimal places are requested; places beyond three are zero.
 * The caller must provide enough space for the result and terminator.
 */
char *millivolts_to_str(uint32_t millivolts,
                        char *str,
                        uint8_t decimal_places);
char *milliamps_to_str(uint32_t milliamps,
                       char *str,
                       uint8_t decimal_places);
char *milliwatts_to_str(uint32_t milliwatts,
                        char *str,
                        uint8_t decimal_places);

char *celcius_to_str(int celcius, char *str);

#ifdef __cplusplus
}
#endif

#endif
