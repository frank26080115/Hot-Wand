# Hot-Wand Lite 470 kHz Build Guide

## 0. Circuit Board Reception

Get the circuit board from JLCPCB with mostly bottom components already populated.

Populate all out-of-stock components that JLCPCB did not populate on the bottom side first.

## 1. Input Power

![](./imgs/soldering_1.jpg)

Populate power input MOSFETs, these PowerPAK MOSFETs are hand soldered with a soldering iron (not hot air). For each of the three MOSFETs, [follow these steps (click here for document)](./assembly-supplement.md#power-input-mosfets)

Populate the USB-C connector, followed by the XT30 connector

Populate JP5, which is the header for the shunt jumpers that configures USB-PD negotiation

Test input power. First test XT30 connector and check if the ideal-diode works. I recommend using a smoke-stopper device for this test if using a battery, otherwise, use a non-battery DC input with a XT30 connector.

Then test USB-C negotiation with a variety of configurations (20V vs 28V, 5A limit vs automatic current limit)

During the USB-C tests, the shunt jumpers should have been installed to JP5

## 2. Top Side Components

![](./imgs/soldering_2.jpg)

## 3. Microcontroller and Firmware Bring-Up

WARNING: NEVER flash firmware while the main input power is connected. You can also just remove the microcontroller module before flashing it.

Many microcontroller modules are compatible with this design:

 * Seeed Studio XIAO RP2040
 * Seeed Studio XIAO SAMD21
 * Seeed Studio XIAO ESP32-S3
 * Seeed Studio XIAO ESP32-C3
 * Waveshare RP2040 Zero (needs to be installed upside down)

Solder male headers to the microcontroller module (the headers should have been included with the microcontroller module).

The microcontroller can be [flashed using Visual Studio Code and PlatformIO (click here for a guide)](firmware-flashing.md).

It is recommended to solder female headers onto the Hot-Wand-Lite PCB where the microcontroller module footprint is, and then plugging in the microcontroller module. This makes the microcontroller module removable.

## 4. Custom Inductors

## 5. RF Power Stage and Output

## 6. Configuration Jumpers and Indicators

## 7. Mounting Hardware

## 8. Final Assembly and Testing

## Handpiece

#### If using: Radio Thermal Handpiece

Radio Thermal provides instructions on making a cheap handpiece, [please see their instructions](https://github.com/RadioThermal/RadioThermal_Soldering_OSHW/tree/main/Handpiece)

When finished, you can just connect the it to the SMA connector

#### If using: Thermaltronics Handpiece

Thermaltronics still sells their older SHP-K handpieces (meant for 470 kHz cartridges) for about $50-$60, but they have a weird big connector. You can either cut off the connector and solder on a SMA connector, or make some sort of adapter by yourself.
