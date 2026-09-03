#pragma once

#include <stdint.h>
#include <stdbool.h>

// Umbrella include for the complete Hot Wand Lite firmware interface.
#include "blink.h"
#if (defined(HOT_WAND_TARGET_XIAO_SAMD21) + defined(HOT_WAND_TARGET_XIAO_RP2040) +                             \
     defined(HOT_WAND_TARGET_WAVESHARE_RP2040_ZERO) + defined(HOT_WAND_TARGET_XIAO_ESP32S3) +                  \
     defined(HOT_WAND_TARGET_XIAO_ESP32C3) + defined(HOT_WAND_TARGET_WAVESHARE_ESP32C3_ZERO) +                 \
     defined(HOT_WAND_TARGET_ESP32C3_SUPERMINI)) != 1
#error "Select exactly one Hot Wand Lite target"
#endif

#if defined(HOT_WAND_TARGET_XIAO_SAMD21)
#include "pins_samd21.h"
#elif defined(HOT_WAND_TARGET_RP2040)
#include "pins_rp2040.h"
#elif defined(HOT_WAND_TARGET_XIAO_ESP32S3) || defined(HOT_WAND_TARGET_ESP32C3)
#include "pins_esp32.h"
#else
#error "The selected target does not have a pin-assignment header"
#endif
#include "power.h"
#include "rfgen.h"
#include "watchdog.h"
