#pragma once

// Board-level pin assignments traced from electrical/hot-wand-lite.sch.
// The XIAO boards share pad names and positions, but not GPIO numbers.

#if defined(HOT_WAND_TARGET_XIAO_ESP32S3)

// XIAO D1, ESP32-S3 GPIO2. RMT signals can be routed through the GPIO matrix.
#define RFGEN_PIN 2

// Schematic active-high LED on XIAO D10, ESP32-S3 GPIO9.
#define BLINK_LED_PIN 9

// XIAO ESP32-S3 onboard active-low user LED, ESP32-S3 GPIO21.
#define BLINK_XIAOBUILTIN_LED_PIN 21

// Power-selection jumpers: names match their JP6 connector pin numbers.
#define SEL2_PIN 6 // XIAO D5.
#define SEL3_PIN 5 // XIAO D4.

// Active-low power switch on XIAO D9, ESP32-S3 GPIO8.
#define POWER_SWITCH_PIN 8

// Active-high fan control on XIAO D7, ESP32-S3 GPIO44.
#define FAN_CONTROL_PIN 44

// Both footprint pads are connected to the voltage-sense net. Use XIAO D2,
// ESP32-S3 GPIO3/ADC1_CH2, and leave XIAO D8/GPIO7 high-impedance. GPIO3 is
// also a strapping pin, so validate the board's reset/JTAG behavior in hardware.
#define ADC_PIN 3
#define ADC_UNUSED_PIN 7

#elif defined(HOT_WAND_TARGET_XIAO_ESP32C3)

// XIAO D1, ESP32-C3 GPIO3. RMT signals can be routed through the GPIO matrix.
#define RFGEN_PIN 3

// Schematic active-high LED on XIAO D10, ESP32-C3 GPIO10. The C3 XIAO has no
// Arduino-defined onboard user LED, so BLINK_XIAOBUILTIN_LED_PIN is omitted.
#define BLINK_LED_PIN 10

// Power-selection jumpers: names match their JP6 connector pin numbers.
#define SEL2_PIN 7 // XIAO D5.
#define SEL3_PIN 6 // XIAO D4.

// Active-low power switch on XIAO D9, ESP32-C3 GPIO9. GPIO9 is also the C3's
// boot strap pin, so holding the switch low during reset can enter download mode.
#define POWER_SWITCH_PIN 9

// Active-high fan control on XIAO D7, ESP32-C3 GPIO20.
#define FAN_CONTROL_PIN 20

// XIAO D2 is ESP32-C3 GPIO4/ADC1_CH4. XIAO D8/GPIO8 is not ADC-capable and
// must remain high-impedance even though the PCB connects it to the same net.
// GPIO8 is also a strapping pin, so validate this voltage-driven level at reset.
#define ADC_PIN 4
#define ADC_UNUSED_PIN 8

#elif defined(HOT_WAND_TARGET_ESP32C3_SUPERMINI)

#define RFGEN_PIN 6
#define BLINK_LED_PIN 3

// Power-mode selection inputs. SEL2 selects Eco; SEL3 selects Sport.
#define SEL2_PIN 10
#define SEL3_PIN 9

#define POWER_SWITCH_PIN 2
#define FAN_CONTROL_PIN 0

#define ADC_PIN 1
#define ADC_UNUSED_PIN 7

#elif defined(HOT_WAND_TARGET_WAVESHARE_ESP32C3_ZERO)

#define RFGEN_PIN 20
#define BLINK_LED_PIN 0

// Power-mode selection inputs. SEL2 selects Eco; SEL3 selects Sport.
#define SEL2_PIN 9
#define SEL3_PIN 10

#define POWER_SWITCH_PIN 1
#define FAN_CONTROL_PIN 3

#define ADC_PIN 2
#define ADC_UNUSED_PIN 19

#else
#error "Select exactly one supported ESP32 target"
#endif
