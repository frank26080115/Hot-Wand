// -----------------------------------------------------------------------------
// Includes
// -----------------------------------------------------------------------------

#include "miscutils.h"

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------

static uint32_t hotwand_rand_state = 1;

// -----------------------------------------------------------------------------
// Function Prototypes
// -----------------------------------------------------------------------------

static char* milliunits_to_str(uint32_t value, char* str, uint8_t decimal_places, size_t* length);

// -----------------------------------------------------------------------------
// Utility Functions
// -----------------------------------------------------------------------------

void hotwand_srand(uint32_t seed)
{
    hotwand_rand_state = seed;
}

uint16_t hotwand_rand(void)
{
    hotwand_rand_state = (hotwand_rand_state * 1103515245) + 12345;

    return (uint16_t)((hotwand_rand_state >> 16) & HOTWAND_RAND_MAX);
}

char* int_to_str(int value, char* str, int base, size_t* length)
{
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char*             left;
    char*             right;
    char*             write;
    unsigned int      magnitude;
    int               negative;

    if (str == (char*)0)
    {
        if (length != NULL)
        {
            *length = 0;
        }
        return str;
    }

    if ((base < 2) || (base > 36))
    {
        str[0] = '\0';
        if (length != NULL)
        {
            *length = 0;
        }
        return str;
    }

    negative  = ((value < 0) && (base == 10));
    magnitude = (unsigned int)value;
    if (negative)
    {
        /* Unsigned arithmetic is intentional: it obtains the magnitude of
         * INT_MIN without overflowing a signed int. */
        magnitude = 0 - magnitude;
    }

    write = str;
    do
    {
        *write++ = digits[magnitude % (unsigned int)base];
        magnitude /= (unsigned int)base;
    } while (magnitude != 0);

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

    if (length != NULL)
    {
        *length = (size_t)(write - str);
    }

    return str;
}

char* millivolts_to_str(uint32_t millivolts, char* str, uint8_t decimal_places, size_t* length)
{
    return milliunits_to_str(millivolts, str, decimal_places, length);
}

char* milliamps_to_str(uint32_t milliamps, char* str, uint8_t decimal_places, size_t* length)
{
    return milliunits_to_str(milliamps, str, decimal_places, length);
}

char* milliwatts_to_str(uint32_t milliwatts, char* str, uint8_t decimal_places, size_t* length)
{
    return milliunits_to_str(milliwatts, str, decimal_places, length);
}

char* celcius_to_str(int celcius, char* str, size_t* length)
{
    return int_to_str(celcius, str, 10, length);
}

uint16_t fletcher16(const uint8_t* data, size_t length)
{
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;

    if ((data == NULL) && (length != 0))
    {
        return 0;
    }

    while (length != 0)
    {
        sum1 = (uint16_t)(sum1 + *data++);
        if (sum1 >= 255)
        {
            sum1 = (uint16_t)(sum1 - 255);
        }

        sum2 = (uint16_t)(sum2 + sum1);
        if (sum2 >= 255)
        {
            sum2 = (uint16_t)(sum2 - 255);
        }

        --length;
    }

    return (uint16_t)((sum2 << 8) | sum1);
}

// -----------------------------------------------------------------------------
// Supporting Functions
// -----------------------------------------------------------------------------

static char* milliunits_to_str(uint32_t value, char* str, uint8_t decimal_places, size_t* length)
{
    char     fraction_str[4];
    char*    write;
    uint32_t whole;
    uint32_t remainder;
    uint32_t divisor;
    uint32_t fraction;
    size_t   whole_length;
    size_t   fraction_length;
    size_t   index;
    uint8_t  shown_places;

    if (str == (char*)0)
    {
        if (length != NULL)
        {
            *length = 0;
        }
        return str;
    }

    whole        = value / 1000;
    remainder    = value % 1000;
    shown_places = (decimal_places < 3) ? decimal_places : 3;

    divisor = 1;
    for (index = shown_places; index < 3; ++index)
    {
        divisor *= 10;
    }

    if (shown_places < 3)
    {
        remainder += divisor / 2;
        if (remainder >= 1000)
        {
            ++whole;
            remainder -= 1000;
        }
    }

    /* UINT32_MAX mill-units has a whole part of only 4294967, so this cast
     * remains within the range accepted by int_to_str(). */
    int_to_str((int)whole, str, 10, &whole_length);

    if (decimal_places == 0)
    {
        if (length != NULL)
        {
            *length = whole_length;
        }
        return str;
    }

    write    = str + whole_length;
    *write++ = '.';

    fraction = remainder / divisor;
    int_to_str((int)fraction, fraction_str, 10, &fraction_length);

    for (index = fraction_length; index < shown_places; ++index)
    {
        *write++ = '0';
    }

    for (index = 0; index < fraction_length; ++index)
    {
        *write++ = fraction_str[index];
    }

    for (index = shown_places; index < decimal_places; ++index)
    {
        *write++ = '0';
    }

    *write = '\0';
    if (length != NULL)
    {
        *length = (size_t)(write - str);
    }
    return str;
}
