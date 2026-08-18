# Hot Wand Lite Instructions

Hot-Wand Lite is the simpler cheaper 470 kHz version of Hot-Wand.

# Operating Instructions

## Powering by Battery

If you want to use a battery with the XT30 connector, you can use up to a 6S Li-HV, 6S Li-Po, or 6S LiFePO4 battery pack.

## Powering by Desktop AC

If you want to power from a AC power supply, the best way is to add a XT30 connector on a AC-to-DC converter capable of 20V 5A.

Technically this device will accept up to 28V, and theoretically it is more powerful at higher voltages. It will demand more current, and the electronics will run hotter, and be more stressed. I recommend sticking with 20V. Or, upgrade to the full blown 13.56 MHz version of Hot-Wand.

## Powering by USB-PD

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

The coated core dimensions are 27.69 mm maximum outside diameter, 14.10 mm minimum inside diameter, and 11.94 mm maximum height. For 22 AWG wire, using a diameter of 0.644 mm, two 10 mm leads, and 5% extra wire for winding tolerance, the approximate cut length in millimeters is:

Single core, where `T` is the number of turns:

`(2 * 10) + T * (2 * ((27.69 - 14.10) / 2 + 11.94) + pi * 0.644) * 1.05`

Two cores stacked together:

`(2 * 10) + T * (2 * ((27.69 - 14.10) / 2 + (2 * 11.94)) + pi * 0.644) * 1.05`

One turn means passing the wire through the center of the toroid once. These lengths are deliberately approximate; cut a little more if the two-core stack has a thick epoxy joint or if you want longer leads.

For 6.2 uH: 10 turns using 2 cores, requiring about 685 mm of wire; or 14 turns using 1 core, requiring about 601 mm

For 16.9 uH: 22 to 24 turns, requiring about 932 mm to 1015 mm of wire (choose 23 turns and about 974 mm if no measurement)

For 11.5 uH: 18 to 20 turns, requiring about 766 mm to 849 mm of wire (choose 19 turns and about 808 mm if no measurement)

Radio Thermal's developers ask that you use 22 AWG wire or thicker, and use a VNA or LCR meter to verify the results.

If you really do not have the measurement equipment, I have indicated the best choice.

#### Total Wire Lengths

The totals below assume 22 AWG wire and use the rounded cut lengths given above.

Using the recommended turn counts and two stacked cores for the 6.2 uH inductor:

`685 + 974 + 808 = 2467 mm = 2.467 m = 8.09 ft`

Depending on whether 22 to 24 turns and 18 to 20 turns are used for the other two inductors, the total ranges from approximately 2383 mm to 2549 mm, or 2.383 m to 2.549 m (7.82 ft to 8.36 ft).

If the 6.2 uH inductor is instead wound with 14 turns on one core, while the recommended turn counts are used for the others:

`601 + 974 + 808 = 2383 mm = 2.383 m = 7.82 ft`

For one complete unit, the practical minimum purchase is 3 m (10 ft) of 22 AWG magnet wire. To leave enough wire for trimming mistakes or rewinding an inductor, buy approximately 5 m (16.4 ft), or the next larger standard spool size.

## Enclosure

