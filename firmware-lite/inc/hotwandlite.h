#pragma once

#include <stdint.h>
#include <stdbool.h>

// Umbrella include for the complete Hot Wand Lite firmware interface.

#if (defined(HOTWANDLITE_MCU_SAMD21) + defined(HOTWANDLITE_MCU_RP2040) + \
     defined(HOTWANDLITE_MCU_ESP32C3)) != 1
#error "Select exactly one Hot Wand Lite MCU target"
#endif

#if (defined(HOTWANDLITE_TARGET_XIAO_SAMD21) + defined(HOTWANDLITE_TARGET_XIAO_RP2040) +                       \
     defined(HOTWANDLITE_TARGET_WAVESHARE_RP2040_ZERO) + defined(HOTWANDLITE_TARGET_XIAO_ESP32S3) +            \
     defined(HOTWANDLITE_TARGET_XIAO_ESP32C3) + defined(HOTWANDLITE_TARGET_WAVESHARE_ESP32C3_ZERO) +           \
     defined(HOTWANDLITE_TARGET_ESP32C3_SUPERMINI)) != 1
#error "Select exactly one Hot Wand Lite target"
#endif

#if defined(HOTWANDLITE_MCU_SAMD21)
#include "pins_samd21.h"
#elif defined(HOTWANDLITE_MCU_RP2040)
#include "pins_rp2040.h"
#elif defined(HOTWANDLITE_MCU_ESP32S3) || defined(HOTWANDLITE_MCU_ESP32C3)
#include "pins_esp32.h"
#else
#error "The selected target does not have a pin-assignment header"
#endif

#include "blink.h"
#include "power.h"
#include "rfgen.h"
#include "watchdog.h"
