# Assembly Instructions for Hot-Wand 13.56 Mhz

This document contains instructions, insights, tips, and deviations, for the assembly of the fully featured 13.56 MHz Hot-Wand project. (for )

This is not a easy project, nor is it a cheap one. It was designed with my own skill level and budget in mind.

## Soldering

Most of the PCB is assembled by JLCPCB's PCBA service, but only for the bottom side of the board. The SMT and through-hole components on the top side of the board are expected to be soldered on by the hobbyist performing the DIY build.

Please see the detailed BOM file to see which components are soldered by whom, and where to get the parts.

The 22uH axial inductor needs to be raised slightly above the PCB.

There are several LED indicators on the top side of the board, they simply indicate if a particular power bus actually has power, they are not required.

#### Solder Jumpers and Tuning Parts

VR1 and R37: Responsible for main buck converter output voltage. If the best value for VR1 is known, VR1 can be omitted and R37 used in its place as a permanent setting.

R12 and R13 dissipates gate charge to Q1. R13 is to be identical as R12 but only placed if required. Otherwise leave DNP.

SJ6 is a 0R01 measurement resistor for current through Q1. Short it out with solder for normal builds.

R11 and VR4 are used to set the sensitivity of the current transformer power factor detector. If you need VR4 for tuning, then hort out SJ2, and maybe remove R11 depending on if you want it in parallel or not.

SJ5 is a measurement resistor for the 12V bus. It is used when tuning L8 (the custom coreless inductor). During normal builds, short out SJ5 with solder.

VR2 and VR3 are used to tune the sensitivity of the tip-detector. VR2 with R19 (and sometimes R20) sets the constant DC bias into Q3's base. VR3 and R22 sets the sensitivity to AC for Q3's base. The resistors can be exchanged for other values when the final resistance is determined, and the potentiometers can be bypassed with a solder jump.

SJ1 is connected between the microcontroller's BOOT0 pin and ground. It should be shorted out with solder in all normal situations. It can be used to put the microcontroller in a bootloader mode if a wire is used to bridge BOOT0 to VCC.

SJ3 connects the fan control MOSFET to the microcontroller's SWDIO pin. You must short this out with solder but only after the firmware has been flashed to the microcontroller. Otherwise the fan will go wild during firmware flashing.

SJ4 should be left open. Shorting it out will leave the fan permanently spinning with no control.

R46 should be a 0 ohm resistor (or short it out with solder). You can choose to replace it with a particular value if you need to negotiate a certain current limit with the USB-PD host.

## Custom Inductors

Make sure you are familiar with how to use enamel coated magnet wire correctly, how to prepare the ends of the wires for soldering, etc.

Remember that, one "turn" is defined as passing through the center of the toroid once. It doesn't have to be a complete loop.

#### 9 uH choke

Both small custom inductors use the Fair-Rite 5961004901 toroid core, and 22 AWG solid core enamel coated wire (aka magnet wire).

For the 9uH choke, use 10 turns.

Equation for wire length: `(2 * 10) + T * (2 * ((16 - 9.6) / 2 + 6.35) + pi * 0.644) * 1.05`

10 turns should be 242 mm of wire.

If you actually managed to get a `K16x8x6`, then use 15 turns.

#### current transformer

The ratio is 1:14:14

Uses the Fair-Rite 5961004901 toroid core and 22 AWG wire.

The primary (the 1 in 1:14:14) is just a single wire crossing the inside of the toroid once. No crossing on the bottom/outside of the toroid.

The two secondaries can be done using a single pair of wires (bifilar), wrapping the toroid 14 times. Do not cause these wires to cross while wrapping around the toroid.

![](imgs/current_transformer_winding_3d.png)

![](imgs/current_transformer_winding_3d_nocore.png)

The wire length of each secondary should be about 331 mm.

#### large inductors

The three large inductors are using the Amidon T130-6 toroid cores and 16 AWG solid core enamel coated wire.

Equation for wire length: `(2 * 10) + T * (2 * ((33 - 19.8) / 2 + 11.1) + pi * 1.29) * 1.05`

180 uH -> 4 turns -> 186 mm

400 uH -> 6 turns -> 269 mm

540 uH -> 7 turns -> 310 mm

#### coreless inductor L8

Use 10 turns of 22 AWG wire, wound around a 5 mm dowel or similar mandrel. Make the coil approximately 10 mm wide, then squeeze or stretch it during tuning.

![](imgs/coreless_inductor_3d.png)

Using a 5 mm inside diameter, a 0.644 mm wire diameter, a 1 mm pitch, two 10 mm leads, and 5% extra wire for winding tolerance, the approximate cut length in millimeters is:

`(2 * 10) + 10 * sqrt((pi * (5 + 0.644))^2 + (10 / 10)^2) * 1.05`

This gives approximately 207 mm, so cut about 210 mm of wire before winding.

#### Total wire used

The totals below use the rounded cut lengths given above. The values calculated directly from the equations are within about 5 mm of these totals.

For 22 AWG:

`210 + 242 + 242 + 331 + 331 = 1356 mm = 1.356 m = 4.45 ft`

This includes L8, both 9 uH chokes, and both current-transformer secondaries. It does not include the current transformer's short one-turn primary; reserve at least another 50 mm for it. The resulting planned requirement is approximately 1.41 m or 4.62 ft.

For 16 AWG:

`186 + 269 + 310 = 765 mm = 0.765 m = 2.51 ft`

For one complete unit, the practical minimum purchase is 1.5 m (5 ft) of 22 AWG and 1 m (3.3 ft) of 16 AWG. To leave enough wire for trimming mistakes or rewinding an inductor, buy approximately 3 m (10 ft) of 22 AWG and 1.5 m (5 ft) of 16 AWG.

## Enclosure Cutouts

### Box Walls

The box walls has holes and cutouts. Two 4mm holes are for the screws that press the MOSFET heat-sinks against the wall. A big 10mm hole is for the coax connector, a 3mm hole is for the button. The coax connector hole and the button hole will be turned into a slot for easier construction using a dremel.

The slots are cut using a dremel and cutoff disk. The big cutout is for the screen.

This is all guided by a 3D printed drilling template. The template also has 2mm holes to help guide the long dremel cuts, used like perforation.

### Bottom Lid

Drill 2.5mm diameter holes into the bottom enclosure lid. I used a 3D printed drilling template to help me with this.

### Screen Bezel

3D print the screen bezel. Use epoxy to attach it to the box where the large screen cutout was made. Then use a drill to drill into the aluminum where the screw should go. I used a M3 threading tap and M3 screw. Use a sheet metal screw if you do not have a threading tap.

Or just skip having a screen bezel.

## PCB Standoffs

The PCB standoffs are:

 * need 6 of them
 * material: brass
 * M2.5 thread, 6mm long, 4.5mm hex

They also need M2.5 screws, 4mm long. You need at least 12, or 6 button head ones for the top and 6 countersink ones for the bottom.

Use the screws to secure all the standoffs to the bottom of the PCB. Use loctite/thread-locker if you can. Make sure the hexagonal faces are all parallel in the long direction.

Soldering the standoff near the buck converter will help dissipate even more heat.

## Heat Sinks

### Buck Converter

The buck converter has heat dissipation areas designed into the PCB. Take a 1mm thick copper sheet, cut it into strips 12mm long and 5mm high, solder them to the top plane in an array arrangement pointing towards the vent hole of the enclosure. Cut some of the fins in a way to avoid the screw head.

Alternatively you can just buy a 12mm x 12mm square heat-sink and just cut it to clear the area on it where the screw needs to go. Make sure to use thermal compound or thermal double-sided tape underneath the square heat-sink.

### MOSFETs

The heat-sinks are Boyd Laconia 504222B00000G. Cut and bend the heat-sinks like in the photos TODO

Epoxy a M3 nut to the TO-220 tab, make sure you are filling the gap between the nut and the plastic body. Or, if you are fancy, you can solder a brass nut to the tab. You will not be tightening this nut very hard. When you are finished doing this, make sure a nylon M3 screw can freely thread into this nut.

Use double-sided-adhesive thermal pads to attach the heat-sink body to the back of the TO-220. Make sure the hole is aligned.

Cut a strip of 2mm or 3mm thick thermal padding, stick it to the back of the heat-sink, and make sure the hole is exposed.

## Cooling Fan

Attach the cooling fan to the PCB with hot glue or epoxy or VHB tape first. Then use insulated solid core 22 AWG wire, or similar, to solder the fan to the PCB through the mounting holes. Then add more hot glue around these holes

## Final Assembly

With the brass standoffs already attached to the PCB (see previous step), attach the PCB to the enclosure lid with more screws, maybe countersink the holes and use countersink screws if you are able to.

The bottom lid with the PCB on it should just slip into the enclosure box. Use the screws that came with the enclosure to close the box.

There are two holes on the wall of the enclosure aligned to where the TO-220 MOSFETs have their holes, the one we epoxied nuts to. Insert a long nylon M3 screw, with a nylon washer, into those holes, and try to tighten the MOSFET heat-sinks against the wall of the enclosure. Remember, in the previous step we have put a thick piece of thermal padding on the heat-sinks in this area. Tightening these nylon screws will compress this padding, ensure good thermal transfer from the MOSFET to the enclosure, which will improve heat dissipation.

## Component Skipping and Substitutions

### Fuse

You can obviously just bridge the fuse holder footprint and not use a fuse. Do this at your own risk.

### Power Input Ideal Diode

The ideal-diode on the XT-30 connector input can simply be skipped, by soldering over the MOSFET's footprint. This will cause that input to not be protected against power from USB back-flowing into it. The purpose of the ideal-diode is so that a battery will not explode if the battery and USB are both connected at the same time. If you skip the ideal-diode, you lose this protection.

You can also put a large schottky diode over the MOSFET footprint and keep the protection at the cost of some power efficiency loss.

### 3.3V Regulator

If you don't want to pay for a `R-78K3.3` buck converter for the 3.3V power bus, you can install a linear voltage regulator instead. The PCB layout supports the `LM1117T-3.3` or `LD1117V33` footprint by providing an extra pad beside the original `R-78K3.3` footprint. (this only saves about $2, and will result in more internal heat)

If you perform this substitution, when the input voltage reaches below about 14V, instead of providing a warning, the whole device will simply shut down abruptly.
