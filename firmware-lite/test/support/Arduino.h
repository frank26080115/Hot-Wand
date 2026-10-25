#pragma once

#include <stdint.h>

enum { LOW = 0, HIGH = 1, INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2, AR_DEFAULT = 0 };
enum { D0, D1, D2, D3, D4, D5, D6, D7, D8, D9, D10, LED_BUILTIN = 13 };

uint32_t millis();
uint32_t micros();
int digitalRead(uint8_t pin);
int analogRead(uint8_t pin);
inline void digitalWrite(uint8_t, int) {}
inline void pinMode(uint8_t, int) {}
inline void analogReference(int) {}
inline void analogReadResolution(int) {}

struct TestSerial
{
    template <typename T> void print(T) {}
    template <typename T> void println(T) {}
};
extern TestSerial Serial;
