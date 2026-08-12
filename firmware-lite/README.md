# Hot Wand Lite Firmware

The Lite edition of Hot Wand is very simple as it is a copy of the Radio Thermal DIY RF soldering iron design. The microcontroller simply needs to generate a 470 kHz square wave.

The additional feature is that the user can select between 3 power levels.

This firmware is built under PlatformIO under the Arduino framework. The RF generator code does use low level hardware registers and so care must be taken so that Arduino built-in functionalities do not conflict with the same hardware.
