#pragma once

// PCB v2 pin assignments traced from electrical/hot-wand-lite.sch and .brd.
// U1 pads: SEL3=4 (XIAO D3), PWR-DIS=8 (D7), FAN=7 (D6).

#if defined(HOTWANDLITE_TARGET_XIAO_RP2040)

#define RFGEN_PIN D1

// The schematic's active-high LED net
#define BLINK_LED_PIN D10

// XIAO's active-low onboard user LED, on RP2040 this resolves to the red LED in the RGB LED
#define BLINK_XIAOBUILTIN_LED_PIN LED_BUILTIN

// Power-selection jumpers: names match their JP6 connector pin numbers.
#define SEL2_PIN D5
#define SEL3_PIN D3

// Active-low power switch: XIAO D7 is RP2040 GPIO1.
#define POWER_SWITCH_PIN D7

// Active-high fan control: XIAO D6 is RP2040 GPIO0.
#define FAN_CONTROL_PIN D6

// Both footprint pads are connected to the voltage-sense net. This target uses
// XIAO D2 for ADC and must leave D8 high-impedance.
#define ADC_PIN D2
#define ADC_UNUSED_PIN D8

#elif defined(HOTWANDLITE_TARGET_WAVESHARE_RP2040_ZERO)

// Install the module upside down (components toward the carrier PCB), with
// the USB end aligned to the XIAO USB end. This flips the left/right columns
// so 5V/GND/3V3 line up with U1. Extra bottom rows are unused.
// U1 D0..D6 -> GPIO0..GPIO6; D7..D10 -> GPIO26..GPIO29.

// Both footprint pads are connected to the voltage-sense net. GPIO27 is ADC1;
// GPIO2 is not ADC-capable and must remain high-impedance.
#define RFGEN_PIN 1 // U1 D1, second row from the USB end.
#define ADC_PIN 27
#define ADC_UNUSED_PIN 2

// The schematic's active-high LED net. The board's onboard WS2812 is not used.
#define BLINK_LED_PIN 29

// Power-selection jumpers: names match their connector pin numbers.
#define SEL2_PIN 5
#define SEL3_PIN 3

// Active-low power switch.
#define POWER_SWITCH_PIN 26

// Active-high fan control on RP2040 GPIO6.
#define FAN_CONTROL_PIN 6

#endif
