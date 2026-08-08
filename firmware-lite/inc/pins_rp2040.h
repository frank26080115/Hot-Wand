#pragma once

// Board-level pin assignments traced from electrical/hot-wand-lite.sch.

#if HOT_WAND_TARGET_XIAO_RP2040 == 1

#define RFGEN_PIN D1

// The schematic's active-high LED net is XIAO D10/A10, SAMD21 PA06.
#define BLINK_LED_PIN D10

// XIAO's active-low onboard user LED is Arduino pin 13, SAMD21 PA17.
#define BLINK_XIAOBUILTIN_LED_PIN LED_BUILTIN

// Power-selection jumpers: names match their JP6 connector pin numbers.
#define SEL2_PIN D5
#define SEL3_PIN D4

// Input-voltage sensing divider output
#define ADC_PIN D2

#endif
