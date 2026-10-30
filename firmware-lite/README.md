# Hot Wand Lite Firmware

The Lite edition of Hot Wand is very simple as it is a copy of the Radio Thermal DIY RF soldering iron design. The microcontroller simply needs to generate a 470 kHz square wave.

The additional feature is that the user can select between 3 power modes. The LED will blink according to the input voltage and selected power mode.

This firmware is built under PlatformIO under the Arduino framework. The RF generator code does use low level hardware registers and so care must be taken so that Arduino built-in functionalities do not conflict with the same hardware.

## Power Modes

The "Sport" mode is a 100%-no-matter-what power mode.

The "Adaptive" power mode is one that allows 100% power for 21V of power input or lower. When above 21V, the firmware attempts to lower power consumption in a way that caps it at 100W.

The "Eco" power mode is a 70% power mode. The intention is to allow a 3A capable power supply to be used instead of a 5A power supply.

## Firmware Version

Edit the version string in `src/version.c`. It is currently `V1.0`.

Send `version` followed by a newline (LF or CRLF) through the serial/USB virtual COM command line to print the firmware version string and compiled PCB version (`HOTWANDLITE_PCB_VERSION`, including `-1` for reworked V1). This command leaves normal operation running and does not enter test mode.
