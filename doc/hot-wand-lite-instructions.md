# Hot Wand Lite Instructions

Hot-Wand Lite is the simpler cheaper 470 kHz version of Hot-Wand.

# Operating Instructions

## USB-PD

Only USB-C chargers delivering 100W or more will work with Hot-Wand-Lite. If you want a cheaper way of using the maximum power output, consider using the XT-30 connector along with a dedicated DC power supply capable of 20V and 5A, or more. A 65W USB-C charger might work if you configure for 15V mode.

The voltage and current requested from a USB-PD host can be configured using jumper shunt blocks. The voltage requested is 15V by default without the jumper, and 20V when the jumper is installed.

The current requested is 3A without the jumper, and 5A when the jumper is installed. However, requesting 5A, you need a special USB-C cable with a "E-marker" chip, and the cable must be rated 140W or more.

Realistically, the only configuration that actually works is:

 * USB-C PD charger capable of 100W or more
 * Voltage configured for 20V
 * Current configured for 5A
 * USB-C cable rated for 140W or more and explicitly states that it has a E-marker chip

## Fan

The fan can be configured to be ON or OFF. Jumping the fan jumper pins will turn the fan on.

## Power Levels

The jumpers near the output of the Hot Wand Lite is where you can select the power level. The levels are:

 * Sport (maximum)
 * Normal (medium)
 * Eco (low)

NOTE: These levels are not meant for saving battery power, they only exist so that if your power supply is not sufficient, you have an option to lower the power usage. In fact, theoretically if you are heating up a gigantic solder joint, using a higher power mode will use less total battery energy, because the heat will rise faster and less is being lost during that time. I would keep the configuration in Sport mode unless you have an insufficient power supply.

## LED Indicators

The LED indicator will blink in different patterns according to the input power voltage range (low or high) and the selected output power level (Sport, Normal, or Eco).

    Low V  + Eco:     _/\_______/\______
    Low V  + Normal:  _/\/\_____/\/\____
    Low V  + Sport:   _/\/\/\___/\/\/\__
    High V + Eco:     _/‾‾‾‾‾\/\____/‾‾‾‾‾\/\___
    High V + Normal:  _/‾‾‾‾‾\/\/\___/‾‾‾‾‾\/\/\___
    High V + Sport:   _/‾‾‾‾‾\__/‾‾‾‾‾\_

Input voltage below 22V is considered low. So a 20V capable USB host (such as 65W to 100W) will be considered in the low range. A 24V DC power supply or a 140W USB host will be considered in the high range.

# Assembly Instructions

## PCB

Most of the bottom side components should have been assembled by JLCPCB already.

Solder on the remaining top side components, custom inductors, and the end-launch SMA connector.

Four standoffs are placed on the bottom of the PCB, the specs are: M2.5 thread, 6mm long, 4.5mm hex. You also need 4x M2.5 4mm long screws to secure these from the PCB. 4x more of these, or countersink screws with the same specs, to attach these standoffs to the enclosure

## Handpiece

#### If using: Radio Thermal Handpiece

Radio Thermal provides instructions on making a cheap handpiece, see [their instructions](https://github.com/RadioThermal/RadioThermal_Soldering_OSHW/tree/main/Handpiece)

When finished, you can just connect the other end of the SMA connector

#### If using: Thermaltronics Handpiece

Thermaltronics still sells their older SHP-K handpieces (meant for 470 kHz cartridges) for about $50-$60, but they have a weird big connector. You can either cut off the connector and solder on a SMA connector, or make some sort of adapter by yourself.

## Custom Inductors

In Radio Thermal's design, they used the Kool Mu 0077932A7 core, with Aₗ = 32 nH/turn², +/- 8%. The first core is actually two of these cores epoxied together in a stack, making it roughly Aₗ = 64 nH/turn².

For 6.2 uH: 10 turns if using 2 cores, 14 turns if using 1 core

For 16.9 uH: 22 to 24 turns (choose 23 if no measurement)

For 11.5 uH: 18 to 20 turns (choose 19 if no measurement)

Radio Thermal's developers ask that you use 22 AWG wire or thicker, and use a VNA or LCR meter to verify the results.

If you really do not have the measurement equipment, I have indicated the best choice.

## Enclosure

