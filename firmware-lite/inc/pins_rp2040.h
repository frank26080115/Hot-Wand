#pragma once

// Board-level pin assignments traced from electrical/hot-wand-lite.sch.

#if defined(HOT_WAND_TARGET_XIAO_RP2040)

#define RFGEN_PIN D1

// The schematic's active-high LED net
#define BLINK_LED_PIN D10

// XIAO's active-low onboard user LED, on RP2040 this resolves to the red LED in the RGB LED
#define BLINK_XIAOBUILTIN_LED_PIN LED_BUILTIN

// Power-selection jumpers: names match their JP6 connector pin numbers.
#define SEL2_PIN D5
#define SEL3_PIN D4

// Both footprint pads are connected to the voltage-sense net. This target uses
// XIAO D2 for ADC and must leave D8 high-impedance.
#define ADC_PIN D2
#define ADC_UNUSED_PIN D8

#elif defined(HOT_WAND_TARGET_WAVESHARE_RP2040_ZERO)

// Both footprint pads are connected to the voltage-sense net. GPIO27 is ADC1;
// GPIO2 is not ADC-capable and must remain high-impedance.
#define RFGEN_PIN 0
#define ADC_PIN 27
#define ADC_UNUSED_PIN 2

// The schematic's active-high LED net. The board's onboard WS2812 is not used.
#define BLINK_LED_PIN 29

// Power-selection jumpers: names match their connector pin numbers.
#define SEL2_PIN 5
#define SEL3_PIN 4

#endif
