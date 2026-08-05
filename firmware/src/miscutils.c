#include "miscutils.h"

static uint32_t hotwand_rand_state = 1U;

static char* milliunits_to_str(uint32_t value, char* str, uint8_t decimal_places);

void hotwand_srand(uint32_t seed)
{
    hotwand_rand_state = seed;
}

uint16_t hotwand_rand(void)
{
    hotwand_rand_state = (hotwand_rand_state * 1103515245UL) + 12345UL;

    return (uint16_t)((hotwand_rand_state >> 16U) & HOTWAND_RAND_MAX);
}

char* int_to_str(int value, char* str, int base)
{
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char*             left;
    char*             right;
    char*             write;
    unsigned int      magnitude;
    int               negative;

    if (str == (char*)0)
    {
        return str;
    }

    if ((base < 2) || (base > 36))
    {
        str[0] = '\0';
        return str;
    }

    negative  = ((value < 0) && (base == 10));
    magnitude = (unsigned int)value;
    if (negative)
    {
        /*
         * Unsigned arithmetic is intentional: it obtains the magnitude of
         * INT_MIN without
         * overflowing a signed int.
         */
        magnitude = 0U - magnitude;
    }

    write = str;
    do
    {
        *write++ = digits[magnitude % (unsigned int)base];
        magnitude /= (unsigned int)base;
    } while (magnitude != 0U);

    if (negative)
    {
        *write++ = '-';
    }
    *write = '\0';

    left  = str;
    right = write - 1;
    while (left < right)
    {
        char temporary = *left;

        *left++  = *right;
        *right-- = temporary;
    }

    return str;
}

char* millivolts_to_str(uint32_t millivolts, char* str, uint8_t decimal_places)
{
    return milliunits_to_str(millivolts, str, decimal_places);
}

char* milliamps_to_str(uint32_t milliamps, char* str, uint8_t decimal_places)
{
    return milliunits_to_str(milliamps, str, decimal_places);
}

char* milliwatts_to_str(uint32_t milliwatts, char* str, uint8_t decimal_places)
{
    return milliunits_to_str(milliwatts, str, decimal_places);
}

char* celcius_to_str(int celcius, char* str)
{
    return int_to_str(celcius, str, 10);
}

uint16_t fletcher16(const uint8_t* data, size_t length)
{
    uint16_t sum1 = 0U;
    uint16_t sum2 = 0U;

    if ((data == NULL) && (length != 0U))
    {
        return 0U;
    }

    while (length != 0U)
    {
        sum1 = (uint16_t)(sum1 + *data++);
        if (sum1 >= 255U)
        {
            sum1 = (uint16_t)(sum1 - 255U);
        }

        sum2 = (uint16_t)(sum2 + sum1);
        if (sum2 >= 255U)
        {
            sum2 = (uint16_t)(sum2 - 255U);
        }

        --length;
    }

    return (uint16_t)((sum2 << 8U) | sum1);
}

static char* milliunits_to_str(uint32_t value, char* str, uint8_t decimal_places)
{
    char     fraction_str[4];
    char*    write;
    uint32_t whole;
    uint32_t remainder;
    uint32_t divisor;
    uint32_t fraction;
    uint8_t  fraction_length;
    uint8_t  shown_places;
    uint8_t  index;

    if (str == (char*)0)
    {
        return str;
    }

    whole        = value / 1000U;
    remainder    = value % 1000U;
    shown_places = (decimal_places < 3U) ? decimal_places : 3U;

    divisor = 1U;
    for (index = shown_places; index < 3U; ++index)
    {
        divisor *= 10U;
    }

    if (shown_places < 3U)
    {
        remainder += divisor / 2U;
        if (remainder >= 1000U)
        {
            ++whole;
            remainder -= 1000U;
        }
    }

    /*
     * UINT32_MAX mill-units has a whole part of only 4294967, so this cast
     * remains within the range
     * accepted by int_to_str().
     */
    int_to_str((int)whole, str, 10);

    if (decimal_places == 0U)
    {
        return str;
    }

    write = str;
    while (*write != '\0')
    {
        ++write;
    }
    *write++ = '.';

    fraction = remainder / divisor;
    int_to_str((int)fraction, fraction_str, 10);

    fraction_length = 0U;
    while (fraction_str[fraction_length] != '\0')
    {
        ++fraction_length;
    }

    for (index = fraction_length; index < shown_places; ++index)
    {
        *write++ = '0';
    }

    for (index = 0U; index < fraction_length; ++index)
    {
        *write++ = fraction_str[index];
    }

    for (index = shown_places; index < decimal_places; ++index)
    {
        *write++ = '0';
    }

    *write = '\0';
    return str;
}
