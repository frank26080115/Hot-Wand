/* Minimal CRT substitutes for no-entry DLLs. No MCU or host hardware access. */
#include <stddef.h>
void* memset(void* destination, int value, size_t count)
{
    volatile unsigned char* bytes = destination;
    for (size_t i = 0; i < count; ++i)
    {
        bytes[i] = value;
    }
    return destination;
}
void* memcpy(void* destination, const void* source, size_t count)
{
    unsigned char*       out = destination;
    const unsigned char* in  = source;
    for (size_t i = 0; i < count; ++i)
    {
        out[i] = in[i];
    }
    return destination;
}
size_t strlen(const char* text)
{
    size_t count = 0;
    while (text[count])
    {
        ++count;
    }
    return count;
}
