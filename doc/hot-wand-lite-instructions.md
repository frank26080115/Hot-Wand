# Hot Wand Lite Instructions

Hot-Wand Lite is the simpler cheaper 470 kHz version of Hot-Wand.

# Operating Instructions

## USB-PD

Only USB-C chargers delivering 65W or more will work with Hot-Wand. Only USB-C chargers delivering 140W or more will deliver the maximum amount of power that Hot-Wand is capable of delivering. If you want a cheaper way of using the maximum power output, consider using the XT-30 connector along with a dedicated DC power supply capable of 24V and 5A, or more.

The voltage and current requested from a USB-PD host can be configured using jumper shunt blocks. The voltage requested is 20V by default without the jumper, and 28V when the jumper is installed. Some USB hosts can still provide 20V even if 28V is requested.

The current requested is "automatic" without the jumper, and 5A when the jumper is installed. However, requesting 5A, you need a special USB-C cable with a "E-marker" chip, and it must be rated 140W or more.

When using the "automatic" option for requesting current from the USB host without the special cable, it is likely the limit is 3A, if using a 65W, 80W, or 100W charger.

If you find that the Hot-Wand simply does not power up, first, choose "automatic" for current. If that doesn't help, then choose 20V instead of 28V. If the 20V and automatic current option does not work, something is seriously wrong and that particular USB host cannot work with Hot-Wand.

If you find that the Hot-Wand will function, but suddently, power is cut by the USB host, then you should try lowering the power usage by using one of the lower power modes.

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
