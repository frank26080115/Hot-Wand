#pragma once

// Board-level pin assignments traced from electrical/hot-wand-lite.sch.

#if HOT_WAND_TARGET_XIAO_SAMD21 == 1

// XIAO D1/A1 is SAMD21 PA04 and provides TCC0/WO[0].
#define RFGEN_PIN D1

// The schematic's active-high LED net is XIAO D10/A10, SAMD21 PA06.
#define BLINK_LED_PIN D10

// XIAO's active-low onboard user LED is Arduino pin 13, SAMD21 PA17.
#define BLINK_XIAOBUILTIN_LED_PIN LED_BUILTIN

// Power-selection jumpers: names match their JP6 connector pin numbers.
#define SEL2_PIN D5 // XIAO D5/A5, SAMD21 PA09.
#define SEL3_PIN D4 // XIAO D4/A4, SAMD21 PA08.

// Active-low power switch: XIAO D9/A9, SAMD21 PA05.
#define POWER_SWITCH_PIN D9

// Both footprint pads are connected to the voltage-sense net. This target uses
// XIAO D8/A8 (SAMD21 PA07) for ADC and must leave D2 high-impedance.
#define ADC_PIN D8
#define ADC_UNUSED_PIN D2

#endif
