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

### Input Power

I can blow away the AC power input section and replace it with a DC power input quite easily. I don't want to play with wall AC power and would rather delegate that to off-the-shelf USB-PD chargers and power banks.

The Radio Thermal design does not use a buck converter so the RF power input is the unconditioned DC power input. SergeyMax's design uses a buck converter and it's feedback network features an adjustable pot and also has a feedback from the current transformer. The adjustment pot is simply for convience, for lowering the power for testing. The feedback from the current transformer is for stability. If the iron draws more current, the voltage will sag, the buck converter will boost the output in response, which could possibly cause even more current to be drawn. This will result in instability. The signal from the current transformer prevents this as it will cause the feedback voltage to rise with current, and so the actual output voltage will not be boosted.

The other thing is... SergeyMax's design is wall powered so it's gotta have something to take AC power from a transformer and turn it into steady DC. You can't just rectify it and pray, output from a transformer, even if rectified, is unregulated, with substantial ripple and a voltage that varies with load because of the transformer's finite source impedance. That's the other big reason why he has it.

I was debating whether or not I just cut out the buck converter in my design. For my own design, I really wanted to have adjustable power levels. The best way to accomplish this is to have a microcontroller interface with the feedback signal of the buck converter. So I kept it in my design.

### Gate Driver

SergeyMax's design has two big MOSFETs instead of just one. One of them is the main RF class E amplifier's MOSFET, the other one is part of another smaller RF class E amplifier that drives the gate of the bigger one. The difference is that, we are now playing at 13.56 Mhz not 470 kHz. Driving a MOSFET gate at this high frequency means the charges at the MOSFET gate are moving back and forth through the gate driver much frequently. At 470 kHz, moving these charges from the positive supply into the gate, and then out of the gate into ground, at 4A, is not problematic.

The dilemma is that, the RF amplifier needs a big beefy MOSFET that can survive high currents and high voltages (the reverse engineered unit had a MOSFET rated for 500V), but big silicon means big gates, which means big gate capacitance.

At 13.56 MHz, this becomes upwards of 11W of heat being wasted.

The solution is to use a first stage with a MOSFET chosen to have a very very small gate capacitance, and it can be small because this amplifier only needs to generate about 12V with almost no real current. This MOSFET, along with the inductors and capacitors around it, form another smaller class E amplifier with a resonant tank. This resonant tank is resonanting at 13.56 MHz, bouncing the charges in and out of the gate of the much bigger MOSFET. The charges are being recycled, instead of going in a DC circuit from positive to negative.

This is not easy to get right, the instructions from SergeyMax basically says... make a coreless coil with wire, 10 loops of wire about 6mm in diameter and 10mm wide, you need to stretch or compress this until you tune the resonance correctly.

I plugged this into a simulator and was able to play with the inductance until the current consumption dropped to 200mA at 12V, so 2W vs 11W, not bad? If you go overboard with stretching the coil then you end up with an output waveform either not low enough or not high enough. The wave form must at least reach 0V or else the next MOSFET won't actually ever turn off, which would essentially cause a short circuit event.

### Protections

SergeyMax's design has a ton of TVS diodes and Zener diodes protecting these MOSFETs. He did explicitly say that the Metcal unit used a 500V rated MOSFET, but... keeping in mind his blog post is from 2019... he found that MOSFET to be too expensive and found a smaller one rated 200V, and just added more 150V TVS diodes. I looked up that bigger MOSFET today... $20 each, so I feel him.

But why did the Metcal use a 500V rated MOSFET and SergeyMax decide that 150V is enough? The circuit isn't actually supposed to let that V_DS voltage get that high in normal operation, when a proper RF soldering iron cartidge is installed, the output impedance is matched and there is a place for all the energy to go. When you unplug the iron cartridge, the output is open circuit and there's nowhere for the energy, stored as magnetic fields in the inductors, to go. The inductors will spike the voltage to hundreds of volts.

Even if you had a detector for disconnection of the cartridge, your MOSFET must survive this spike somehow, either by being rated very high, or having a TVS diode buddy taking it. It does not matter how fast your tip detection circuit works, the energy is already stored, if it has no where to go, even shutting down the RF generator does stop the voltage spike from happening.

### Microcontroller

The last bit I needed to study is how the microcontroller is generating the 13.56 MHz signal. SergeyMax used a STM32F030F4P6 and a 27.12 MHz crystal in the circuit. The STM32 uses its timer to generate a PWM signal with period of 2 system clock ticks, resulting in a 13.56 MHz square wave.

While SergeyMax used a classic HD44780 chipset 16x2 LCD screen, I wanted to use a SSD1306 chipset OLED screen, which means using I2C. SergeyMax used one of the I2C pins for the PWM generation so I had to reassign the PWM generation pin to another timer channel, not a huge deal. Writing out the code a bit while integrating the U8g2 graphics library into it, it seemed likely that my code will exceed 16kb, so I made sure my circuit is compatible with both the STM32F030F4P6 and the STM32F042F6P6, which has the same footprint but 32kb of flash memory.

Fun fact... more modern STM32 families, like STM32C or STM32G, don't offer the same pin-out or crystal inputs for their smaller packages. But luckily, while exceeding 16kb, the firmware is still under 32kb, so I don't need to upgrade beyond a STM32F042.

With all the pins assigned, I still had two ADC capable pins left, so I added footprints to connect two NTC thermistors, just in case I want to monitor temperatures in the circuit somewhere.

### DC Power Input

The power input in my design is my own design, it is not derived from either of the preceding projects.

For battery power, I put in a XT-30 connector in my design, as the choice is between XT-30 and XT60, and I simply will more likely need it to be XT-30. My own small combat robots all use XT-30 and our team's bigger robots use other much bigger connectors.

For USB-C power input, it uses a USB-PD negotiation IC [AP53781](https://www.diodes.com/part/view/AP53781), compatible with PD3.2, to obtain 28V (from EPR, Extended Power Range). This IC is quite simple, you pick the voltage and current using two resistors. You can also pick "automatic" for the current. The hope is that, if 28V is unavailable from the connected charger, then it will fall back to the next lower voltage, which is 20V.

I chose to use a USB-C charge-only connector to simplify the layout, which means there are no data signals. This means the connector is incompatible with Qualcomm QuickCharge. AP53781 can only negotiate QC up to version 3.0 which is capped at 36W, way too low anyways. So it's not a big deal to not have USB data signals.

With two power inputs, I need one to not explode the other, especially the battery. That's why I added an ideal-diode controller to the circuit. If the USB power is detected, the ideal-diode controller is simply shut down via its enable pin. This prevents 28V from USB from making a 20V lithium battery explode if somebody plugs in both. And vice versa, a battery is also not able to damage a USB charger because the ideal-diode controller would be shut down.

I am not using a 10V tap from a AC transformer like SergeyMax, instead, I've placed a 12V fixed voltage buck converter in the circuit. This powers the gate driver and the cooling fan. The microcontroller is also powered by a similar 3.3V buck converter. These are small and compatible with 78XX series voltage regulators.

The Radio Thermal design features a NTC in-rush limiter on the barrel jack. In my design, the AP53781 slowly opens the MOSFET that gates the VBUS power. When the MOSFETs connect, the in-rush towards the input bulk capacitor should not exceed 2A. If 2A is tripping the USB power supply then we have bigger issues. The RF amplifier should not cause a large in-rush when starting. The MOSFET will close and the inductor will essentially block the current surge that you would expect.

Leaving out a barrel jack in my design is a deliberate decision.

I also added a classic glass fuse in my design. I expect it to be appreciated during a short circuit event or a MOSFET failure.

### Thermal and Cooling

SergeyMax talked about how the buck converter needed ground via stitching under the IC's exposed bottom pad, plus the vendor uses 2oz copper. SergeyMax's own layout does have the ground stitching, and he even put in a lot of effort making sure the ground plane isn't partitioned near the buck converter.

In my own design, I follow the same practices, and in addition, I put a heatsink and standoff in the area near the buck converter IC. The standoff is made of brass and will conduct some heat from that area of copper to the aluminum enclosure.

SergeyMax's photos shows he's put small generic heatsinks onto the backs of the MOSFETs. I have also put heatsinks on my MOSFETs, and the heatsinks are pressed against the aluminum enclosure for extra effectiveness. I do have to bend and cut my heatsinks so they fit in the design though, a bit inconvenient.

The Radio Thermal design uses a decently sized cooling fan, but when I talked with the authors in person during their demo, they said it will run without the fan. However, without cooling, the magnetic properties of the inductor toroids will change and the whole circuit will go out of tune slightly, causing it to be more inefficient.

In my own design, I had plenty of PCB space and physical space to spare, so I added a small 20mm fan. It is controlled by a MOSFET driven by the microcontroller. The user can choose to leave the fan off, or always on, or use temperature sensors to determine when the fan should run.
