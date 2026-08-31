# Assembly Supplement

## Power Input MOSFETs

Populate power input MOSFETs, these PowerPAK MOSFETs are hand soldered with a soldering iron (not hot air). For each of the three MOSFETs, follow these steps:

1. brush the bottom of the MOSFET with flux, brush the PCB footprint with flux
2. apply a **very thin** layer of solder to the center pad of the MOSFET footprint on the PCB (not the MOSFET itself)
3. brush some more flux onto the PCB footprint
4. solder pin 1 of the MOSFET to the PCB, ensuring it is on straight
5. solder pins 2, 3, 4 of the MOSFET onto the PCB
6. solder pins 5 thru 8 of the MOSFET all in one go, apply extra solder as the wicking action will pull solder underneath the MOSFET (this is why we prepped the center pad first)

## Skipping Components and Substitutions

None of the following are recommended.

You can obviously just bridge the fuse holder footprint and not use a fuse. Do this at your own risk.

If you only ever want to use the power mode "Sport" mode, then you only need C12 and not C13.

You don't need the SMA coaxial connector if you just solder the cable directly to the PCB.

D3 and D4 are optional. They do not exist in the Radio Thermal design. They are meant to protect Q1 from high V_DS conditions, which could occur if the iron tip is disconnected while the unit is operating. These diodes do exist in the SergeyMax design. Omiting these diodes would save I think maybe $10 total if you order a batch of 5 PCBA jobs.

TVS4, TVS5, TVS6, are all meant for anti-static protection. They are optional.

TVS1 and TVS2 are meant for power line transient protection and a bit of anti-static protection. They are optional.

Q5 is used to protect the AP53781 from battery voltage that is potentially too high. Skip and short this out if you wish. Or substitute with an appropriate schottky diode.

Everything around U3, Q2, and Q3, these are an ideal-diode implementation with inrush limiting. They prevent a battery from exploding if a battery and USB are simultaneously plugged in. They also prevent inrush current from causing a fuse to blow. You can decide to skip these two protections, and maybe also add a NTC inrush limiter.
