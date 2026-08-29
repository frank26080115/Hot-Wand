# Hot-Wand-Lite 470 kHz Build Guide

## 0. Circuit Board Reception

Get the circuit board from JLCPCB with mostly bottom components already populated.

Populate all out-of-stock components that JLCPCB did not populate on the bottom side first.

## 1. Input Power

![](../imgs/soldering_1.jpg)

Populate power input MOSFETs, these PowerPAK MOSFETs are hand soldered with a soldering iron (not hot air). For each of the three MOSFETs, [follow these steps (click here for document)](./assembly-supplement.md#power-input-mosfets)

Populate the USB-C connector, followed by the XT30 connector

Populate JP5, which is the header for the shunt jumpers that configures USB-PD negotiation

Test input power. First test XT30 connector and check if the ideal-diode works. I recommend using a smoke-stopper device for this test if using a battery, otherwise, use a non-battery DC input with a XT30 connector.

Then test USB-C negotiation with a variety of configurations.

During the USB-C tests, the shunt jumpers should have been installed to JP5
