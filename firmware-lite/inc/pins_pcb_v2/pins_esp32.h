#pragma once

// PCB v2 pin assignments traced from electrical/hot-wand-lite.sch and .brd.
// U1 pads: SEL3=4 (XIAO D3), PWR-DIS=8 (D7), FAN=7 (D6).
// The XIAO boards share pad names and positions, but not GPIO numbers.

#if defined(HOTWANDLITE_TARGET_XIAO_ESP32C3)

// XIAO D1, ESP32-C3 GPIO3. RMT signals can be routed through the GPIO matrix.
#define RFGEN_PIN 3

// Schematic active-high LED on XIAO D10, ESP32-C3 GPIO10. The C3 XIAO has no
// Arduino-defined onboard user LED, so BLINK_XIAOBUILTIN_LED_PIN is omitted.
#define BLINK_LED_PIN 10

// Power-selection jumpers: names match their JP6 connector pin numbers.
#define SEL2_PIN 7 // XIAO D5.
#define SEL3_PIN 5 // XIAO D3.

// Active-low shutdown on XIAO D7, GPIO20; no longer on boot strap GPIO9.
#define POWER_SWITCH_PIN 20

// Active-high fan control on XIAO D6, ESP32-C3 GPIO21 (UART0 TX at boot).
// ROM boot output can pulse the fan before firmware takes control.
#define FAN_CONTROL_PIN 21

// XIAO D2 is ESP32-C3 GPIO4/ADC1_CH4. XIAO D8/GPIO8 is not ADC-capable and
// must remain high-impedance even though the PCB connects it to the same net.
// GPIO8 is ignored for normal boot with GPIO9 high, but the divider does not
// guarantee the high level required for ROM download with GPIO9 low.
#define ADC_PIN 4
#define ADC_UNUSED_PIN 8

#elif defined(HOTWANDLITE_TARGET_ESP32C3_SUPERMINI)

// Align 5V/GND/3V3 with U1; the eighth module row is unused.
#define RFGEN_PIN 6
#define BLINK_LED_PIN 4

// Power-mode selection inputs. SEL2 selects Eco; SEL3 selects Sport.
#define SEL2_PIN 10
// GPIO8 is a boot strap and commonly drives the onboard active-low LED.
// Sport grounds it: normal boot works with GPIO9 high; ROM download needs
// Sport disconnected so GPIO8 can be high.
#define SEL3_PIN 8

#define POWER_SWITCH_PIN 1
#define FAN_CONTROL_PIN 20

// GPIO2 is a strap: it does not select SPI/download boot, but Espressif
// recommends pulling it high to avoid glitches. The divider cannot ensure it.
#define ADC_PIN 2
#define ADC_UNUSED_PIN 7

#else
#error "Select exactly one supported ESP32 target"
#endif
