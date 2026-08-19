#pragma once

#include <stdint.h>
#include <stdbool.h>

// Umbrella include for the complete Hot Wand Lite firmware interface.
#include "blink.h"
#if (defined(HOT_WAND_TARGET_XIAO_SAMD21) + defined(HOT_WAND_TARGET_XIAO_RP2040) +                              \
     defined(HOT_WAND_TARGET_WAVESHARE_RP2040_ZERO)) != 1
#error "Select exactly one Hot Wand Lite target"
#endif

#if defined(HOT_WAND_TARGET_XIAO_SAMD21)
#include "pins_samd21.h"
#else
#include "pins_rp2040.h"
#endif
#include "power.h"
#include "rfgen.h"
