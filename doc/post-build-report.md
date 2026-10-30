# Post-Build Report

This document is written after I've built all 10 units and have tested them. I have observations and results I'd like to share.

## 13.56 MHz Version

Most of this testing is done on my first unit, the metal one. That's my "golden" unit. For the cooling tests, I used the four plastic ones I've nicknamed "econo" in the build guide.

### Voltage Tuning

It seems like with the Metcal cartridges, tuning the buck converter potentiometer to about 20V will result in the iron tips idling at about 390C. This is my tuning target.

Notice that this is only achievable if the input power voltage is slightly above 20V.

### Coreless Inductor Tuning

The coreless inductor I hand wound using help from a 3D printed spindle, which is designed with a spiraling groove to fix the winding's pitch and diameter. See `mechanical\utilities\coreless_inductor_post.scad` for details.

The default is 10 turns with 1.0mm winding pitch. This worked well for 3 out of 5 units. 1 unit uses 11 turns with 1.0mm winding pitch. 1 unit uses 12 turns with 1.0mm winding pitch.

### Metcal vs Hakko

The meter is a knockoff FG-100, and I'm mainly testing the Metcal STTC-147P and Hakko T31B-02D08. I don't have a commercial Metcal or Hakko station to compare against, so these are all measurements on Hot-Wand.

When I started this project, I didn't even know Hakko also made a RF soldering iron. They make the FX-100 soldering station which uses the FX1001 handpiece. This is compatible with Hot-Wand directly, using the same F-type coaxial connector.

The Hakko tip seems to run much hotter, on a unit tuned for 390C for Metcal, the Hakko tip reaches about 420C. I think if you are planning to use the Hakko handpiece, then you need to tune the buck converter voltage down a bit.

The Hakko tip also seem to need a different position for the sleep-magnet in the stand. The ideal magnet placement for the Hakko is just behind the tip. Whereas for Metcal, it is near the flange of the handle.

### Power Draw

All of my testing is done with cartridges specified for around 390C. This is key to contextualize the following observation.

I'm reading the power from the OLED debug display, and all of these power tests are in SPORT mode (the 100W-ish mode).

While the initial heat-up power can spike to 100W or more, it is not possible to cause more than 60W of power draw by sinking heat from the tip. I've tested this by pooling solder on a 80x80x1mm piece of copper plate and using that to heatsink hot iron tips.

Without a firmware power spike suppression, I've seen power draw spiking to more than 130W, and also some cases when my 140W USB-C brick actually just browned out. The firmware was updated to detect sharp rises in power draw and respond by reducing voltage, which solved this problem.

Idle power consumption is in the neighborhood of 17W, while under the magnetic stand's influence. It's not much higher without the magnets, maybe 2 more watts. The inactivity detection is configurable but the default setting is tuned against this result (the detection is both relative and absolute).

I did one tip temperature measurement while the Hakko tip was in the magnetic stand, it settled at about 270C.

### Power Modes

This test was done with a benchtop adjustable power supply and an inline power measurement device. The handpiece is the Hakko FX1001 with the T31-02 tip, and note that the unit under test is tuned a bit higher than what this tip expects, it's over driven slightly and reaches a tip temperature of 420C instead of the 390C target that I should be tuning for (thus, these are worst case measurements). The firmware power spike attenuation and power level limiter are on, firmware version is `v1.1`.

| Input Voltage | Power Mode | Input Peak Power | Output Peak Power | Recommended USB Spec     |
|--------------:|-----------:|-----------------:|------------------:|-------------------------:|
| 28V           | 100W       | 110W             | 100W              | 140W                     |
| 28V           | 75W        | 88W              | 75W               | 100W                     |
| 28V           | 50W        | 60W              | 50W               | 65W                      |
| 20V           | 100W       | 88W              | 80W               | 100W                     |
| 20V           | 75W        | 83W              | 75W               | 100W                     |
| 20V           | 50W        | 57W              | 50W               | 65W                      |

Note that a 100W or 140W USB-PD charger is needed to reach 28V anyways.

Tests with my 65W charger causes the undervoltage detection to trigger and go into the 50W mode temporarily during peak power use, this indicates that the charger I used exhibited a slight voltage dip during high current draw. There are other layers of protection in play too.

### Cooling Performance

Testing was done with input configured for 28V, room temperature is 72F (22C) as read by my apartment thermostat, and the load is my RF dummy load, which pulls 75W in the configuration. The enclosure for the testing is the plastic one (nicknamed "econo"), not the metal one.

The temperature measured by the internal thermistor rises slowly to 50C, and then the cooling fan kicks on, it will cool to about 48C under these conditions and the fan remains on (the hysteresis in the firmware to turn the fan off is 5C). All four plastic units did pretty much the same thing, cooling to about 48C, and I could repeat this result.

This assures me that as long as the fan is functioning, the soldering station can work indefinitely, as it is difficult to load it past 60W continuously in real usage.

As for the buck converter (which is where we've put custom copper heatsinking fins), a test was done with a constant-current dummy-load on the output directly. There was no enclosure and no active cooling, just open room temperature air. A K-type thermocouple was covered in one layer of kapton tape and used as a probe. The point of probing is the ground plane nearest to the buck converter IC on the same side as the IC itself.

If I remember right, I used 28V input through XT30 and tuned the buck converter output to about 21V.

At 21V 4A, the highest measured temperature was 60C. At 5A, the highest temperature measured was about 97C, and I didn't let it stay that way for too long.

I set the current on the dummy load, and the microcontroller's readings agreed perfectly. I didn't time these runs, I just waited until the temperature stopped rising and assumed it had reached equilibrium. I didn't leave it sitting at 5A for long after that.

The result is good, the temperature is still low enough for me to not worry. The buck converter IC itself has internal protections for junction overheating. And we are not likely to sustain a 5A power draw ever in real usage situations.

### High RF Voltage Exposure

I accidentally touched the underside of the PCB while it was running, my finger was on the RF output near the current transformer. I felt a burning pain sensation. Be careful! It is quite different from touching power from a AC outlet, which has a much lower frequency.

### Other

The OLED screen cannot be dimmed, there's a "contrast" setting that is 0-255, and while it does kinda work, it's really REALLY hard to see a difference between the levels. So I removed the auto dimming screen feature.

## Lite (470 kHz) Version

### Coil Tuning Variations

The individual toroid cores are not very consistent. Around... 1/3rd of the inductors I've hand wound needed additional hand manipulation before being close enough to the specified inductance. The technique is to remove a wind (which lowers the inductance drastically) and then squish all the winds tighter together (which raises the inductance again, to the target value).

It is my opinion that the Lite version of Hot Wand is impossible to build without either a LCR meter or a VNA.

### Tip Temperatures

At 20V input, most units were at 320C or something like that. I waited until the warm-up power spike was over and the power draw had settled before taking the readings.

Since my target temperature is 390C, it seems like the only way to accomplish this is to run the input at 28V, and then use my fingers to manipulate some of the inductors.

It is my opinion that, it'll be foolish to attempt to build the Lite version of Hot Wand without a soldering iron tip temperature measurement device or a LCR meter or VNA for the tuning, you can't build this thing blind.

### Power Consumption

Most of the time, at 28V input, the unit will start at around 35W, then suddenly spike to 100W, then slow taper down to 35W again. The weird part is that the sharp spike does not happen at the lowest temperature, it's a particular "warm" temperature band that causes it, so there is a delay after power on.

### Magnetic Stand

The 470 kHz cartridges do not respond at all to a magnetic stand.

### Heatsink Temperature

While enclosed and the fan working, the heatsink basically stays completely cool to the touch even at maximum load. It didn't even feel warm, so I didn't bother measuring it with a tool.
