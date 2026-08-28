# Assembly Supplement

## Power Input MOSFETs

Populate power input MOSFETs, these PowerPAK MOSFETs are hand soldered with a soldering iron (not hot air). For each of the three MOSFETs, follow these steps:

1. brush the bottom of the MOSFET with flux, brush the PCB footprint with flux
2. apply a **very thin** layer of solder to the center pad of the MOSFET footprint on the PCB (not the MOSFET itself)
3. brush some more flux onto the PCB footprint
4. solder pin 1 of the MOSFET to the PCB, ensuring it is on straight
5. solder pins 2, 3, 4 of the MOSFET onto the PCB
6. solder pins 5 thru 8 of the MOSFET all in one go, apply extra solder as the wicking action will pull solder underneath the MOSFET (this is why we prepped the center pad first)

## Testing Note: Voltage Measurement Points

During assembly, it is recommended that you test the power supplies as they are added. Using a multimeter, these are the points that are convenient to test each important voltage node.

[![](../imgs/voltage_measurement_nodes_200.png)](../imgs/voltage_measurement_nodes_800.png)

## PCB Cooling Fins near Buck Converter

Using 0.5mm thick copper sheets, cut strips that are about 5mm tall, and then cut them into small fins. Solder these fins to the top side of the PCB where the copper is exposed, near where buck converter is under, which is also near where one of the standoff screws is supposed to go. Arrange them such that the fins are perpendicular to the edge of the board as we want the air to flow outwards towards vents that are on the side of the box.

<!-- TODO: Add a photo of the PCB cooling fins. -->

## Fan Solder Jumper Selection

![](../imgs/fan_sj_config.png)

For fans with only two wires, simply connect it such that the positive wire is connected to where it says `FAN+`, and the positive wire is connected to where it says `FAN-`. Then, use solder to bridge SJ4-B.

For fans with a PWM input (4 wires), plug the connector in the correct orientation (as indicated by the artwork). Then, for fans that prefer a push-pull PWM signal, use solder to bridge SJ4-A and SJ5-D. For fans that prefer an open-drain PWM signal, use solder to bridge SJ4-A and SJ5-C.

Noctua's documentation says their fans prefer a push-pull signal. Other brands of PC fans traditionally prefers an open-drain signal.

Using an open-drain signal requires the firmware option "FAN SIGNL POLAR" to be set to `INVRT`.

## Solder Jumpers and Tuning Parts

VR1 and R37: Responsible for main buck converter output voltage. If the best value for VR1 is known, VR1 can be omitted and R37 used in its place as a permanent setting.

R12 and R13 dissipates gate charge to Q1. R13 is to be identical as R12 but only placed if required. Otherwise leave DNP.

SJ6 is a 0R01 measurement resistor for current through Q1. Short it out with solder for normal builds.

SJ7 is an additional short-to-ground for when SJ6 is not being used for current measurement. Short it out with solder for normal builds.

R11 and VR4 are used to set the sensitivity of the current transformer power factor detector. If you need VR4 for tuning, then hort out SJ2, and maybe remove R11 depending on if you want it in parallel or not.

SJ5 is a measurement resistor for the 12V bus. It is used when tuning L8 (the custom coreless inductor). During normal builds, short out SJ5 with solder.

VR2 and VR3 are used to tune the sensitivity of the tip-detector. VR2 with R19 (and sometimes R20) sets the constant DC bias into Q3's base. VR3 and R22 sets the sensitivity to AC for Q3's base. The resistors can be exchanged for other values when the final resistance is determined, and the potentiometers can be bypassed with a solder jump.

SJ1 is connected between the microcontroller's BOOT0 pin and ground. It should be shorted out with solder in all normal situations. It can be used to put the microcontroller in a bootloader mode if a wire is used to bridge BOOT0 to VCC.

SJ3 connects the microcontroller to the overall fan controlling circuitry. Only connect this after flashing the microcontroller with firmware.

SJ4 is used to select the fan's ground, it can be permanently connected to ground, or have the ground connected to the MOSFET.

SJ5 selects the connection to the PWM pin of the fan. Most fans do not actually use a PWM signal. You can select the PWM signal coming directly from the MCU (recommended for Noctua brand fans and some others), or select the PWM to come from the MOSFET (open drain mode, requires signal inversion in configurable firmware settings).

R46 should be a 0 ohm resistor (or short it out with solder). You can choose to replace it with a particular value if you need to negotiate a certain current limit with the USB-PD host.

## Component Skipping and Substitutions

### Fuse

You can obviously just bridge the fuse holder footprint and not use a fuse. Do this at your own risk.

### Power Input Ideal Diode

The ideal-diode for the XT30 connector input can simply be skipped, by soldering over the MOSFET's footprint. This will cause that input to not be protected against power from USB back-flowing into it. The purpose of the ideal-diode is so that a battery will not explode if the battery and USB are both connected at the same time. If you skip the ideal-diode, you lose this protection.

You can also put a large schottky diode over the MOSFET footprint and keep the protection at the cost of some power efficiency loss.

### 3.3V Regulator

If you don't want to pay for a `R-78K3.3` buck converter for the 3.3V power bus, you can install a linear voltage regulator instead. The PCB layout supports the `LM1117T-3.3` or `LD1117V33` footprint by providing an extra pad beside the original `R-78K3.3` footprint. (this only saves about $2, and will result in more internal heat)

If you perform this substitution, when the input voltage reaches below about 14V, instead of providing a warning, the whole device will simply shut down abruptly.
