## Soldering

Most of the PCB is assembled by JLCPCB's PCBA service, but only for the bottom side of the board. The SMT and through-hole components on the top side of the board are expected to be soldered on by the hobbyist performing the DIY build.

Please see the detailed BOM file to see which components are soldered by whom, and where to get the parts.

The 22uH axial inductor needs to be raised slightly above the PCB.

There are several LED indicators on the top side of the board, they simply indicate if a particular power bus actually has power, they are not required.

The following solder jumpers must be shorted out (soldered over) by you (unless you need them open for tuning):

 * TODO

## Custom Inductors

## Enclosure Cutouts

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

## Finaly Assembly

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

If you don't want to pay for a `R-78K3.3` buck converter for the 3.3V power bus, you can install a linear voltage regulator instead. The PCB layout supports the `LM1117T-3.3` or `LD1117V33` footprint by providing an extra pad beside the original `R-78K3.3` footprint.

If you perform this substitution, when the input voltage reaches below about 14V, instead of providing a warning, the whole device will simply shut down abruptly.
