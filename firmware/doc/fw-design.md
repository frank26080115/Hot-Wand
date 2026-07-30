# Main Loop Tasks

 * check all ADC inputs, maybe apply digital LPF filtering
 * if the iron tip is disconnected, the iron goes into a safe state until button press
 * button press toggles power modes, action performed on release
 * long buttom press forces iron to sleep, short press returns to low power mode
 * GUI is updated at 15 FPS
 * UART debug messages are sent at 2 Hz

# GUI design

The screen is 128x32 but oriented in a portrait fashion, so it is 32 pixels wide and 128 pixels tall. The connector of the screen is at the bottom.

The top line is always the DC input voltage (for battery monitoring)

The second line shows either "TIP ERR" or "SLEEP" depending on situation, otherwise blank when normal.

The third line always says "POWER:" but only when not sleeping and not in an error state.

Below that is the power meter. There is a solid horizontal line indicating the 100% boundary (this is just below "POWER:").

If we are in full power mode, then the whole area below this is a solid rectangle growing from the bottom upwards according to power consumption.

If we are in a power limited mode, then the bottom area is split in two halves, the left half is blank except for a dotted horizontal line indicating the targeted power level. The right side of this area grows as a solid rectangle from the bottom upwards according to power consumption.

# Power Levels

The assumption is that this is a 100W system because the buck converter is theoretically tuned for 20V and is capable of 5A.

The available levels are

 * 100% Power (100W)
 * 75% Power (75W)
 * 50% Power (50W, must never fail when used with a 65W USB power supply)

To actually modulate the power, there is a PWM pin allocated to send a bias signal to the buck converter's feedback input through a resistor and diode.

If the currently utilized power is above the set limit, the attenuation is raised at a steady pace, otherwise, it is lowered.

# Firmware Code Modules

 * U8g2 and U8x8
 * OLED interface layer, connects HAL to U8g2
 * Systick 1ms time keeper, doesn't have to be precise
 * Round robin ADC sampling state machine that also handles digital low pass filtering
 * Tip detection state machine
 * User button state machine
 * Power output limiter
 * RF clock generator
 * Fan state machine
