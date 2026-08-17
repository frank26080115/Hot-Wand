# Main Loop Tasks

 * long button press forces iron to sleep, short press cycles through power modes
 * GUI is updated at 15 FPS
 * UART debug messages are sent at 2 Hz

# Important Policies

 * Faults are never automatically recovered, always have the user press a button
 * The screen is only 32 pixels wide, supporting 5 characters, choose short words
 * Only use capital letters on the GUI as the pixel font may be hard to read
 * Action for button short press must be performed immediately on the first edge to appear responsive

# GUI design

The screen is 128x32 but oriented in a portrait fashion, so it is 32 pixels wide and 128 pixels tall. The connector of the screen is at the bottom.

The top line is always the DC input voltage (for battery monitoring)

If a temperature limit is reached, the first line will also blink "!HOT!" every other second.

Below that is the power graph. It plots the power vs time with solid shaded plot.

# Power Levels

The assumption is that this is an electrically 100W-ish system because the buck converter is theoretically tuned for 20V and is capable of 5A. Actual heat power is unknown. Total input power will also be slightly higher due to inefficienceis and  miscellaneous power usage elsewhere.

The user can choose between power level modes. Available levels are:

 * "SPORT" (100W)
 * "NORMAL" (75W)
 * "ECO" (50W, must never fail when used with a 65W USB power supply)

To actually modulate the power, there is a PWM pin allocated to send a bias signal to the buck converter's feedback input through a resistor and diode.

If the currently utilized power is above the set limit, the attenuation is raised at a steady pace, otherwise, it is lowered.

Pressing the button will cycle through the power modes.

# Power Management

If the input power is below 14V (undervoltage lockout threshold of the secondary buck converter), then all soldering iron functionality is disabled. The GUI will show "LOW VOLT FAULT". Button presses in this state will cause a reboot. The microcontroller shall be in a state that can accept SWD connections.

The ADC provides other fault protections with interrupt priority: one triggered by over-voltage of the buck converter, another triggered by a sharp drop of the input voltage. These events will trigger the system to immediately shut down to protect itself.

If the temperature is too hot, the power limit of ECO mode will be automatically imposed. There is also a higher temperature limit, if reached, will cause the system to completely shutdown with the a fault message.

# Cooling Fan Control

The cooling fan hardware is optional, but the control for it will always exist.

The fan is always off for at least the configured minimum-off time after power up. After that interval, the fan will respect the configured operation mode. If the mode is always-on, then it will turn on.

In either automatic mode, the fan turns on when any monitored temperature exceeds that mode's configured low or high threshold. It turns off only after every monitored temperature falls to or below the selected threshold minus the configured temperature hysteresis. Normal state-machine transitions keep the fan on for at least the configured minimum-on time and off for at least the configured minimum-off time, preventing rapid cycling around a threshold. Explicit safety, fault, and sleep stops remain immediate; if such a stop is recoverable, the minimum-off interval is enforced before the fan can restart.

The fan is off during sleep mode.

# Boot Mode

When the microcontroller boots, it will wait 300 milliseconds for power to stabilize, and then sample the input DC power.

If during boot, the user button is held down, then UART message debugging will become enabled. While the buttons is held, the screen shows "HOLD TO ENTER SETUP" with a progress bar. The progress bar will grow for 3 seconds and then enter the setup menu if the user continues to hold the button.

If the crystal clock fails to initialize properly, the GUI will display an error "CLOCK FAULT"

# Setup Menu

When in the setup menu, the RF generator will be off and the buck converter will be put in minimum output state.

The first line will always say "SETUP", the second line will always be a blank space.

From the third line and onwards, the subject title of the item will be displayed, followed by a "  =  " line, then the value of the item.

Long hold press cycles the value.

Short press cycles the subject.

The two "SAVE AND EXIT" and "EXIT NO SAVE" are the last subjects and will perform the actions indicated

A 5 minute inactivity timeout will cause the device to enter sleep mode (without saving) to prevent OLED burn-in.

# Firmware Code Modules

 * U8g2 and U8x8
 * OLED interface layer, connects HAL to U8g2
 * GUI main mode display
 * GUI setup menu display
 * NVM saving and loading
 * Systick 1ms time keeper, doesn't have to be precise
 * Round robin ADC sampling state machine that also handles digital low pass filtering
 * Tip detection state machine
 * User button state machine
 * Power output limiter
 * RF clock generator
 * Fan state machine
