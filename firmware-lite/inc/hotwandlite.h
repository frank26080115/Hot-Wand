#pragma once

#include <stdint.h>
#include <stdbool.h>

// Umbrella include for the complete Hot Wand Lite firmware interface.
#include "blink.h"
#if defined(HOT_WAND_TARGET_XIAO_SAMD21) && defined(HOT_WAND_TARGET_XIAO_RP2040)
#error "Select exactly one Hot Wand Lite target"
#elif defined(HOT_WAND_TARGET_XIAO_SAMD21)
#include "pins_samd21.h"
#elif defined(HOT_WAND_TARGET_XIAO_RP2040)
#include "pins_rp2040.h"
#else
#error "No Hot Wand Lite target selected"
#endif
#include "power.h"
#include "rfgen.h"
