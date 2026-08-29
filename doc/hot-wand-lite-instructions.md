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

## Power Levels

The jumpers near the output of the Hot Wand Lite is where you can select the power level. The levels are:

 * Sport (maximum always)
 * Normal (attempt to stay under 3A limit)
 * Eco (60% power always)

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

[Please see this document (click here)](build-guide-lite-470khz/build-guide-lite.md)
