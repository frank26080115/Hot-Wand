#pragma once

// PCB version -1: original V1 with a XIAO ESP32-C3 and one switch-input jumper.
// Isolate the module's D9 header connection from the PCB. Connect the PCB's
// D9/PWR-DIS contact to module D3/GPIO5, which is unused on the original V1.
// Keep the module's own GPIO9 pull-up and BOOT button intact.
// See doc/xiao-esp32c3-v1-rework.md. Remove the module from the PCB to flash.

#if !defined(HOTWANDLITE_TARGET_XIAO_ESP32C3)
#error "PCB version -1 supports only the Seeed Studio XIAO ESP32-C3"
#endif

#define RFGEN_PIN 3          // XIAO D1, unchanged V1 RF output.
#define BLINK_LED_PIN 10     // XIAO D10, external active-high LED.
#define SEL2_PIN 7           // XIAO D5, Eco selection.
#define SEL3_PIN 6           // XIAO D4, Sport selection (original V1 routing).
#define POWER_SWITCH_PIN 5   // XIAO D3, jumper from PCB D9/PWR-DIS.
#define FAN_CONTROL_PIN 20   // XIAO D7, original V1 fan routing.

// V1 connects D2 and D8 to the same voltage-divider net. Read D2/GPIO4;
// leave D8/GPIO8 high-impedance. GPIO8's strap level can prevent ROM download
// while installed, so flash with the module removed from the PCB.
#define ADC_PIN 4
#define ADC_UNUSED_PIN 8
