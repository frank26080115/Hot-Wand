Designing Hot-Wand was the result of me being inspired by these projects, and then studying them

[SergeyMax's GitHub](https://github.com/SergeyMax/SolderingStation), and SergeyMax's [blog post](https://habr.com/en/articles/451246/) (note: this is a translated version from Russian)

[Radio Thermal's GitHub](https://github.com/RadioThermal/RadioThermal_Soldering_OSHW) and Radio Thermal's [website](https://radiothermal.com/)

At the heart of both is a RF class E amplifier, followed by an impedance matching circuit. Drive the amplifier at the specified frequency and it will output RF power to the iron cartridge.

The Radio Thermal design uses a ATtiny microcontroller to generate the 470 kHz square wave that drives the gate of the MOSFET in the amplifier. There's no power preconditioning or any feedback, nothing fancy, it is extremely simple.

SergeyMax's efforts stemmed from tearing down an actual Metcal soldering station, reverse engineering it, and then building a cheap compact replica. It is way more complicated.

(for personal reasons, I wanted to build a 13.56 MHz version, hence why the extra effort, these reasons may not be totally right in hindsight but that's what I did lol)

SergeyMax's schematic is a bit hard to decode as it is drawn to fit everything onto one sheet of paper and also does not use net flags. Drawing it out again into logical blocks help.

In SergeyMax's design, there are the major sections:

 * AC power input, which provides the DC input to the main buck converter and also has a tapped 10V-ish for the gate driver
 * main buck converter, set to about 20V but adjustable, with high side current measurement
 * gate driver for the second gate driver, weird...
 * a second gate driver stage, this is important
 * the class E RF amplifier
 * the impedance matching network
 * a current amplifier right before the RF output
 * a tip detector that detects high voltages at the RF output and signals the microcontroller
 * linear regulators for the microcontroller
 * microcontroller and LCD screen

I can blow away the AC power input section and replace it with a DC power input quite easily. I don't want to play with wall AC power and would rather delegate that to off-the-shelf USB-PD chargers and power banks.

The Radio Thermal design does not use a buck converter so the RF power input is the unconditioned DC power input. SergeyMax's design uses a buck converter and it's feedback network features an adjustable pot and also has a feedback from the current transformer. The adjustment pot is simply for convience, for lowering the power for testing. The feedback from the current transformer is for stability. If the iron draws more current, the voltage will sag, the buck converter will boost the output in response, which could possibly cause even more current to be drawn. This will result in instability. The signal from the current transformer prevents this as it will cause the feedback voltage to rise with current, and so the actual output voltage will not be boosted.

The other thing is... SergeyMax's design is wall powered so it's gotta have something to take AC power from a transformer and turn it into steady DC. You can't just rectify it and pray, output from a transformer, even if rectified, is unregulated, with substantial ripple and a voltage that varies with load because of the transformer's finite source impedance. That's the other big reason why he has it.

I was debating whether or not I just cut out the buck converter in my design. For my own design, I really wanted to have adjustable power levels. The best way to accomplish this is to have a microcontroller interface with the feedback signal of the buck converter. So I kept it in my design.

SergeyMax's design has two big MOSFETs instead of just one. One of them is the main RF class E amplifier's MOSFET, the other one is part of another smaller RF class E amplifier that drives the gate of the bigger one. The difference is that, we are now playing at 13.56 Mhz not 470 kHz. Driving a MOSFET gate at this high frequency means the charges at the MOSFET gate are moving back and forth through the gate driver much frequently. At 470 kHz, moving these charges from the positive supply into the gate, and then out of the gate into ground, at 4A, is not problematic.

The dilemma is that, the RF amplifier needs a big beefy MOSFET that can survive high currents and high voltages (the reverse engineered unit had a MOSFET rated for 500V), but big silicon means big gates, which means big gate capacitance.

At 13.56 MHz, this becomes upwards of 11W of heat being wasted.

The solution is to use a first stage with a MOSFET chosen to have a very very small gate capacitance, and it can be small because this amplifier only needs to generate about 12V with almost no real current. This MOSFET, along with the inductors and capacitors around it, form another smaller class E amplifier with a resonant tank. This resonant tank is resonanting at 13.56 MHz, bouncing the charges in and out of the gate of the much bigger MOSFET. The charges are being recycled, instead of going in a DC circuit from positive to negative.

This is not easy to get right, the instructions from SergeyMax basically says... make a coreless coil with wire, 10 loops of wire about 6mm in diameter and 10mm wide, you need to stretch or compress this until you tune the resonance correctly.

I plugged this into a simulator and was able to play with the inductance until the current consumption dropped to 200mA at 12V, so 2W vs 11W, not bad? If you go overboard with stretching the coil then you end up with an output waveform either not low enough or not high enough. The wave form must at least reach 0V or else the next MOSFET won't actually ever turn off, which would essentially cause a short circuit event.

SergeyMax's design has a ton of TVS diodes and Zener diodes protecting these MOSFETs. He did explicitly say that the Metcal unit used a 500V rated MOSFET, but... keeping in mind his blog post is from 2019... he found that MOSFET to be too expensive and found a smaller one rated 200V, and just added more 150V TVS diodes. I looked up that bigger MOSFET today... $20 each, so I feel him.

But why did the Metcal use a 500V rated MOSFET and SergeyMax decide that 150V is enough? The circuit isn't actually supposed to let that V_DS voltage get that high in normal operation, when a proper RF soldering iron cartidge is installed, the output impedance is matched and there is a place for all the energy to go. When you unplug the iron cartridge, the output is open circuit and there's nowhere for the energy, stored as magnetic fields in the inductors, to go. The inductors will spike the voltage to hundreds of volts.

Even if you had a detector for if the cartridge is installed or not, your MOSFET must survive this somehow, either by being rated very high, or having a TVS diode buddy taking it.
