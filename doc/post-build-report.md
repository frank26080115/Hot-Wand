# Post-Build Report

This document is written after I've built all 10 units and have tested them. I have observations and results I'd like to share.

## 13.56 MHz Version

### Voltage Tuning

It seems like with the Metcal cartridges, tuning the buck converter potentiometer to about 20V will result in the iron tips idling at about 390C. This is my tuning target.

Notice that this is only achievable if the input power voltage is slightly above 20V.

### Coreless Inductor Tuning

The coreless inductor I hand wound using help from a 3D printed spindle, which is designed with a spiraling groove to fix the winding's pitch and diameter. See `mechanical\utilities\coreless_inductor_post.scad` for details.

The default is 10 turns with 1.0mm winding pitch. This worked well for 3 out of 5 units. 1 unit uses 11 turns with 1.0mm winding pitch. 1 unit uses 12 turns with 1.0mm winding pitch.

### Metcal vs Hakko

When I started this project, I didn't even know Hakko also made a RF soldering iron. They make the FX-100 soldering station which uses the FX1001 handpiece. This is compatible with Hot-Wand directly, using the same F-type coaxial connector.

The Hakko tip seems to run much hotter, on a unit tuned for 390C for Metcal, the Hakko tip reaches about 420C. I think if you are planning to use the Hakko handpiece, then you need to tune the buck converter voltage down a bit.

The Hakko tip also seem to need a different position for the sleep-magnet in the stand. The ideal magnet placement for the Hakko is just behind the tip. Whereas for Metcal, it is near the flange of the handle.

### Power Draw

All of my testing is done with cartridges specified for around 390C. This is key to contextualize the following observation.

While the initial heat-up power can spike to 100W or more, it is not possible to cause more than 60W of power draw by sinking heat from the tip. I've tested this by pooling solder on a 80x80x1mm piece of copper plate and using that to heatsink hot iron tips.

Without a firmware power spike suppression, I've seen power draw spiking to more than 130W, and also some cases when my 140W USB-C brick actually just browned out. The firmware was updated to detect sharp rises in power draw and respond by reducing voltage, which solved this problem.

### Cooling Performance

Testing was done with input configured for 28V, room temperature is 72F (22C) as read by my apartment thermostat, and the load is my RF dummy load, which pulls 75W in the configuration. The enclosure for the testing is the plastic one (nicknamed "econo"), not the metal one.

The temperature measured by the internal thermistor rises slowly to 50C, and then the cooling fan kicks on, it will cool to about 48C under these conditions and the fan remains on (the hysteresis in the firmware to turn the fan off is 5C).

This assures me that as long as the fan is functioning, the soldering station can work indefinitely, as it is difficult to load it past 60W continuously in real usage.

As for the buck converter, a test was done with a constant-current dummy-load on the output directly. There was no enclosure and no active cooling, just open room temperature air. A K-type thermocouple was covered in one layer of kapton tape and used as a probe. The point of probing is the ground plane nearest to the buck converter IC on the same side as the IC itself.

At 21V 4A, the highest measured temperature was 60C. At 5A, the highest temperature measured was about 97C, and I didn't let it stay that way for too long.

The result is good, the temperature is still low enough for me to not worry. The buck converter IC itself has internal protections for junction overheating. And we are not likely to sustain a 5A power draw ever in real usage situations.

### High RF Voltage Exposure

I accidentally touched the underside of the PCB while it was running, my finger was on the RF output near the current transformer. I felt a burning pain sensation. Be careful! It is quite different from touching power from a AC outlet, which has a much lower frequency.

## Lite (470 kHz) Version

### Coil Tuning Variations

The individual toroid cores are not very consistent. Around... 1/3rd of the inductors I've hand wound needed additional hand manipulation before being close enough to the specified inductance. The technique is to remove a wind (which lowers the inductance drastically) and then squish all the winds tighter together (which raises the inductance again, to the target value).

### Tip Temperatures

Since my target temperature is 390C, it seems like the only way to accomplish this is to run the input at 28V, and then use my fingers to manipulate some of the inductors.

### Power Consumption

Most of the time, at 28V input, the unit will start at around 35W, then suddenly spike to 100W, then slow taper down to 35W again. The weird part is that the sharp spike does not happen at the lowest temperature, it's a particular "warm" temperature band that causes it, so there is a delay after power on.

### Heatsink Temperature

While enclosed and the fan working, the heatsink basically stays completely cool all the time even at maximum load.
