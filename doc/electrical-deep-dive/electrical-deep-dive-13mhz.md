# Main Power Buck Converter

This [switching buck converter](https://www.ti.com/product/TPS54560) turns the input supply into the DC voltage that feeds the main RF amplifier. L1/C2 perform the main energy conversion; L2/C4 add a second filter stage to reduce RF returning from the amplifier.

The feedback takeoff is before L2. Its three control inputs are the output divider, the [current-transformer phase detector](../design-study/current-transformer.md), and the microcontroller's attenuation PWM. The transformer branch can move the voltage up or down; the MCU branch is arranged to lower it only.

## U1

Value: TPS54560DDA

[LCSC: C31966](https://www.lcsc.com/product-detail/C31966.html)

This is the main IC that does the switching of the buck converter. Its switching transistor is internal, the switching frequency is adjustable, and its 5 A rating provides enough current for this design. It also has current limiting and will shut down if overheated. See the [TPS54560 documentation](https://www.ti.com/product/TPS54560).

## L1

Value: 33u

[Digi-Key: 744750460220 (22 uH fitted)](https://www.digikey.com/en/products/detail/w%C3%BCrth-elektronik/744750460220/6598151)

The first-stage buck inductor. It stores energy as a magnetic field when current flows through it. When U1 switches off, the collapsing field keeps current flowing through D1 and into the load. The schematic says 33 uH and the actual fitted part is 22 uH. The calculated minimum required is about 16.3 uH.

## L2

Value: 4u7 IHLP2525CZ

[LCSC: C845027](https://www.lcsc.com/product-detail/C845027.html)

The second-stage inductor also stores energy as a magnetic field and resists sudden changes in current. Together with C4, it smooths the RF amplifier supply and reduces RF energy returning to the buck converter. The feedback takeoff is before this stage.

## C2

Value: 100uF 50V

[Digi-Key: EEU-FR1H101B](https://www.digikey.com/en/products/detail/panasonic-industry/EEU-FR1H101B/2504106)

The first-stage output capacitor. It stores energy as electric charge, collecting current from L1 and supplying it to the load between switching pulses. This keeps the voltage at the buck feedback takeoff reasonably steady.

## C4

Value: 100uF 50V

[Digi-Key: EEU-FR1H101B](https://www.digikey.com/en/products/detail/panasonic-industry/EEU-FR1H101B/2504106)

The second-stage output capacitor. It stores energy as electric charge and works with L2 to smooth the supply. It also keeps RF current local to the amplifier supply instead of sending it back through the buck converter.

## D1

Value: STPS3L60S

[LCSC: C2971007](https://www.lcsc.com/product-detail/C2971007.html)

This is the main catch diode for the buck converter. It provides a path for the inductor current when U1's switching transistor turns off. Because the inductor resists sudden changes in current, energy stored in its magnetic field continues flowing into the load until the next switching cycle.

Section 8.2.2.5 in the [TPS54560 documentation](https://www.ti.com/product/TPS54560) talks more about selecting this diode.

## C1

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

The bootstrap capacitor between U1's BOOT and SW pins. Its stored charge powers the internal high-side MOSFET gate driver while the switching node rises.

The [TPS54560 documentation](https://www.ti.com/product/TPS54560) specifies 0.1 uF, a voltage rating of at least 10 V, and an X5R or better dielectric.

## VR1 and R4

VR1 Value: 10K

R4 Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

[Digi-Key: TC33X-2-103E](https://www.digikey.com/en/products/detail/bourns-inc/TC33X-2-103E/612858)

These series resistances form the upper arm of the feedback divider; R8 is the lower arm. Adjust VR1 to set the baseline output, typically around 18-20 V for the intended cartridge. More resistance requests more voltage; the transformer feedback can move the running voltage away from this baseline.

Setting VR1 to its maximum 10 kohms requests the highest output voltage. That is the end of the adjustment range, not necessarily the right setting for the cartridge.

R4 was changed from SergeyMax's 6.8 kohms to 22 kohms because the original value only allowed adjustment up to about 14 V. The larger value lets us reach the intended output range. See the [design notes](../design-study/design-study.md#input-power).

## R37

Value: DNP

An optional fixed replacement for VR1 once its setting is known. This is here for a manufacturing process that does not need every unit tuned by hand. Leave it unpopulated while using the trimmer; fitting both puts them in parallel and changes the setting. See the [tuning notes](../build-guide-13mhz/assembly-supplement.md#solder-jumpers-and-tuning-parts).

## R8

Value: 1K

[LCSC: C21190](https://www.lcsc.com/product-detail/C21190.html)

The lower arm of the feedback divider, from BUCK-FB to ground. Together with R4 and VR1, it sets the baseline output voltage. The other feedback branches add or remove current at this same node.

## C8

Value: 3n3

[LCSC: C1613](https://www.lcsc.com/product-detail/C1613.html)

A small capacitor from the feedback pin to ground, in parallel with R8. It reduces high-frequency noise reaching the feedback input; it also affects the feedback response.

## C3

Value: 15nF (DNP)

An unpopulated feed-forward capacitor across the upper feedback path. If fitted, it would couple output changes directly into BUCK-FB and alter the loop response. It is not another output reservoir or a simple capacitor to ground.

## D2 and R5

D2 Value: BZT52C24

R5 Value: 1K

[D2 LCSC: C2109](https://www.lcsc.com/product-detail/C2109.html)

[R5 LCSC: C21190](https://www.lcsc.com/product-detail/C21190.html)

D2 is a 24 V zener diode. When the first-stage output rises enough for it to conduct, current flows through R5 into the feedback pin, making U1 lower the output. It's a leak into feedback, not a clamp to ground: the idea is to discourage the output from rising past about 24 V.

## D3

Value: SD103A

[LCSC: C7420360](https://www.lcsc.com/product-detail/C7420360.html)

Makes the microcontroller attenuation branch act in one direction. A sufficiently high filtered PWM voltage can inject current into BUCK-FB and lower the output; a low PWM voltage cannot pull feedback down through D3 and request a higher output.

This is a just-in-case protection against firmware mistakes: the microcontroller is allowed to turn the voltage down, but this branch cannot turn it above the level set by the other feedback paths.

## R9

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

The microcontroller sends its attenuation PWM through R9. R9 and C58 form a low-pass filter that smooths the square wave before it reaches D3. R9 also works with R8 and the other feedback resistances to set how strongly that smoothed signal affects the output voltage.

## C58

Value: 3n3

[LCSC: C1613](https://www.lcsc.com/product-detail/C1613.html)

Works with R9 to turn the attenuation PWM into a smoother control voltage. D3 then passes only the part of that voltage that can lower the buck output.

## TP2

A test point on the raw PWR-ATTENU PWM signal, before R9 and C58 smooth it. Useful for checking firmware frequency and duty cycle; it does not show the filtered voltage at D3.

## R6

Value: 270K

[LCSC: C137755](https://www.lcsc.com/product-detail/C137755.html)

Sets U1's switching frequency through the RT/CLK pin. With 270 kohms, the nominal frequency is about 360 kHz. This sets the buck converter's switching rate independently of the 13.56 MHz RF carrier. See the [TPS54560 documentation](https://www.ti.com/product/TPS54560).

## R7 and C7

R7 Value: 22K

[R7 LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

C7 Value: 100n

[C7 LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

The external compensation network on U1's COMP pin. It shapes the control-loop response so the converter settles instead of oscillating. This is different from the chip's internal slope compensation; see section 8.2.2.11 of the [TPS54560 documentation](https://www.ti.com/product/TPS54560).

## F1

Value: 7A 20x5mm

[Digi-Key: 3518P fuse clip](https://www.digikey.com/en/products/detail/keystone-electronics/3518P/316011)

This is the main input fuse. It will blow when something goes wrong (tm). "Something" is a really large problem space here; a power component failing short is one example. The supplier link is for a fuse clip, not the fuse itself.

## C5

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Local input decoupling for the buck converter. It supplies the fast switching-current pulses near U1, while the larger C6 supports slower changes in input demand.

## C6

Value: 470uF

[Digi-Key: UHW1H471MPD](https://www.digikey.com/en/products/detail/nichicon/UHW1H471MPD/3664388)

The bulk input capacitor. It smooths out short power glitches from upstream. If a USB-C host briefly overloads and recovers, this stored energy can keep the microcontroller alive so it recovers quickly too. It only bridges brief interruptions; its stored energy is also why input inrush limiting matters.

## D13

Value: 2A 60V

[LCSC: C53340922](https://www.lcsc.com/product-detail/C53340922.html)

Reverse-current bypass diode for the buck converter. It provides an alternate path for current when the regulator's input power is suddenly disconnected while energy remains stored in the output inductor and capacitors. This limits how far the switching node rises above the input supply, reducing stress on the regulator's internal switching transistor.

## TVS6

Value: SMAJ24CA

[LCSC: C148223](https://www.lcsc.com/product-detail/C148223.html)

This is supposed to catch overvoltage on the final output of the buck converter. The 24 V marking is its stand-off rating; the actual clamping voltage is higher.

Realistically, I don't consider it especially useful as protection for Q1. L3 is just an inductor feeding Q1's drain, and Q1 can handle about 200 V at its drain, with its own D4/TVS1 protection. TVS6 isn't here because Q1 needs a 24 V limit.

Also, the design is supposed to operate at 18 V to 20 V as adjusted by VR1. At 20 V, some of the iron cartridges are heating beyond specifications anyways.

This component is actually here because... if it explodes, it tells me I've done something wrong in the design, or the circuit is damaged. There's a good chance it fails short, which would cause the main fuse to blow. (This isn't a guess...)

## R1, R2, R3

Value: 0R1

[LCSC: C188071](https://www.lcsc.com/product-detail/C188071.html)

Three 0.1 ohm resistors in parallel make an approximately 0.033 ohm current shunt. U5 measures their voltage drop to estimate DC current into the RF amplifier. The parallel arrangement shares heat and gives about 33 mV per amp.

# Main RF Power Amplifier Class E

This generates a powerful 13.56 MHz waveform that powers the soldering iron cartridges. It is fed from the main buck converter output, which is a 20V-ish input, and outputs a sine wave swinging at much higher voltage, like 60V or more depending on loading conditions.

The transistor rapidly switches current through an inductor, storing energy in its magnetic field. When the transistor switches off, the inductor keeps pushing current, charging the surrounding capacitance and raising the voltage. The inductors and capacitors shape this into a repeating RF waveform that delivers energy to the cartridge.

In a class-E amplifier, they also shape the voltage so it falls close to zero before the transistor switches on again, reducing wasted heat. Class-E also a bit simpler than other topologies.

[Keysight's video: How to Design an RF Power Amplifier: Class E](https://www.keysight.com/zz/en/library/demos/how-to-video/how-to-design-an-rf-power-amplifier-class-e-2581786.html)

[All About Circuits: Introduction to the Class E Power Amplifier](https://www.allaboutcircuits.com/technical-articles/introduction-to-the-class-e-power-amplifier/)

The surrounding inductors and capacitors shape its switching waveform and deliver energy into the output network. For the topology and voltage behavior, see the [RF matching study](../design-study/impedance-matching-13.56MHz.md).

## Q1

Value: STP19NF20

[Digi-Key: STP19NF20](https://www.digikey.com/en/products/detail/stmicroelectronics/STP19NF20/1852512)

The main RF power switch.

The important specs for this job are:

* **200 V drain-source rating:** the drain swings much higher than the 18-20 V DC supply, so it needs voltage headroom.
* **0.11 ohm typical on-resistance, 0.16 ohm maximum at a 10 V gate drive:** keeps conduction losses down while the transistor is on.
* **24 nC typical gate charge and 800 pF typical input capacitance:** the gate still takes work to drive at 13.56 MHz. These figures help explain the separate gate-driving amplifier and L8's tuning; a replacement with different gate characteristics would need that tune checked again.
* **TO-220 package:** straightforward to mount against a heatsink so the switching and conduction losses can escape.

These are [ST's datasheet figures](https://www.st.com/en/power-transistors/stp19nf20.html), measured under specified test conditions. The gate charge and capacitance change with operating voltage; they aren't fixed values throughout the RF cycle. See the [gate-driver study](../design-study/gate-driver-method-comparison.md#datasheet-values-used) for more detail.

## L3

Value: 9uH

The drain-feed RF choke. It carries DC into Q1's drain but presents a large impedance to the RF waveform, keeping RF out of the supply. See the [winding instructions](../build-guide-13mhz/custom-inductors.md).

## C9

Value: 100nF

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

One of four parallel coupling capacitors, totaling 400 nF. They pass RF from Q1's drain into L4 while blocking the drain's DC bias. Their RF current and voltage ratings matter. See the [matching-network study](../design-study/impedance-matching-13.56MHz.md#circuit-topology).

## C10

Value: 100nF

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

One of four parallel coupling capacitors, totaling 400 nF. They pass RF from Q1's drain into L4 while blocking the drain's DC bias. Their RF current and voltage ratings matter. See the [matching-network study](../design-study/impedance-matching-13.56MHz.md#circuit-topology).

## C11

Value: 100nF

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

One of four parallel coupling capacitors, totaling 400 nF. They pass RF from Q1's drain into L4 while blocking the drain's DC bias. Their RF current and voltage ratings matter. See the [matching-network study](../design-study/impedance-matching-13.56MHz.md#circuit-topology).

## C12

Value: 100nF

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

One of four parallel coupling capacitors, totaling 400 nF. They pass RF from Q1's drain into L4 while blocking the drain's DC bias. Their RF current and voltage ratings matter. See the [matching-network study](../design-study/impedance-matching-13.56MHz.md#circuit-topology).

## D4

Value: ES1J

[LCSC: C64879](https://www.lcsc.com/product-detail/C64879.html)

Steers positive drain-voltage excursions into TVS1. The series diode also isolates the TVS from part of the RF swing; this pair is a protection path rather than the normal route for output power.

## TVS1

Value: SM15T150CA

[LCSC: C133715](https://www.lcsc.com/product-detail/C133715.html)

Absorbs drain overvoltage through D4, especially during disturbed or disconnected loading. The SM15T150CA is not an exact 150 V clamp: breakdown and clamping depend on current. It cannot absorb continuous excess RF power indefinitely.

## R12

Value: 150Ω 2W

[LCSC: C175287](https://www.lcsc.com/product-detail/C175287.html)

Provides a resistive load from Q1's gate to ground. It damps the resonant gate drive and helps discharge the gate, but deliberately dissipates power while RF is running. See the [gate-driver study](../design-study/gate-driver-method-comparison.md).

## R13

Value: DNP

An optional second gate-load resistor in parallel with R12. Adding it increases damping and dissipation, so it changes the driver tune. Leave it unpopulated unless that change is needed.

## D9

Value: STPS0540Z

[LCSC: C181835](https://www.lcsc.com/product-detail/C181835.html)

Clamps negative excursions of Q1's gate close to ground. The resonant driver can swing below zero; this Schottky diode limits that excursion.

## TVS2

Value: SMBJ15CA

[LCSC: C78809](https://www.lcsc.com/product-detail/C78809.html)

Limits large gate-voltage excursions to protect Q1's gate oxide. Its capacitance and conduction also load the resonant gate drive, so the actual fitted part can affect tuning.

## SJ6

Value: SJ-SMALL-1206-SJ

A source-current measurement position for Q1. It can accept a small shunt during testing; normal builds bridge it. SJ7 must not bypass the shunt during a measurement. See the [assembly notes](../build-guide-13mhz/assembly-supplement.md#solder-jumpers-and-tuning-parts).

## SJ7

Value: SJ-SMALL-1206-SJ

An additional low-resistance connection from Q1's source to ground. Bridge it for normal operation; open it when using SJ6 as a source-current shunt.

# Matching Network

The L4-L6 ladder transforms the cartridge impedance seen by Q1. T1 and its surrounding detector parts also live here: they feed load-dependent phase information back to the buck converter. See the [matching calculations](../design-study/impedance-matching-13.56MHz.md) and [phase-detector study](../design-study/current-transformer.md).

## L4

Value: 180nH

The first series inductor of the output-matching ladder. Together with the C13-C18 bank and the later stages, it transforms the cartridge load seen by Q1. See the [matching-network study](../design-study/impedance-matching-13.56MHz.md).

## C13

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

One of six parallel 100 pF capacitors making the first 600 pF shunt bank after L4. This bank is part of the RF impedance transformation, not supply decoupling.

## C14

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

One of six parallel 100 pF capacitors making the first 600 pF shunt bank after L4. This bank is part of the RF impedance transformation, not supply decoupling.

## C15

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

One of six parallel 100 pF capacitors making the first 600 pF shunt bank after L4. This bank is part of the RF impedance transformation, not supply decoupling.

## C16

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

One of six parallel 100 pF capacitors making the first 600 pF shunt bank after L4. This bank is part of the RF impedance transformation, not supply decoupling.

## C17

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

One of six parallel 100 pF capacitors making the first 600 pF shunt bank after L4. This bank is part of the RF impedance transformation, not supply decoupling.

## C18

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

One of six parallel 100 pF capacitors making the first 600 pF shunt bank after L4. This bank is part of the RF impedance transformation, not supply decoupling.

## C19

Value: DNP

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

An unpopulated tuning position in parallel with C13-C18. Fitting it increases the first shunt-bank capacitance and changes the matching network.

## L5

Value: 400nH

The second series inductor in the matching ladder. It works with the C20-C26 shunt bank and the other stages to transform the cartridge impedance and shape the output waveform.

## C20

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

One of six parallel 47 pF capacitors in the second shunt bank. Together with C26, they total 382 pF and work with L5 and L6 in the impedance-matching ladder.

## C21

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

One of six parallel 47 pF capacitors in the second shunt bank. Together with C26, they total 382 pF and work with L5 and L6 in the impedance-matching ladder.

## C22

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

One of six parallel 47 pF capacitors in the second shunt bank. Together with C26, they total 382 pF and work with L5 and L6 in the impedance-matching ladder.

## C23

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

One of six parallel 47 pF capacitors in the second shunt bank. Together with C26, they total 382 pF and work with L5 and L6 in the impedance-matching ladder.

## C24

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

One of six parallel 47 pF capacitors in the second shunt bank. Together with C26, they total 382 pF and work with L5 and L6 in the impedance-matching ladder.

## C25

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

One of six parallel 47 pF capacitors in the second shunt bank. Together with C26, they total 382 pF and work with L5 and L6 in the impedance-matching ladder.

## C26

Value: 100pF 1000V

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

Adds 100 pF to the six 47 pF capacitors C20-C25, bringing the second shunt bank to 382 pF. This deliberate mixed-value bank is part of the RF match.

## L6

Value: 540nH

The final series inductor in the matching ladder. It works with C27-C31 before RF current passes through T1 to the handpiece connector.

## C27

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

One of five parallel 47 pF capacitors making the final 235 pF shunt bank after L6. This bank completes the output-matching ladder before the current transformer.

## C28

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

One of five parallel 47 pF capacitors making the final 235 pF shunt bank after L6. This bank completes the output-matching ladder before the current transformer.

## C29

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

One of five parallel 47 pF capacitors making the final 235 pF shunt bank after L6. This bank completes the output-matching ladder before the current transformer.

## C30

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

One of five parallel 47 pF capacitors making the final 235 pF shunt bank after L6. This bank completes the output-matching ladder before the current transformer.

## C31

Value: 47pF 1000V

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

One of five parallel 47 pF capacitors making the final 235 pF shunt bank after L6. This bank completes the output-matching ladder before the current transformer.

## C32

Value: DNP

An unpopulated tuning position across the final shunt bank. Fitting it adds capacitance after L6 and changes the impedance presented by the output network.

## JP1

Value: COAX-FTYPE-PCB-MNT

[Digi-Key: 0733300030](https://www.digikey.com/en/products/detail/molex/0733300030/2755903)

The F-type RF output connector for the 13.56 MHz handpiece. Its center contact carries RF power and the small tip-continuity test current; the shell is the return. It is for the 13.56 MHz cartridge family, not the Lite's 470 kHz cartridges.

## T1

Samples RF output current using a one-turn primary and two 14-turn secondaries. With C33 and D5-D8, it forms a phase-sensitive detector that influences the buck supply. Winding polarity matters. See the [transformer study](../design-study/current-transformer.md).

## C33

Value: 10pF 500V

[LCSC: C6629873](https://www.lcsc.com/product-detail/C6629873.html)

Samples RF output voltage capacitively into T1's detector circuit. Its current is phase-shifted relative to output voltage, giving the diode network a reference to compare with output current. See the [phase-detector explanation](../design-study/current-transformer.md).

## D5

Value: 4148

[LCSC: C84367](https://www.lcsc.com/product-detail/C84367.html)

One of four diodes in the phase-sensitive switching network around T1. Together they combine the current sample and C33's voltage-derived reference; this is more than an ordinary bridge rectifier. See the [detector explanation](../design-study/current-transformer.md).

## D6

Value: 4148

[LCSC: C84367](https://www.lcsc.com/product-detail/C84367.html)

One of four diodes in the phase-sensitive switching network around T1. Together they combine the current sample and C33's voltage-derived reference; this is more than an ordinary bridge rectifier. See the [detector explanation](../design-study/current-transformer.md).

## D7

Value: 1N4148W

[LCSC: C84367](https://www.lcsc.com/product-detail/C84367.html)

One of four diodes in the phase-sensitive switching network around T1. Together they combine the current sample and C33's voltage-derived reference; this is more than an ordinary bridge rectifier. See the [detector explanation](../design-study/current-transformer.md).

## D8

Value: 4148

[LCSC: C84367](https://www.lcsc.com/product-detail/C84367.html)

One of four diodes in the phase-sensitive switching network around T1. Together they combine the current sample and C33's voltage-derived reference; this is more than an ordinary bridge rectifier. See the [detector explanation](../design-study/current-transformer.md).

## R10

Value: 100Ω

[LCSC: C22775](https://www.lcsc.com/product-detail/C22775.html)

Works with C34 to remove RF from the detector output. Their nominal 100 ohm and 1 uF values give a corner near 1.6 kHz, leaving a slower control signal for the buck feedback network.

## C34

Value: 1uF

[LCSC: C15849](https://www.lcsc.com/product-detail/C15849.html)

Stores the smoothed phase-detector signal after R10. It removes most of the RF ripple so R11 couples a slowly changing voltage into the buck feedback pin.

## R11

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

Couples the detector into BUCK-FB. A detector voltage above the approximately 0.8 V feedback reference tends to lower the buck output; below it, the branch can raise the output. See the [feedback-polarity investigation](../design-study/investigations/ct-reaction-dummy-load-polarity.md#zero-phase-and-feedback-neutral-are-different-conditions).

## VR4

Value: 5K

[Digi-Key: TC33X-2-502E](https://www.digikey.com/en/products/detail/bourns-inc/TC33X-2-502E/612861)

An optional adjustable path from the detector to BUCK-FB through SJ2. It tunes detector coupling. If R11 remains fitted, the two paths are parallel, so their combined resistance determines the effect.

## SJ2

Connects the optional VR4 feedback path. Leaving it open isolates the trimmer; closing it enables that path alongside R11 unless R11 is removed. See the [tuning notes](../build-guide-13mhz/assembly-supplement.md#solder-jumpers-and-tuning-parts).

## STANDOFF2

Value: STANDOFF-M2.5-4.5MMHEX

A grounded mechanical mounting point for the enclosure standoff. Its electrical connection is to ground; it is not part of the RF impedance transformation.

# RF Amp Gate Driver

This is a small RF amplifier driving the gate of the larger one. U2 drives Q2, and Q2's tuned output drives Q1 through L8. See why the design uses two stages in the [gate-driver comparison](../design-study/gate-driver-method-comparison.md).

## U2

Value: 1EDN8511BXUSA1

[LCSC: C3655509](https://www.lcsc.com/product-detail/C3655509.html)

Turns the microcontroller's logic waveform into strong gate-charge and gate-discharge pulses for Q2. Separate source and sink outputs let R16 and R17 set the two switching paths independently. See the [driver product page](https://www.infineon.com/part/1EDN8511B).

## Q2

Value: IRF510

[Digi-Key: IRF510PBF](https://www.digikey.com/en/products/detail/vishay-siliconix/IRF510PBF/811710)

The small RF amplifier that drives Q1's much larger gate. Its drain circuit and L8 recycle reactive gate energy at 13.56 MHz instead of making U2 drive Q1 directly. See the [driver comparison](../design-study/gate-driver-method-comparison.md).

## R16

Value: 3R6

[LCSC: C6301221](https://www.lcsc.com/product-detail/C6301221.html)

The turn-on gate resistor for Q2, connected to U2's sourcing output. It limits charging current and damps ringing in the gate loop.

## R17

Value: 3R6

[LCSC: C6301221](https://www.lcsc.com/product-detail/C6301221.html)

The turn-off gate resistor for Q2, connected to U2's sinking output. It controls discharge current separately from R16's turn-on path.

## L7

Value: 9uH

Feeds 12 V DC into Q2's drain while impeding RF current from returning to the supply. It is the RF choke for the smaller gate-driving amplifier.

## L8

Value: 180nH

The adjustable air-core inductor in Q1's resonant gate drive. Its inductance interacts with Q1's gate capacitance, so stretching or compressing it changes drive amplitude and losses. Low current alone does not prove a good tune. See the [driver study](../design-study/gate-driver-method-comparison.md).

## C37

Value: 100nF

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

One of two parallel coupling capacitors, totaling 200 nF. They block Q2's drain DC voltage while passing RF into L8 and Q1's gate-drive network.

## C38

Value: 100nF

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

One of two parallel coupling capacitors, totaling 200 nF. They block Q2's drain DC voltage while passing RF into L8 and Q1's gate-drive network.

## C39

Value: 100pF

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

One of two parallel 100 pF capacitors from Q2's drain to ground. Their combined 200 pF, together with device capacitances and the load, shapes the small amplifier's drain waveform.

## C40

Value: 100pF

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

One of two parallel 100 pF capacitors from Q2's drain to ground. Their combined 200 pF, together with device capacitances and the load, shapes the small amplifier's drain waveform.

## R14

Value: 100Ω

[LCSC: C22775](https://www.lcsc.com/product-detail/C22775.html)

A series resistor on the logic input to U2. It damps fast-edge ringing and limits transient current between the microcontroller and the driver input.

## R15

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Pulls U2's input low when the microcontroller pin is not driving it. This gives the RF driver a defined off command during reset or disconnection.

## C35

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Fast local decoupling for U2's 12 V supply. It supplies the brief current pulses used to charge and discharge Q2's gate.

## C36

Value: 1uF

[LCSC: C15849](https://www.lcsc.com/product-detail/C15849.html)

Additional local energy storage beside U2. It supports the fast 100 nF decoupler C35 and reduces supply movement during repeated gate-drive pulses.

## SJ8

Value: SJ-SMALL-1206-SJ

A measurement position in the gate-driver 12 V supply. A temporary shunt here measures driver-branch current while tuning L8; normal builds bridge it. See the [tuning notes](../build-guide-13mhz/assembly-supplement.md#solder-jumpers-and-tuning-parts).

# Tip Detection

A small DC current tests continuity through the handpiece and cartridge while an RF filter keeps the carrier away from Q3. TIP-DET is high for a connected tip and low for a disconnection. See the [detector study](../design-study/tip-detector.md).

## Q3

Value: BC846B

[LCSC: C181138](https://www.lcsc.com/product-detail/C181138.html)

The tip-continuity switch. An open tip lets the bias network drive its base, pulling TIP-DET low; a connected low-resistance cartridge keeps it off, allowing TIP-DET high. See the [tip-detector study](../design-study/tip-detector.md#dc-operation).

## R21

Value: 22Ω

[LCSC: C5204054](https://www.lcsc.com/product-detail/C5204054.html)

Limits current and damps the path from RF-OUT into the continuity detector. With L9 and C42, it keeps RF away from Q3 while allowing the small DC test current through.

## L9

Value: 22uH

[Digi-Key: AIAP-01-220K-T](https://www.digikey.com/en/products/detail/abracon-llc/AIAP-01-220K-T/3059827)

Passes the DC continuity-test current but impedes RF from the handpiece output. With C42, its ideal LC frequency is about 187 kHz, well below the 13.56 MHz carrier.

## C42

Value: 33nF

[LCSC: C21117](https://www.lcsc.com/product-detail/C21117.html)

Bypasses RF at the detector input to ground while retaining the DC continuity voltage. Together with L9 and R21, it stops the RF carrier from becoming a false tip-detect signal.

## VR2

Value: 1K

[Digi-Key: TC33X-2-102E](https://www.digikey.com/en/products/detail/bourns-inc/TC33X-2-102E/612857)

An optional adjustment for the DC test current, normally bypassed with solder. In the normal bypassed configuration R19 sets that current. Changing this path also changes the resistance at which a tip looks disconnected. See the [detector study](../design-study/tip-detector.md#optional-vr2-test-current-adjustment).

## R19

Value: 470Ω

[LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

Feeds the continuity-test current from 3.3 V into the output through the detector filter. A connected cartridge diverts that current to ground; an open circuit lets the detector voltage rise.

## R20

Value: DNP

An unpopulated resistor position in parallel with R19. Fitting it increases the injected test current and changes detector sensitivity; it is not needed for the standard configuration.

## VR3

Value: 1K

[Digi-Key: TC33X-2-102E](https://www.digikey.com/en/products/detail/bourns-inc/TC33X-2-102E/612857)

An optional adjustment in Q3's base path, normally bypassed. R22 supplies the normal base-current limiting, so this trimmer is not required for ordinary operation.

## R22

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

Limits current into Q3's base when the tip is disconnected. This keeps the transistor drive controlled while still allowing it to pull TIP-DET firmly low.

## R18

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Pulls TIP-DET up to 3.3 V when Q3 is off. A high signal means the tip is connected; Q3 pulls it low for a disconnection.

## C41

Value: 3n3

[LCSC: C1613](https://www.lcsc.com/product-detail/C1613.html)

Filters short disturbances on TIP-DET with R18. It adds a small analog settling delay; firmware adds its own qualification before latching a disconnect. See the [detector timing discussion](../design-study/tip-detector.md#firmware-behavior).

# Microcontroller

The controller generates RF timing, supervises the measured supply conditions, and runs the user interface. Its temperature inputs monitor hardware hot spots, rather than measuring the cartridge tip directly.

## U3

Value: STM32F042F6P6

[LCSC: C2969989](https://www.lcsc.com/product-detail/C2969989.html)

The controller: generates the 13.56 MHz RF drive, adjusts buck attenuation, reads voltage/current/temperature signals, and handles the display, button, and fan. The tip regulates its heating through its magnetic material; the MCU does not directly measure tip temperature.

## Y1

Value: 27.12 MHz 10pF

[LCSC: C1986817](https://www.lcsc.com/product-detail/C1986817.html)

The 27.12 MHz crystal reference. The controller derives the 13.56 MHz carrier by dividing this clock by two. A stable clock keeps the RF frequency aligned with the intended cartridge system.

## C55

Value: 10p

[LCSC: C1634](https://www.lcsc.com/product-detail/C1634.html)

The load capacitor on the crystal's oscillator-output side. It works with C56 and stray capacitance to give Y1 its intended operating load; 10 pF here does not mean the total crystal load is 10 pF.

## C56

Value: 10p

[LCSC: C1634](https://www.lcsc.com/product-detail/C1634.html)

The load capacitor on the crystal's oscillator-input side. Together with C55 and PCB/input capacitance, it affects oscillator startup and the actual crystal frequency.

## C53

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Local 3.3 V decoupling for the controller supply. It provides fast current near the MCU rather than forcing switching pulses through long supply traces.

## C54

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

A second local 3.3 V bypass capacitor for the controller's supply connections, including its analog supply. It helps keep digital switching noise out of ADC measurements.

## R33

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Pulls the controller's reset pin high for normal operation. The programming header can pull this same node low to reset the MCU.

## SJ1

Value: SOLDERJUMPERWS

Holds BOOT0 low so the controller starts the application in flash. Normal builds bridge it; changing BOOT0 is only for a deliberate boot-mode change. See the [assembly notes](../build-guide-13mhz/assembly-supplement.md#solder-jumpers-and-tuning-parts).

## JP4

The programming/debug header: exposes 3.3 V reference, ground, SWCLK, SWDIO, and reset. SWDIO is also shared with the fan-control circuit, which SJ3 can disconnect.

## R52

Value: 100Ω

[LCSC: C22775](https://www.lcsc.com/product-detail/C22775.html)

A series resistor on the RF logic output from the MCU, before TP1 and the U2 input path. It reduces fast-edge ringing; it is not a resistor carrying handpiece RF power.

## TP1

Value: TEST-POINT-SQ-2MM

A test point on the RF-GEN logic waveform between R52 and R14. It is useful for checking the 13.56 MHz command, rather than Q1's high-voltage drain waveform.

## U4

Value: SSD1306-4PIN-0.91N-VERT

The 0.91-inch SSD1306 OLED module. The MCU sends status and settings over I2C; match the module's pin order to the PCB rather than relying only on its display size.

## R35

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The I2C SDA pull-up to 3.3 V. Devices normally pull this data wire low or release it, so the resistor supplies its high level.

## R36

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The I2C SCL pull-up to 3.3 V. It restores the clock wire high when released and helps determine the I2C rise time together with bus capacitance.

## C57

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Local reservoir capacitance for the OLED supply. It reduces voltage dips and noise when the display's internal drive circuitry changes its current draw.

## SW1

[Digi-Key: MJTP1236D](https://www.digikey.com/en/products/detail/apem-inc/MJTP1236D/1798058)

The user button. Pressing it pulls BTN to ground; firmware uses the press for menu actions and acknowledgements. Its mounting connection is grounded too.

## D12

Value: 3V6

[LCSC: C840638](https://www.lcsc.com/product-detail/C840638.html)

Transient/ESD protection on the exposed button input. It diverts fast voltage spikes toward ground; it is not a replacement for firmware debouncing.

# Secondary Power Buck Converters

These fixed regulators provide 12 V for the gate driver/fan and 3.3 V for the controller/display/sensors. The nearby LEDs are rail-presence indicators, not firmware status outputs.

## IC1

Value: 12V R-78K12-2.0

[Digi-Key: R-78K12-2.0](https://www.digikey.com/en/products/detail/recom-power/R-78K12-2-0/18093036)

The fixed 12 V regulator module for the gate driver and fan. Its output reaches the 12 V rail through D10, so the rail can be slightly below the module's nominal output.

## IC2

Value: 3V3 R-78K3.3

[Digi-Key: R-78K3.3-1.0](https://www.digikey.com/en/products/detail/recom-power/R-78K3-3-1-0/18093033)

The fixed 3.3 V regulator for the MCU, display, and sensors. A switching module keeps dissipation lower than a linear regulator dropping the full input voltage.

## C43

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Input decoupling on DC-IN near the secondary regulators. It provides a local reservoir and reduces the effect of supply-trace impedance on the regulator inputs.

## D10

Value: 2A

[LCSC: C698873](https://www.lcsc.com/product-detail/C698873.html)

A series diode from IC1 to the 12 V rail. It prevents the charged 12 V rail from feeding backward into IC1 when its input disappears, at the cost of a forward-voltage drop.

## C44

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

The output reservoir on the 12 V rail after D10. It supports changes in gate-driver and fan current.

## D11

[LCSC: C698873](https://www.lcsc.com/product-detail/C698873.html)

A reverse-current bypass from the 3.3 V output back to DC-IN. It limits reverse voltage across IC2 if the input falls while the output capacitor is still charged.

## C45

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Output reservoir capacitance for the 3.3 V rail. It supports brief changes in logic and display load alongside the smaller local decoupling capacitors.

## D14

Value: 3V6

[LCSC: C840638](https://www.lcsc.com/product-detail/C840638.html)

A transient suppressor on the 3.3 V rail. It helps limit brief rail excursions; its 3.6 V designation should not be read as an exact rail-regulation or clamping voltage.

## LED1

Value: LED

A simple DC-IN presence indicator, with current limited by R38. It shows that the rail has voltage, not that the voltage is accurate or that RF power is reaching the tip.

## R38

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

Limits current through LED1 from the DC-IN rail. Its resistance sets brightness and dissipation; the LED must not be connected directly across the supply.

## R39

Value: DNP

An optional resistor position in LED1's path. It is bypassed in the current schematic and normally left unpopulated, so fitting it alone does not add series resistance while that bypass remains.

## LED2

Value: LED

A simple 12 V presence indicator, with current limited by R40. It shows that the rail has voltage, not that the voltage is accurate or that RF power is reaching the tip.

## R40

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Limits current through LED2 from the 12 V rail. Its resistance sets brightness and dissipation; the LED must not be connected directly across the supply.

## LED3

Value: LED

A simple 3.3 V presence indicator, with current limited by R41. It shows that the rail has voltage, not that the voltage is accurate or that RF power is reaching the tip.

## R41

Value: 1K

[LCSC: C21190](https://www.lcsc.com/product-detail/C21190.html)

Limits current through LED3 from the 3.3 V rail. Its resistance sets brightness and dissipation; the LED must not be connected directly across the supply.

## LED4

Value: LED

A simple RF-amplifier supply presence indicator, with current limited by R42. It shows that the rail has voltage, not that the voltage is accurate or that RF power is reaching the tip.

## R42

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

Limits current through LED4 from the RF-amplifier supply rail. Its resistance sets brightness and dissipation; the LED must not be connected directly across the supply.

## R43

Value: DNP

An optional resistor position in LED4's path. It is bypassed in the current schematic and normally left unpopulated; the bypass must change before this resistor can affect LED current.

# Thermistors

Optional NTC sensors report temperatures at the hardware locations where they are attached. Each sensor forms a divider with a 3.3 V pull-up, followed by an ADC filter.

## THERM1

Value: 10K/~3950

[Digi-Key: B57891M0103K000](https://www.digikey.com/en/products/detail/tdk/B57891M0103K000/3500546)

The NTC sensor for the first monitored hot spot. Its resistance falls as it warms, pulling its ADC voltage lower through the divider with R50. It measures device temperature where attached, not the soldering tip temperature.

## R50

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

The 3.3 V pull-up for THERM1. Together they convert temperature-dependent resistance into an ADC voltage; C59 filters noise on the measurement.

## C59

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

A noise filter from the THERM1 ADC input to ground. It smooths electrical pickup while the thermistor and R50 set the measurement's DC level.

## THERM2

Value: 10K/~3950

[Digi-Key: B57891M0103K000](https://www.digikey.com/en/products/detail/tdk/B57891M0103K000/3500546)

The NTC sensor for the second monitored hot spot. Its resistance falls as it warms, pulling its ADC voltage lower through the divider with R51. It measures device temperature where attached, not the soldering tip temperature.

## R51

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

The 3.3 V pull-up for THERM2. Together they convert temperature-dependent resistance into an ADC voltage; C60 filters noise on the measurement.

## C60

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

A noise filter from the THERM2 ADC input to ground. It smooths electrical pickup while the thermistor and R51 set the measurement's DC level.

# Fan CTRL

The solder jumpers select switched ground, direct PWM, or open-drain PWM fan control. The bridge pattern and firmware polarity must agree; see the [fan setup instructions](../build-guide-13mhz/assembly-supplement.md#fan-solder-jumper-selection).

## Q9

Value: Si2318

[LCSC: C14498](https://www.lcsc.com/product-detail/C14498.html)

The fan-control MOSFET. Depending on the solder-jumper configuration, it either switches the fan's ground return or supplies an open-drain signal to the fan's PWM input.

## FAN1

Value: FAN-CONN-X4-CONN-X4

[Digi-Key: 0470533000](https://www.digikey.com/en/products/detail/molex/0470533000/3262217)

The fan connector, supplying 12 V and the selected ground/PWM connections. SJ4 and SJ5 adapt the circuit to two-wire or PWM fans. See the [fan configuration guide](../build-guide-13mhz/assembly-supplement.md#fan-solder-jumper-selection).

## R45

Value: 470Ω

[LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

The series gate resistor for Q9. It limits brief charging current and damps ringing between the MCU signal and the MOSFET gate.

## R44

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Pulls the fan-control signal low when the MCU is not driving it. This gives Q9 a defined command during reset; the fan's actual behavior still depends on the selected jumper mode.

## SJ3

Value: SHORTED

Connects the fan-control circuit to the MCU pin also used for SWDIO. Keep programming access in mind when bridging it. See the [assembly instructions](../build-guide-13mhz/assembly-supplement.md#solder-jumpers-and-tuning-parts).

## SJ4

Selects whether the fan ground is permanently grounded or switched by Q9. Use the documented bridge for the chosen fan mode; do not bridge both alternatives. See the [fan configuration](../build-guide-13mhz/assembly-supplement.md#fan-solder-jumper-selection).

## SJ5

Selects a direct MCU signal or Q9's open-drain output for the fan PWM pin. The open-drain path inverts the signal, so firmware polarity must match. See the [fan configuration](../build-guide-13mhz/assembly-supplement.md#fan-solder-jumper-selection).

## C61

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Local 12 V reservoir at the fan connector. It reduces rail disturbances from fan commutation and startup.

# Power Sensing

U5 measures DC current entering the RF amplifier, and two resistor dividers measure the RF supply and DC input voltages. These support supervision and DC power estimates; they are not a direct measurement of RF power delivered to the cartridge.

## U5

Value: INA138

[LCSC: C47914](https://www.lcsc.com/product-detail/C47914.html)

The high-side current monitor for the RF amplifier's DC supply. It converts the voltage across R1-R3 into an output current, which R25 turns into an ADC voltage. It measures supply current, not RF output power directly. See [INA138 documentation](https://www.ti.com/product/INA138).

## R25

Value: 82K

[LCSC: C23254](https://www.lcsc.com/product-detail/C23254.html)

Sets U5's output gain by turning its output current into a voltage. At 82 kohms, the voltage gain from the shunt is about 16.4, giving roughly 0.547 V per amp with R1-R3.

## C49

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Filters the current-monitor output with R25. It smooths RF pickup and switching ripple before the ADC, while slowing the response to fast current changes.

## C46

Value: 1uF

[LCSC: C15849](https://www.lcsc.com/product-detail/C15849.html)

Local supply bypass for U5 on the positive side of the current shunt. It keeps the monitor's supply quieter without replacing the shunt measurement itself.

## R23

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

The upper resistor of the RF-supply voltage divider. With R24, it scales the rail down by eleven before it reaches the MCU ADC.

## R24

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

The lower resistor of the RF-supply voltage divider, from the ADC node to ground. Its 2.2 kohms with R23's 22 kohms gives an 11:1 ratio.

## C47

Value: 100n

Filters the RF-supply voltage ADC input with R23 and R24. It reduces switching/RF noise so firmware sees a steadier supply-voltage measurement.

## R26

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

The upper resistor of the DC-IN voltage divider. It lets firmware monitor the input after F1; R27 completes the 11:1 scaling.

## R27

Value: 2K2

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

The lower resistor of the DC-IN voltage divider. It returns the ADC node to ground and, with R26, scales a 36 V input to about 3.27 V.

## C48

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Filters the DC-IN ADC signal. Together with R26 and R27, it reduces noise while still allowing firmware to detect changes in the input supply.

# XT30 DC Input

U6 and its two MOSFETs form the protected battery/DC path. They block reverse current, support input disconnection, and slow startup to limit inrush. See the [LM74810-Q1 overview](https://www.ti.com/product/LM7481-Q1).

## U6

Value: LM74810QDRRRQ1

[LCSC: C3215601](https://www.lcsc.com/product-detail/C3215601.html)

The ideal-diode and disconnect controller for the XT30 input. It drives Q4/Q5 to limit reverse current and provide controlled startup. Q8 disables this input when USB power is present. See the [LM74810-Q1 documentation](https://www.ti.com/product/LM7481-Q1).

## Q4

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

The input-side MOSFET controlled by U6's DGATE pin. It provides the low-loss ideal-diode path from XT30 to the common-drain node and helps block reverse current toward the battery.

## Q5

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

The output-side MOSFET controlled by U6's HGATE pin. With Q4, it allows the controller to disconnect the XT30 path; its controlled turn-on also limits capacitor-charging inrush.

## JP3

Value: XT-CONNECTOR-XT30PW

The XT30 DC-input connector. Its positive pin enters the protected Q4/Q5 path; it does not connect straight to the RF amplifier supply.

## TVS3

Value: 36V

[LCSC: C134984](https://www.lcsc.com/product-detail/C134984.html)

A transient suppressor across the XT30 input. Its 36 V stand-off rating is not an operating-voltage recommendation or an exact clamp level; downstream component ratings still limit the allowed supply.

## C50

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Local high-frequency bypass across the XT30 input. It helps absorb fast disturbances near the connector and U6's input sensing.

## C51

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

U6's charge-pump reservoir between CAP and the common-drain supply node. It supports gate drive above the MOSFET source voltage. See the [controller documentation](https://www.ti.com/product/LM7481-Q1).

## C52

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Decouples U6's common-drain supply node to ground. It provides local current for the controller and reduces supply noise during gate switching.

## R56

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

The series resistor in U6's HGATE slew-control branch. Together with C62 and C63, it shapes turn-on behavior rather than carrying the main load current.

## C62

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

One of two series 100 nF capacitors in the HGATE slew-control branch, giving about 50 nF overall. They slow Q5 turn-on to reduce bulk-capacitor inrush and share voltage stress. See the [controller's inrush guidance](https://www.ti.com/product/LM7481-Q1).

## C63

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

One of two series 100 nF capacitors in the HGATE slew-control branch, giving about 50 nF overall. They slow Q5 turn-on to reduce bulk-capacitor inrush and share voltage stress. See the [controller's inrush guidance](https://www.ti.com/product/LM7481-Q1).

## R53

Value: 82K

[LCSC: C23254](https://www.lcsc.com/product-detail/C23254.html)

The upper resistor of U6's overvoltage-sense divider. With R54 in parallel with R55, it sets the nominal cutoff near 39.5 V. That threshold does not make every downstream component safe at 39 V.

## R54

Value: 3K

[LCSC: C4211](https://www.lcsc.com/product-detail/C4211.html)

One of the two parallel lower resistors in U6's overvoltage divider. Its 3 kohms in parallel with R55 gives about 2.64 kohms.

## R55

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

The 22 kohm resistor in parallel with R54. It trims the effective lower-divider resistance and therefore the XT30 overvoltage cutoff threshold.

## R34

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

Pulls U6's enable input up from the common-drain supply. This enables the XT30 path when Q8 is off; USB voltage turns Q8 on and overrides this pull-up.

# USB-PD Input

U7 negotiates USB-C power and controls the USB power path. A separate USB-presence circuit disables the XT30 path. The resistor settings request a profile; the charger and cable still have to support it. See [AP53781 documentation](https://www.diodes.com/part/view/AP53781).

## U7

Value: AP53781

[LCSC: C52730979](https://www.lcsc.com/product-detail/C52730979.html)

The USB-PD controller, configured as a power consumer. It negotiates a voltage/current request from its resistor settings and drives the external input MOSFETs. The requested profile still depends on charger and cable support. See the [AP53781 product page](https://www.diodes.com/part/view/AP53781).

## Q6

Value: DMTH64M2LPDWQ-13

[Digi-Key: DMTH64M2LPDWQ-13](https://www.digikey.com/en/products/detail/diodes-incorporated/DMTH64M2LPDWQ-13/28171648)

Two MOSFETs in one package, connected back-to-back in the USB power path. U7 drives their common gate to admit negotiated power while allowing the path to block when disabled.

## Q7

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

An additional controlled isolation MOSFET between the shared DC bus and the USB switching circuit. It helps keep high battery voltage away from the AP53781's lower-voltage circuitry when USB is not supplying power.

## JP2

Value: USBC-CHGONLY-4M-DRILL

[Digi-Key: UJC-H-G-SMT-2-P6-TR](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/UJC-H-G-SMT-2-P6-TR/24818567)

The charge-only USB-C connector. VBUS supplies power and CC1/CC2 carry USB-C/PD negotiation; this connector does not provide a general-purpose USB data interface.

## R31

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

Part of the series path between U7's VOUT sense pin and the Q6/Q7 junction. With R32, it limits current into the sense connection during disturbances; it is not in the main high-current supply path.

## R32

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

The second resistor in U7's VOUT sensing path. Together with R31, it isolates the controller's sense pin from the power-switch junction without adding resistance to the load-current path.

## R30

Value: 4K3

[LCSC: C23159](https://www.lcsc.com/product-detail/C23159.html)

The series resistor from U7's VG_A output to the power-MOSFET gates. It limits gate-charging current and affects turn-on speed, including the initial capacitor-charging surge.

## R47

Value: 39K

[LCSC: C23153](https://www.lcsc.com/product-detail/C23153.html)

The default VSEL programming resistor. Its 39 kohm path requests the documented 20 V profile; grounding the alternate JP5 path adds a parallel resistance to select a different request.

## R48

Value: 3K

[LCSC: C4211](https://www.lcsc.com/product-detail/C4211.html)

Part of the alternate VSEL branch with R49. Grounding that branch through JP5 puts their series resistance in parallel with R47, giving approximately 26 kohms overall for the 28 V request.

## R49

Value: 75K

[LCSC: C23242](https://www.lcsc.com/product-detail/C23242.html)

Completes the alternate VSEL branch from R48 to JP5. With R47, this is a resistor-code selection for USB-PD negotiation, not a divider regulating the power-bus voltage.

## JP5

The USB-PD selection header. The voltage shunt enables the alternate 28 V resistance code; the current shunt grounds ISEL directly for a 5 A request. With the current shunt open and R46/R46B unpopulated, current selection is automatic. Move shunts with power disconnected.

## R46

Value: DNP

An optional hardwired ISEL programming branch, in series with R46B to ground. Their total resistance can set a fixed current request; JP5's direct-to-ground current shunt overrides that branch. With both positions unpopulated and no shunt, ISEL uses the automatic setting.

## R46B

Value: DNP

An optional second resistor from the ISEL programming path to ground. It can provide a fixed current-selection resistance; its DNP default is an open circuit, not a zero-ohm link.

## Q8

Value: Si2318

[LCSC: C14498](https://www.lcsc.com/product-detail/C14498.html)

Detects USB VBUS through R29/R28 and pulls U6's enable low. This gives USB priority over the XT30 path and helps prevent USB power from feeding a connected battery.

## R28

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The lower resistor of Q8's USB-presence divider and a gate pull-down. It makes Q8 turn off when VBUS is absent, allowing the XT30 controller to enable again.

## R29

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The upper resistor feeding USB VBUS into Q8's gate divider. With R28, it reduces the gate voltage and limits current into the TVS protection path.

## TVS4

Value: 15V

[LCSC: C2987151](https://www.lcsc.com/product-detail/C2987151.html)

Protects Q8's gate from excessive voltage and fast spikes on the USB-presence signal. Its placement protects the small detection MOSFET, not the whole USB power bus.

## TVS5

Value: 36V

[LCSC: C134984](https://www.lcsc.com/product-detail/C134984.html)

A transient suppressor across USB VBUS. It deals with short spikes; it does not regulate the negotiated voltage or make AP53781's pins tolerant of continuous overvoltage.

## C64

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Decouples U7's internal V5V output. This is a local controller-support capacitor, not the main USB input reservoir. See the [AP53781 documentation](https://www.diodes.com/part/view/AP53781).
