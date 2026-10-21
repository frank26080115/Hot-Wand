#pragma once

// Select 1 or 2, or -1 for V1 reworked for the XIAO ESP32-C3.
// A build flag can select the rework without changing the default for other builds.
#ifndef HOTWANDLITE_PCB_VERSION
#define HOTWANDLITE_PCB_VERSION 1
#endif

#if defined(HOTWANDLITE_TARGET_XIAO_ESP32S3) || defined(HOTWANDLITE_TARGET_WAVESHARE_ESP32C3_ZERO)
#error "ESP32-S3 and Waveshare ESP32-C3-Zero are not supported"
#endif

#if (defined(HOTWANDLITE_MCU_SAMD21) + defined(HOTWANDLITE_MCU_RP2040) + \
     defined(HOTWANDLITE_MCU_ESP32C3)) != 1
#error "Select exactly one Hot Wand Lite MCU target"
#endif

#if (defined(HOTWANDLITE_TARGET_XIAO_SAMD21) + defined(HOTWANDLITE_TARGET_XIAO_RP2040) + \
     defined(HOTWANDLITE_TARGET_WAVESHARE_RP2040_ZERO) +            \
     defined(HOTWANDLITE_TARGET_XIAO_ESP32C3) +           \
     defined(HOTWANDLITE_TARGET_ESP32C3_SUPERMINI)) != 1
#error "Select exactly one Hot Wand Lite target"
#endif

#if HOTWANDLITE_PCB_VERSION == -1
#if defined(HOTWANDLITE_TARGET_XIAO_ESP32C3)
#include "pins_pcb_vminus1/pins_esp32.h"
#else
#error "PCB version -1 requires the reworked V1 wiring and XIAO ESP32-C3"
#endif
#elif HOTWANDLITE_PCB_VERSION == 1
#if defined(HOTWANDLITE_MCU_SAMD21)
#include "pins_pcb_v1/pins_samd21.h"
#elif defined(HOTWANDLITE_MCU_RP2040)
#include "pins_pcb_v1/pins_rp2040.h"
#elif defined(HOTWANDLITE_MCU_ESP32C3)
//#include "pins_pcb_v1/pins_esp32.h"
#error ESP32 UNSUPPORTED ON PCB V1
#else
#error "The selected target does not have a pin-assignment header"
#endif
#elif HOTWANDLITE_PCB_VERSION == 2
#if defined(HOTWANDLITE_MCU_SAMD21)
#include "pins_pcb_v2/pins_samd21.h"
#elif defined(HOTWANDLITE_MCU_RP2040)
#include "pins_pcb_v2/pins_rp2040.h"
#elif defined(HOTWANDLITE_MCU_ESP32C3)
#include "pins_pcb_v2/pins_esp32.h"
#else
#error "The selected target does not have a pin-assignment header"
#endif
#else
#error "Unsupported PCB version: select -1 (reworked V1), 1, or 2"
#endif
