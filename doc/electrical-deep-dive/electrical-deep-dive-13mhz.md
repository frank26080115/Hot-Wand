# Main Power Buck Converter

This [switching buck converter](https://www.ti.com/product/TPS54560) turns the input supply into the DC voltage that feeds the main RF amplifier. L1/C2 perform the main energy conversion; L2/C4 add a second filter stage to reduce RF returning from the amplifier.

The feedback takeoff is before L2. Its three control inputs are the output divider, the [current-transformer phase-detector](../design-study/current-transformer.md), and the microcontroller's attenuation PWM. The transformer branch can move the voltage up or down; the microcontroller branch is arranged to lower it only.

The circuit design here was initiated by copying the original SergeyMax design and then evolved to improve upon it.

## U1

Value: TPS54560DDA

[LCSC: C31966](https://www.lcsc.com/product-detail/C31966.html)

This is the main IC that does the switching of the buck converter. Its switching transistor is internal, the switching frequency is adjustable, and its 5 A rating provides enough current for this design. It also has current limiting and will shut down if overheated. See the [TPS54560 documentation](https://www.ti.com/product/TPS54560).

## L1

Value: 33μH

[Digi-Key: 744750460220 (22μH fitted)](https://www.digikey.com/en/products/detail/w%C3%BCrth-elektronik/744750460220/6598151)

A purchased first-stage buck inductor. It stores energy as a magnetic field when current flows through it. When U1 switches off, the collapsing field keeps current flowing through D1 and into the load. The schematic says 33μH and the actual fitted part is 22μH. The calculated minimum required is about 16.3μH.

## L2

Value: 4.7μH IHLP2525CZ

[LCSC: C845027](https://www.lcsc.com/product-detail/C845027.html)

The purchased second-stage inductor also stores energy as a magnetic field and resists sudden changes in current. Together with C4, it smooths the RF amplifier supply and reduces RF energy returning to the buck converter. The feedback takeoff is before this stage.

## C2

Value: 100μF 50V

[Digi-Key: EEU-FR1H101B](https://www.digikey.com/en/products/detail/panasonic-industry/EEU-FR1H101B/2504106)

The first-stage output capacitor. It stores energy as electric charge, collecting current from L1 and supplying it to the load between switching pulses. This keeps the voltage at the buck feedback takeoff reasonably steady.

## C4

Value: 100μF 50V

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

The [TPS54560 documentation](https://www.ti.com/product/TPS54560) specifies 0.1μF, a voltage rating of at least 10 V, and an X5R or better dielectric.

## VR1 and R4

VR1 Value: 10kΩ

R4 Value: 22kΩ

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

[Digi-Key: TC33X-2-103E](https://www.digikey.com/en/products/detail/bourns-inc/TC33X-2-103E/612858)

These series resistances form the upper arm of the feedback divider; R8 is the lower arm. Adjust VR1 to set the baseline output, typically around 18-20 V for the intended cartridge. More resistance requests more voltage; the transformer feedback can move the running voltage away from this baseline.

Setting VR1 to its maximum 10kΩ requests the highest output voltage. That is the end of the adjustment range, not necessarily the right setting for the cartridge.

R4 was changed from SergeyMax's 6.8kΩ to 22kΩ because the original value only allowed adjustment up to about 14 V. The larger value lets us reach the intended output range. See the [design notes](../design-study/design-study.md#input-power).

## R37

Value: DNP

An optional fixed replacement for VR1 once its setting is known. This is here for a manufacturing process that does not need every unit tuned by hand. Leave it unpopulated while using the trimmer; fitting both puts them in parallel and changes the setting. See the [tuning notes](../build-guide-13mhz/assembly-supplement.md#solder-jumpers-and-tuning-parts).

## R8

Value: 1kΩ

[LCSC: C21190](https://www.lcsc.com/product-detail/C21190.html)

The lower arm of the feedback divider, from the buck converter's feedback net (`BUCK-FB`) to ground. Together with R4 and VR1, it sets the baseline output voltage. The detector also feeds this connection through [R11](#r11); R8 and R11 work with the upper divider to set how strongly the detector influences the buck output.

## C8

Value: 3.3nF

[LCSC: C1613](https://www.lcsc.com/product-detail/C1613.html)

A small capacitor from the feedback pin to ground, in parallel with R8. It reduces high-frequency noise reaching the feedback input; it also affects the feedback response.

## C3

Value: 15nF (DNP)

An unpopulated feed-forward capacitor across the upper feedback path. If fitted, it would couple output changes directly into the buck converter's feedback net (`BUCK-FB`) and alter the loop response. It is not another output reservoir or a simple capacitor to ground.

## D2 and R5

D2 Value: BZT52C24

R5 Value: 1kΩ

[D2 LCSC: C2109](https://www.lcsc.com/product-detail/C2109.html)

[R5 LCSC: C21190](https://www.lcsc.com/product-detail/C21190.html)

D2 is a **24 V zener diode**, specified at **22.8-25.6 V with 5 mA flowing through it** for the listed JSCJ part. Its voltage also changes with current, so 24 V is a nominal value.

When the first-stage output rises enough for D2 to conduct, current flows through R5 into the feedback pin, making U1 lower the output. It's a leak into feedback, not a clamp to ground: the idea is to discourage excessive output voltage rather than impose an exact 24 V limit.

## D3

Value: SD103A

[LCSC: C7420360](https://www.lcsc.com/product-detail/C7420360.html)

Makes the microcontroller attenuation branch act in one direction. A sufficiently high filtered PWM voltage can inject current into the buck converter's feedback net (`BUCK-FB`) and lower the output; a low PWM voltage cannot pull feedback down through D3 and request a higher output.

This is a just-in-case protection against firmware mistakes: the microcontroller is allowed to turn the voltage down, but this branch cannot turn it above the level set by the other feedback paths.

## R9

Value: 2.2kΩ

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

The microcontroller sends its attenuation PWM through R9. R9 and C58 form a low-pass filter that smooths the square wave before it reaches D3. R9 also works with R8 and the other feedback resistances to set how strongly that smoothed signal affects the output voltage.

## C58

Value: 3.3nF

[LCSC: C1613](https://www.lcsc.com/product-detail/C1613.html)

Works with R9 to turn the attenuation PWM into a smoother control voltage. D3 then passes only the part of that voltage that can lower the buck output.

## TP2

A test point on the microcontroller's power-attenuation control net (`PWR-ATTENU`), carrying the raw PWM signal before R9 and C58 smooth it. Useful for checking firmware frequency and duty cycle; it does not show the filtered voltage at D3.

## R6

Value: 270kΩ

[LCSC: C137755](https://www.lcsc.com/product-detail/C137755.html)

Sets U1's switching frequency through the RT/CLK pin. With 270kΩ, the nominal frequency is about 360 kHz. This sets the buck converter's switching rate independently of the 13.56 MHz RF carrier. See the [TPS54560 documentation](https://www.ti.com/product/TPS54560).

## R7 and C7

R7 Value: 22kΩ

[R7 LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

C7 Value: 100nF

[C7 LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

The external compensation network on U1's COMP pin. It shapes the control-loop response so the converter settles instead of oscillating. This is different from the chip's internal slope compensation; see section 8.2.2.11 of the [TPS54560 documentation](https://www.ti.com/product/TPS54560).

## F1

Value: 7A 20×5mm

[Digi-Key: 3518P fuse clip](https://www.digikey.com/en/products/detail/keystone-electronics/3518P/316011)

This is the main input fuse. It will blow when something goes wrong (tm). "Something" is a really large problem space here; a power component failing short is one example. The supplier link is for a fuse clip, not the fuse itself.

## C5

Value: 10μF

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Local input decoupling for the buck converter. It supplies the fast switching-current pulses near U1, while the larger C6 supports slower changes in input demand.

## C6

Value: 470μF

[Digi-Key: UHW1H471MPD](https://www.digikey.com/en/products/detail/nichicon/UHW1H471MPD/3664388)

The bulk input capacitor. It smooths out short power glitches from upstream. If a USB-C host briefly overloads and recovers, this stored energy can keep the microcontroller alive so it recovers quickly too. It only bridges brief interruptions; its stored energy is also why input inrush limiting matters.

## D13

Value: 2A 60V

[LCSC: C53340922](https://www.lcsc.com/product-detail/C53340922.html)

Reverse-current bypass diode for the buck converter. It provides an alternate path for current when the regulator's input power is suddenly disconnected while energy remains stored in the output inductor and capacitors. This limits how far the switching node rises above the input supply, reducing stress on the regulator's internal switching transistor.

## TVS6

Value: SMAJ24CA

[LCSC: C148223](https://www.lcsc.com/product-detail/C148223.html)

This is supposed to catch overvoltage on the final output of the buck converter. The SMAJ24CA has a **24 V stand-off rating**, **26.7-29.5 V breakdown at 1 mA**, and a **38.9 V maximum clamp at 10.3 A** for a 10/1000μs surge pulse. That is why the 24 V marking doesn't mean it holds the output at 24 V. Its **400 W rating applies to that brief surge waveform**, not continuous power.

Realistically, I don't consider it especially useful as protection for Q1. L3 is just an inductor feeding Q1's drain, and Q1 can handle about 200 V at its drain, with its own D4/TVS1 protection. TVS6 isn't here because Q1 needs a 24 V limit.

Also, the design is supposed to operate at 18 V to 20 V as adjusted by VR1. At 20 V, some of the iron cartridges are heating beyond specifications anyways.

This component is actually here because... if it explodes, it tells me I've done something wrong in the design, or the circuit is damaged. There's a good chance it fails short, which would cause the main fuse to blow. (This isn't a guess...)

## R1, R2, R3

Value: 0.1Ω

[LCSC: C188071](https://www.lcsc.com/product-detail/C188071.html)

Three 0.1Ω resistors in parallel make an approximately 0.033Ω current shunt. U5 measures their voltage drop to estimate DC current into the RF amplifier. The parallel arrangement shares heat and gives about 33 mV per amp.

# Main RF Power Amplifier Class E

This generates a powerful 13.56 MHz waveform that powers the soldering iron cartridges. It is fed from the main buck converter output, which is a 20V-ish input, and outputs a sine wave swinging at much higher voltage, like 60V (napkin math says 60V, simulation says up to 120V normally) or more depending on loading conditions.

The transistor rapidly switches current through an inductor, storing energy in its magnetic field. When the transistor switches off, the inductor keeps pushing current, charging the surrounding capacitance and raising the voltage. The inductors and capacitors shape this into a repeating RF waveform that delivers energy to the cartridge.

In a class-E amplifier, they also shape the voltage so it falls close to zero before the transistor switches on again, reducing wasted heat. Class-E also a bit simpler than other topologies.

[Keysight's video: How to Design an RF Power Amplifier: Class E](https://www.keysight.com/zz/en/library/demos/how-to-video/how-to-design-an-rf-power-amplifier-class-e-2581786.html)

[All About Circuits: Introduction to the Class E Power Amplifier](https://www.allaboutcircuits.com/technical-articles/introduction-to-the-class-e-power-amplifier/)

The surrounding inductors and capacitors shape its switching waveform and deliver energy into the output network. For the topology and voltage behavior, see the [RF matching study](../design-study/impedance-matching-13.56MHz.md).

The circuit design here was initiated by copying the original SergeyMax design and then evolved to improve upon it.

## Q1

Value: STP19NF20

[Digi-Key: STP19NF20](https://www.digikey.com/en/products/detail/stmicroelectronics/STP19NF20/1852512)

The main RF power switch.

The important specs for this job are:

* **200 V drain-source rating:** the drain swings much higher than the 18-20 V DC supply, so it needs voltage headroom.
* **0.11Ω typical on-resistance, 0.16Ω maximum at a 10 V gate drive:** keeps conduction losses down while the transistor is on.
* **24 nC typical gate charge and 800pF typical input capacitance:** the gate still takes work to drive at 13.56 MHz. These figures help explain the separate gate-driving amplifier and L8's tuning; a replacement with different gate characteristics would need that tune checked again.
* **TO-220 package:** straightforward to mount against a heatsink so the switching and conduction losses can escape.

These are [ST's datasheet figures](https://www.st.com/en/power-transistors/stp19nf20.html), measured under specified test conditions. The gate charge and capacitance change with operating voltage; they aren't fixed values throughout the RF cycle. See the [gate-driver study](../design-study/gate-driver-method-comparison.md#datasheet-values-used) for more detail.

## L3

Value: 9μH

A hand-wound RF choke using a [Fair-Rite 5961004901](https://fair-rite.com/product/toroids-5961004901/) material-61 ferrite toroid and 22 AWG enamelled wire. It carries DC into Q1's drain but presents a large impedance to the RF waveform, keeping RF out of the supply. See the [winding instructions](../build-guide-13mhz/custom-inductors.md#9-uh-choke).

Note that "over-winding" it slightly above 9μH is acceptable, don't stress out about getting it perfect, but do aim for more than 9μH instead of less.

## C9-C12

Value: 100nF each

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

Four parallel 100nF coupling capacitors, totaling 400nF. They pass RF from Q1's drain into L4 while blocking the drain's DC bias. Their RF current and voltage ratings matter. See the [matching-network study](../design-study/impedance-matching-13.56MHz.md#circuit-topology).

## D4

Value: ES1J

[LCSC: C64879](https://www.lcsc.com/product-detail/C64879.html)

Steers positive drain-voltage excursions into TVS1. The series diode also isolates the TVS from part of the RF swing; this pair is a protection path rather than the normal route for output power.

D4 lets TVS1 catch excessive drain-voltage peaks without making Q1 drive the TVS’s full parasitic capacitance every RF cycle. This reduces how much the protection circuit disturbs the amplifier’s tuning. D4 still adds some capacitance of its own, and its switching speed matters.

## TVS1

Value: SM15T150CA

[LCSC: C133715](https://www.lcsc.com/product-detail/C133715.html)

Absorbs drain overvoltage through D4, especially during disturbed or disconnected loading. The [SM15T150CA datasheet](https://www.st.com/en/protections-and-emi-filters/sm15t150ca.html) specifies **150 V typical breakdown**, with a range of **143-158 V at 1 mA**. Its maximum clamping voltage is **207 V at 7.2 A** for a 10/1000μs surge pulse. So the "150 V" describes its breakdown voltage; the voltage it holds while absorbing a surge rises with current.

Its **1500 W rating is for a brief 10/1000μs surge pulse**, not continuous RF power. It can still overheat if asked to absorb excess power continuously. (this is a chonky SMC packaged diode but we still don't want it to dissipate continuously)

For context, the [drain-voltage simulations](../design-study/sim-13mhz/vds_results.md#with-es1j-and-sm15t150ca-drain-clamp) give us an idea of what voltage to expect here. Basically it might peak to 120V when the iron is heating up, and while the simulation also says it might reach over 200V when the tip is hot, or over 500V when the tip is disconnected, we have the [current-transformer phase-detector](../design-study/current-transformer.md) that will lower the input voltage. So realistically, TVS1 is here to catch a state when the phase-detector did not do it's job, or a very short time when it has not reacted yet, which should be a very very short period, not continuous.

## R12

Value: 150Ω 2W

[LCSC: C175287](https://www.lcsc.com/product-detail/C175287.html)

Provides a resistive load from Q1's gate to ground. When the microcontroller stops generating RF, for example after a fault, the energy stored in L8 and Q1's gate capacitance can keep the gate ringing briefly. R12 dissipates that energy and discharges the gate, helping Q1 stop switching promptly. The tradeoff is that it also dissipates power while RF is running. See the [gate-driver study](../design-study/gate-driver-method-comparison.md#what-the-rf-gate-amplifier-changes).

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

Limits large gate-voltage excursions to protect Q1's gate oxide. The SMBJ15CA has a **15 V stand-off rating**, **16.7-18.5 V breakdown at 1 mA**, and a **24.4 V maximum clamp at 24.6 A** for a 10/1000μs surge pulse. The 15 V marking describes its stand-off rating; it doesn't hold the gate at exactly 15 V.

Its capacitance and conduction also load the resonant gate drive, so the actual fitted part can affect tuning.

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

A hand-wound inductor using one [Amidon T130-6](https://www.amidoncorp.com/t130-6/) iron-powder toroid and 16 AWG enamelled wire. It is the first series inductor of the output-matching ladder. Together with the C13-C18 bank and the later stages, it transforms the cartridge load seen by Q1. See the [winding instructions](../build-guide-13mhz/custom-inductors.md#large-inductors) and [matching-network study](../design-study/impedance-matching-13.56MHz.md).

## C13-C18

Value: 100pF 1000V each

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

Six parallel 100pF capacitors make the first 600pF shunt bank after L4. This bank is part of the RF impedance transformation, not supply decoupling.

## C19

Value: DNP

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

An unpopulated tuning position in parallel with C13-C18. Fitting it increases the first shunt-bank capacitance and changes the matching network.

## L5

Value: 400nH

A hand-wound inductor using one [Amidon T130-6](https://www.amidoncorp.com/t130-6/) iron-powder toroid and 16 AWG enamelled wire. It is the second series inductor in the matching ladder. It works with the C20-C26 shunt bank and the other stages to transform the cartridge impedance and shape the output waveform. See the [winding instructions](../build-guide-13mhz/custom-inductors.md#large-inductors).

## C20-C26

C20-C25 Value: 47pF 1000V each

C26 Value: 100pF 1000V

[C20-C25 LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

[C26 LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

The second shunt bank in the matching ladder: six 47pF capacitors (C20-C25) plus one 100pF capacitor (C26), all in parallel, give 382pF. This deliberate mixed-value bank works with L5, L6, and the other stages to transform the cartridge impedance.

## L6

Value: 540nH

A hand-wound inductor using one [Amidon T130-6](https://www.amidoncorp.com/t130-6/) iron-powder toroid and 16 AWG enamelled wire. It is the final series inductor in the matching ladder. It works with C27-C31 before RF current passes through T1 to the handpiece connector. See the [winding instructions](../build-guide-13mhz/custom-inductors.md#large-inductors).

## C27-C31

Value: 47pF 1000V each

[LCSC: C6366545](https://www.lcsc.com/product-detail/C6366545.html)

Five parallel 47pF capacitors make the final 235pF shunt bank after L6. This bank completes the output-matching ladder before the current transformer.

## C32

Value: DNP

An unpopulated tuning position across the final shunt bank. Fitting it adds capacitance after L6 and changes the impedance presented by the output network.

## JP1

[Digi-Key: 0733300030](https://www.digikey.com/en/products/detail/molex/0733300030/2755903)

The F-type RF output connector for the 13.56 MHz handpiece. It's right angled, through hole soldered with huge supporting legs, and a screw designed to be near by to reduce PCB bending when this connector is mechanically loaded.

## T1

A hand-wound current transformer using a [Fair-Rite 5961004901](https://fair-rite.com/product/toroids-5961004901/) material-61 ferrite toroid. It samples RF output current using a one-turn primary and two 14-turn secondaries. With C33 and D5-D8, it forms a phase-sensitive detector that influences the buck supply. Winding polarity matters. See the [winding instructions](../build-guide-13mhz/custom-inductors.md#current-transformer) and [current transformer phase detector study](../design-study/current-transformer.md).

## C33

Value: 10pF 500V

[LCSC: C6629873](https://www.lcsc.com/product-detail/C6629873.html)

Samples RF output voltage capacitively into T1's detector circuit. Its current is phase-shifted relative to output voltage, giving the diode network a reference to compare with output current. See the [phase-detector explanation](../design-study/current-transformer.md).

## D5-D8

Value: 1N4148W

[LCSC: C84367](https://www.lcsc.com/product-detail/C84367.html)

The four diodes in the phase-sensitive switching network around T1. Together they combine the current sample and C33's voltage-derived reference; this is more than an ordinary bridge rectifier. See the [current transformer phase detector explanation](../design-study/current-transformer.md).

## R10

Value: 100Ω

[LCSC: C22775](https://www.lcsc.com/product-detail/C22775.html)

Works with C34 to remove RF from the detector output. Their nominal 100Ω and 1μF values give a corner near 1.6 kHz, leaving a slower control signal for the buck feedback network.

## C34

Value: 1μF

[LCSC: C15849](https://www.lcsc.com/product-detail/C15849.html)

Stores the smoothed phase-detector signal after R10. It removes most of the RF ripple so R11 couples a slowly changing voltage into the buck feedback pin.

## R11

Value: 2.2kΩ

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

Connects the detector output to the buck converter's feedback net (`BUCK-FB`). Together with [R8](#r8), it sets the detector's control gain: how much the buck output changes when the detector voltage changes. A smaller R11 gives the detector more influence; a larger one makes the correction gentler. U1 adjusts the buck output to keep its FB pin near 0.8 V, while the detector voltage can vary over a different range.

## VR4

Value: 5kΩ

[Digi-Key: TC33X-2-502E](https://www.digikey.com/en/products/detail/bourns-inc/TC33X-2-502E/612861)

An optional adjustable path from the detector output to the buck converter's feedback net (`BUCK-FB`) through SJ2. It tunes detector coupling. If R11 remains fitted, the two paths are parallel, so their combined resistance determines the effect.

After testing, it is advised that VR4 should not be populated.

## SJ2

Connects the optional VR4 feedback path. Leaving it open isolates the trimmer; closing it enables that path alongside R11 unless R11 is removed. See the [tuning notes](../build-guide-13mhz/assembly-supplement.md#solder-jumpers-and-tuning-parts).

# RF Amp Gate Driver

This is a small RF amplifier driving the gate of the larger one. U2 drives Q2, and Q2's tuned output drives Q1 through L8. See why the design uses two stages in the [gate-driver comparison](../design-study/gate-driver-method-comparison.md).

The circuit design here was initiated by copying the original SergeyMax design and then evolved to improve upon it.

## U2

Value: 1EDN8511BXUSA1

[LCSC: C3655509](https://www.lcsc.com/product-detail/C3655509.html)

Turns the microcontroller's logic waveform into strong gate-charge and gate-discharge pulses for Q2. Separate source and sink outputs let R16 and R17 set the two switching paths independently.

This part is selected because it is used in the Radio Thermal design and is a better substitute for the MAX17602 in the SergeMax design, better in nearly all aspects. Plus, SergeMax experienced problems with some MAX17602 when operating at 13.56 MHz, when it overheats and did not output the expected waveform at all.

## Q2

Value: IRF510

[Digi-Key: IRF510PBF](https://www.digikey.com/en/products/detail/vishay-siliconix/IRF510PBF/811710)

The small RF amplifier that drives Q1's much larger gate. Its drain circuit and L8 recycle reactive gate energy at 13.56 MHz instead of making U2 drive Q1 directly. See the [driver comparison](../design-study/gate-driver-method-comparison.md).

## R16 and R17

Value: 3.6Ω each

[LCSC: C6301221](https://www.lcsc.com/product-detail/C6301221.html)

The separate turn-on and turn-off gate resistors for Q2. R16 connects U2's sourcing output to charge the gate; R17 connects its sinking output to discharge it. They limit current and damp ringing, with each switching direction controlled independently.

## L7

Value: 9μH

A hand-wound RF choke using a [Fair-Rite 5961004901](https://fair-rite.com/product/toroids-5961004901/) material-61 ferrite toroid and 22 AWG enamelled wire. It feeds 12 V DC into Q2's drain while impeding RF current from returning to the supply. It is the RF choke for the smaller gate-driving amplifier. See the [winding instructions](../build-guide-13mhz/custom-inductors.md#9-uh-choke).

Note that "over-winding" it slightly above 9μH is acceptable, don't stress out about getting it perfect, but do aim for more than 9μH instead of less.

## L8

Value: 180nH

A hand-wound air-core coil in Q1's resonant gate drive, with no ferrite or iron-powder core. It is made from 22 AWG enamelled wire, using a mandrel or a 3D-printed former. Its inductance interacts with Q1's gate capacitance, so changing the coil geometry or turn count changes drive amplitude and losses. See the [construction and tuning instructions](../build-guide-13mhz/custom-inductors.md#coreless-inductor-l8) and [driver study](../design-study/gate-driver-method-comparison.md).

## C37 and C38

Value: 100nF each

[LCSC: C5182461](https://www.lcsc.com/product-detail/C5182461.html)

Two parallel coupling capacitors, totaling 200nF. They block Q2's drain DC voltage while passing RF into L8 and Q1's gate-drive network.

## C39 and C40

Value: 100pF each

[LCSC: C6958299](https://www.lcsc.com/product-detail/C6958299.html)

Two parallel 100pF capacitors from Q2's drain to ground. Their combined 200pF, together with device capacitances and the load, shapes the small amplifier's drain waveform.

## R14

Value: 100Ω

[LCSC: C22775](https://www.lcsc.com/product-detail/C22775.html)

A series resistor on the logic input to U2. It damps fast-edge ringing and limits transient current between the microcontroller and the driver input.

## R15

Value: 4.7kΩ

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Pulls U2's input low when the microcontroller pin is not driving it. This gives the RF driver a defined off command during reset or disconnection.

## C35 and C36

C35 Value: 100nF

C36 Value: 1μF

[C35 LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

[C36 LCSC: C15849](https://www.lcsc.com/product-detail/C15849.html)

Local 12 V decoupling for U2. The 100nF C35 supplies fast current pulses as Q2's gate charges and discharges; the 1μF C36 adds nearby energy storage and reduces supply movement during repeated pulses.

## SJ8

A measurement position in the gate-driver 12 V supply. A temporary shunt here measures driver-branch current while tuning L8; normal builds bridge it. See the [tuning notes](../build-guide-13mhz/assembly-supplement.md#solder-jumpers-and-tuning-parts).

# Tip Detection

A small DC current tests continuity through the handpiece and cartridge while an RF filter keeps the carrier away from Q3. The tip-detection signal net (`TIP-DET`) is high for a connected tip and low for a disconnection. See the [tip-detector study](../design-study/tip-detector.md).

The circuit design here was initiated by copying the original SergeyMax design and then evolved to improve upon it.

## Q3

Value: BC846B

[LCSC: C181138](https://www.lcsc.com/product-detail/C181138.html)

The tip-continuity switch. An open tip lets the bias network drive its base, pulling the tip-detection signal net (`TIP-DET`) low; a connected low-resistance cartridge keeps it off, allowing that signal to go high. See the [tip-detector study](../design-study/tip-detector.md#dc-operation).

## R21

Value: 22Ω

[LCSC: C5204054](https://www.lcsc.com/product-detail/C5204054.html)

Limits current and damps the path from the RF output net (`RF-OUT`) into the continuity detector. With L9 and C42, it keeps RF away from Q3 while allowing the small DC test current through.

## L9

Value: 22μH

[Digi-Key: AIAP-01-220K-T](https://www.digikey.com/en/products/detail/abracon-llc/AIAP-01-220K-T/3059827)

A purchased 22μH choke that passes the DC continuity-test current but impedes RF from the handpiece output. With C42, its ideal LC frequency is about 187 kHz, well below the 13.56 MHz carrier.

## C42

Value: 33nF

[LCSC: C21117](https://www.lcsc.com/product-detail/C21117.html)

Bypasses RF at the detector input to ground while retaining the DC continuity voltage. Together with L9 and R21, it stops the RF carrier from becoming a false tip-detect signal.

## VR2

Value: 1kΩ

[Digi-Key: TC33X-2-102E](https://www.digikey.com/en/products/detail/bourns-inc/TC33X-2-102E/612857)

An optional adjustment for the DC test current, normally bypassed with solder. In the normal bypassed configuration R19 sets that current. Changing this path also changes the resistance at which a tip looks disconnected. See the [tip-detector study](../design-study/tip-detector.md#optional-vr2-test-current-adjustment).

In actual implementation, this potentiometer is essentially useless. The latest PCB design has this footprint bypassed. Do not populate it.

## R19

Value: 470Ω

[LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

Feeds the continuity-test current from 3.3 V into the output through the detector filter. A connected cartridge diverts that current to ground; an open circuit lets the detector voltage rise.

## R20

Value: DNP

An unpopulated resistor position in parallel with R19. Fitting it increases the injected test current and changes detector sensitivity; it is not needed for the standard configuration.

## VR3

Value: 1kΩ

[Digi-Key: TC33X-2-102E](https://www.digikey.com/en/products/detail/bourns-inc/TC33X-2-102E/612857)

An optional adjustment in Q3's base path, normally bypassed. R22 supplies the normal base-current limiting, so this trimmer is not required for ordinary operation.

In actual implementation, this potentiometer is essentially useless. The latest PCB design has this footprint bypassed. Do not populate it.

## R22

Value: 2.2kΩ

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

Limits current into Q3's base when the tip is disconnected. This keeps the transistor drive controlled while still allowing it to pull the tip-detection signal net (`TIP-DET`) firmly low.

## R18

Value: 4.7kΩ

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Pulls the tip-detection signal net (`TIP-DET`) up to 3.3 V when Q3 is off. A high signal means the tip is connected; Q3 pulls it low for a disconnection.

## C41

Value: 3.3nF

[LCSC: C1613](https://www.lcsc.com/product-detail/C1613.html)

Filters short disturbances on the tip-detection signal net (`TIP-DET`) with R18. It adds a small analog settling delay; firmware adds its own qualification before latching a disconnect. See the [detector timing discussion](../design-study/tip-detector.md#firmware-behavior).

# Microcontroller

The microcontroller generates RF timing, supervises the measured supply conditions, supervises the temperature sensors, and runs the user interface.

## U3

Value: STM32F042F6P6

[LCSC: C2969989](https://www.lcsc.com/product-detail/C2969989.html)

A [32-bit Arm Cortex-M0 microcontroller](https://www.st.com/en/microcontrollers-microprocessors/stm32f042f6.html) with **32 KB of flash** for firmware and **6 KB of RAM** for working data. It generates the 13.56 MHz RF drive, adjusts buck attenuation, reads voltage/current/temperature signals, and handles the display, button, and fan.

SergeyMax's original design used an **STM32F030F4P6 with 16 KB of flash**. Our full feature set, including the OLED interface, wouldn't fit in that space, so Hot-Wand upgrades to the STM32F042F6P6 with twice the flash. See the [microcontroller upgrade notes](../design-study/design-study.md#microcontroller).

## Y1

Value: 27.12MHz 10pF

[LCSC: C1986817](https://www.lcsc.com/product-detail/C1986817.html)

The 27.12 MHz crystal reference. The microcontroller derives the 13.56 MHz carrier by dividing this clock by two. A stable clock keeps the RF frequency aligned with the intended cartridge system.

## C55 and C56

Value: 10pF each

[LCSC: C1634](https://www.lcsc.com/product-detail/C1634.html)

The crystal's two load capacitors: C55 is on the oscillator-output side, C56 on the input side. Together with stray PCB and microcontroller capacitance, they affect Y1's startup and frequency.

On SergeyMax's original design, these had to be soldered to the casing of the crystal due his digital ground plane being too close to the RF ground plane. In the Hot-Wand design, this is not an issue, the grounds are much farther apart electrically.

## C53 and C54

Value: 100nF each

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Local 3.3 V bypass capacitors for the microcontroller's supply connections, including its analog supply. They provide fast current near the microcontroller and help keep digital switching noise out of ADC measurements.

## R33

Value: 4.7kΩ

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Pulls the microcontroller's reset pin high for normal operation. The programming header can pull this same node low to reset the microcontroller.

## SJ1

Holds BOOT0 low so the microcontroller starts the application in flash. Normal builds bridge it; changing BOOT0 is only for a deliberate boot-mode change. See the [assembly notes](../build-guide-13mhz/assembly-supplement.md#solder-jumpers-and-tuning-parts).

## JP4

The programming/debug header: exposes 3.3 V reference, ground, the debug clock (`SWCLK`) and data (`SWDIO`) signal nets, and reset. The `SWDIO` net is also shared with the fan-control circuit, which SJ3 can disconnect.

## R52

Value: 100Ω

[LCSC: C22775](https://www.lcsc.com/product-detail/C22775.html)

A series resistor on the RF logic output from the microcontroller, before TP1 and the U2 input path. It reduces fast-edge ringing.

## TP1

A test point on the RF command net (`RF-GEN`) between R52 and R14. It is useful for checking the 13.56 MHz logic waveform.

## U4

The [0.91-inch SSD1306 OLED display module](../build-guide-13mhz/shopping.md#oled-display-module).

## R35 and R36

Value: 4.7kΩ each

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The [I2C](https://en.wikipedia.org/wiki/I2C) pull-ups to 3.3 V: R35 is for the data net (`SDA`), R36 for the clock net (`SCL`). Devices pull these lines low, and the resistors bring them high again when released.

Their 4.7kΩ value suits a short, low-capacitance **3.3 V, 400 kHz (Fast-mode)** bus.

## C57

Value: 10μF

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Local reservoir capacitance for the OLED supply. It reduces voltage dips and noise when the display's internal drive circuitry changes its current draw.

## SW1

[Digi-Key: MJTP1236D](https://www.digikey.com/en/products/detail/apem-inc/MJTP1236D/1798058)

The user button. Pressing it pulls the button signal net (`BTN`) to ground; firmware uses the press for menu actions and acknowledgements. Its mounting connection is grounded too.

## D12

Value: 3.6V

[LCSC: C840638](https://www.lcsc.com/product-detail/C840638.html)

Transient/ESD protection on the exposed button input.

# Secondary Power Buck Converters

These fixed regulators provide 12 V for the gate driver/fan and 3.3 V for the microcontroller, display, and sensors.

I chose to use off-the-shelf pre-packaged buck converters here for convenience and size, they are not the "fun" part of the design so I prioritized size and convenience over cost.

## IC1

Value: 12V R-78K12-2.0

[Digi-Key: R-78K12-2.0](https://www.digikey.com/en/products/detail/recom-power/R-78K12-2-0/18093036)

The fixed 12 V regulator module for the gate driver and fan. Its output reaches the 12 V rail through D10, so the rail can be slightly below the module's nominal output.

Its input undervoltage lockout (UVLO) stops the module when the input falls through **12.7-13.8 V**, and allows it to restart when the input rises through **13.1-14.0 V**, according to [RECOM's specifications](https://recom-power.com/en/products/switching-regulators/switching-regulators-sip/rec-p-R-78K12-2.0.html) at 25°C. This means the gate-driver and fan supply can shut down while IC2 still powers the microcontroller.

## IC2

Value: 3.3V R-78K3.3

[Digi-Key: R-78K3.3-1.0](https://www.digikey.com/en/products/detail/recom-power/R-78K3-3-1-0/18093033)

The fixed 3.3 V regulator for the microcontroller, display, and sensors. A switching module keeps dissipation lower than a linear regulator dropping the full input voltage.

For the fitted R-78K3.3-1.0, [RECOM specifies](https://recom-power.com/en/products/switching-regulators/switching-regulators-sip/rec-p-R-78K3.3-1.0.html) an input undervoltage lockout (UVLO) turn-off threshold of **3.6-3.8 V** as the input falls, and a restart threshold of **3.75-4.05 V** as it rises, at 25°C. Its normal specified input range starts at **4.5 V**. The much lower cutoff than IC1 lets the microcontroller remain powered after the 12 V gate-driver supply shuts down.

## C43

Value: 10μF

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Input decoupling on the input power rail (`DC-IN`) near the secondary regulators. It provides a local reservoir and reduces the effect of supply-trace impedance on the regulator inputs.

## D10

Value: 2A

[LCSC: C698873](https://www.lcsc.com/product-detail/C698873.html)

A series diode from IC1 to the 12 V rail. It prevents the charged 12 V rail from feeding backward into IC1 when its input disappears, at the cost of a forward-voltage drop.

## C44

Value: 10μF

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

The output reservoir on the 12 V rail after D10. It supports changes in gate-driver and fan current.

## D11

[LCSC: C698873](https://www.lcsc.com/product-detail/C698873.html)

A reverse-current bypass from the 3.3 V output back to the input power rail (`DC-IN`). It limits reverse voltage across IC2 if the input falls while the output capacitor is still charged.

## C45

Value: 10μF

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Output reservoir capacitance for the 3.3 V rail. It supports brief changes in logic and display load alongside the smaller local decoupling capacitors.

## D14

Value: 3.6V

[LCSC: C840638](https://www.lcsc.com/product-detail/C840638.html)

A transient suppressor on the 3.3 V rail.

In practice, this one is probably useless. The actual intent is to protect any potentially higher-than-normal voltages that may come from a malfunctioning tip-detector circuit.

## Indicator LEDs and Current-Limiting Resistors (LED1 thru LED4, R38 thru R43)

LED1-LED4

Four optional rail-presence indicators. They show that their respective supply rails have voltage.

| LED  | Purpose               | Current-limiting resistor | Resistor value | Expected drive current |
| ---- | --------------------- | --- | -------- | --- |
| LED1 | Input power (`DC-IN`) | R38 | 22kΩ  | About 1.0 mA at 24 V input |
| LED2 | 12 V rail             | R40 | 4.7kΩ | About 2.1 mA at 12 V |
| LED3 | 3.3 V logic rail      | R41 | 1kΩ   | About 1.3 mA at 3.3 V |
| LED4 | buck converter output | R42 | 22kΩ  | About 0.73-0.82 mA at 18-20 V |

R39 and R43 Value: DNP. These optional resistor positions in LED1's and LED4's paths are bypassed in the current schematic, so they don't affect current while those bypasses remain. R38 and R42 provide the current limiting.

LED4's brightness gives a clue as to what the buck converter voltage is being output at a present moment.

Due to budget cuts, most of these are not populated on the first batch of gift units of Hot-Wand.

# Thermistors

NTC sensors report temperatures, they are supposed to be epoxied to the TO-220 MOSFETs of Hot-Wand. Each sensor forms a divider with a 3.3 V pull-up, followed by an ADC filter.

## THERM1 and THERM2

Value: 10kΩ, β ≈3950K

[Digi-Key: B57891M0103K000](https://www.digikey.com/en/products/detail/tdk/B57891M0103K000/3500546)

The NTC sensor for the two monitored MOSFET hot spots. Its resistance falls as it warms, pulling its ADC voltage lower through the divider with R50. It measures device temperature where attached, not the soldering tip temperature.

## R50

Value: 2.2kΩ

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

The 3.3 V pull-up for THERM1. Together they convert temperature-dependent resistance into an ADC voltage; C59 filters noise on the measurement.

## C59

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

A noise filter from the THERM1 ADC input to ground. It smooths electrical pickup while the thermistor and R50 set the measurement's DC level.

## R51

Value: 2.2kΩ

[LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

The 3.3 V pull-up for THERM2. Together they convert temperature-dependent resistance into an ADC voltage; C60 filters noise on the measurement.

## C60

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

A noise filter from the THERM2 ADC input to ground. It smooths electrical pickup while the thermistor and R51 set the measurement's DC level.

# Fan Control

This section of the circuit has a bunch of parts related to controlling the cooling fan. See the [fan setup instructions](../build-guide-13mhz/assembly-supplement.md#fan-solder-jumper-selection).

## Q9

Value: Si2318

[LCSC: C14498](https://www.lcsc.com/product-detail/C14498.html)

The fan-control MOSFET. Depending on the solder-jumper configuration, it either switches the fan's ground return or supplies an open-drain signal to the fan's PWM input.

## FAN1

- Four-pin PWM fan: [Digi-Key: Molex 0470533000](https://www.digikey.com/en/products/detail/molex/0470533000/3262217).
- Two-pin fan: [Digi-Key: JST B2B-XH-A](https://www.digikey.com/en/products/detail/jst-sales-america-inc/B2B-XH-A/1651045), a 2.5 mm-pitch XH header.

The fan connector, supplying 12 V and the selected ground/PWM connections. SJ4 and SJ5 adapt the circuit to two-wire or PWM fans. See the [fan configuration guide](../build-guide-13mhz/assembly-supplement.md#fan-solder-jumper-selection).

If using a 2 pin fan, fit the two-pin header to FAN1's `V` and `G` pads. It has no separate PWM control pin; two-wire fan control works by switching the fan's ground return.

## R45

Value: 470Ω

[LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

The series gate resistor for Q9. It limits brief charging current and damps ringing between the microcontroller signal and the MOSFET gate.

## R44

Value: 4.7kΩ

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Pulls the fan-control signal low when the microcontroller is not driving it. This gives Q9 a defined command during reset; the fan's actual behavior still depends on the selected jumper mode.

## SJ3

Connects the fan-control circuit to the microcontroller's debug-data net (`SWDIO`). Keep programming access in mind when bridging it. See the [assembly instructions](../build-guide-13mhz/assembly-supplement.md#solder-jumpers-and-tuning-parts).

## SJ4

Selects whether the fan ground is permanently grounded or switched by Q9. Use the documented bridge for the chosen fan mode; do not bridge both alternatives. See the [fan configuration](../build-guide-13mhz/assembly-supplement.md#fan-solder-jumper-selection).

## SJ5

Selects a direct microcontroller signal or Q9's open-drain output for the fan PWM pin. The open-drain path inverts the signal, so firmware polarity must match. See the [fan configuration](../build-guide-13mhz/assembly-supplement.md#fan-solder-jumper-selection).

## C61

Value: 10μF

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Local 12 V reservoir at the fan connector. It reduces rail disturbances from fan commutation and startup.

# Power Sensing

U5 measures DC current entering the RF amplifier, and two resistor dividers measure the RF supply and DC input voltages. This is how the firmware measures an estimate of the power consumption, to display to the user, and to manage power.

## U5

Value: INA138

[LCSC: C47914](https://www.lcsc.com/product-detail/C47914.html)

The high-side current monitor for the RF amplifier's DC supply. It converts the voltage across R1-R3 into an output current, which R25 turns into an ADC voltage. It measures supply current, not RF output power directly. See [INA138 documentation](https://www.ti.com/product/INA138).

## R25

Value: 82kΩ

[LCSC: C23254](https://www.lcsc.com/product-detail/C23254.html)

Sets U5's output gain by turning its output current into a voltage. At 82kΩ, the voltage gain from the shunt is about 16.4, giving roughly 0.547 V per amp with R1-R3.

## C49

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Filters the current-monitor output with R25. It smooths RF pickup and switching ripple before the ADC, while slowing the response to fast current changes.

## C46

Value: 1μF

[LCSC: C15849](https://www.lcsc.com/product-detail/C15849.html)

Local supply bypass for U5 on the positive side of the current shunt. It keeps the monitor's supply quieter without replacing the shunt measurement itself.

## R23 and R24

R23 Value: 22kΩ

R24 Value: 2.2kΩ

[R23 LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

[R24 LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

The RF-supply voltage divider. R23 is the upper 22kΩ resistor and R24 the lower 2.2kΩ resistor to ground, giving 11:1 scaling before the microcontroller's ADC. C47 filters this measurement.

## C47

Value: 100nF

Filters the RF-supply voltage ADC input with R23 and R24. It reduces switching/RF noise so firmware sees a steadier supply-voltage measurement.

## R26 and R27

R26 Value: 22kΩ

R27 Value: 2.2kΩ

[R26 LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

[R27 LCSC: C4190](https://www.lcsc.com/product-detail/C4190.html)

The voltage divider monitoring the input power rail (`DC-IN`) after F1. R26 is the upper 22kΩ resistor and R27 the lower 2.2kΩ resistor to ground. Their 11:1 scaling gives about 3.27 V at the ADC for a 36 V input; C48 filters the signal.

## C48

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Filters the ADC measurement of the input power rail (`DC-IN`). Together with R26 and R27, it reduces noise while still allowing firmware to detect changes in the input supply.

# XT30 DC Input

U6 and its two MOSFETs form the protected battery/DC path. They block reverse current, support input disconnection, and slow startup to limit inrush. See the [LM74810-Q1 overview](https://www.ti.com/product/LM7481-Q1).

## U6

Value: LM74810QDRRRQ1

[LCSC: C3215601](https://www.lcsc.com/product-detail/C3215601.html)

The ideal-diode and disconnect controller for the XT30 input. It drives Q4/Q5 to limit reverse current and provide controlled startup. Q8 disables this input (via the enable pin) when USB power is present. See the [LM74810-Q1 documentation](https://www.ti.com/product/LM7481-Q1), which also contains example circuit diagrams that my design learns from.

## Q4

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

The input-side MOSFET controlled by U6's DGATE pin. It provides the low-loss ideal-diode path from XT30 to the common-drain node and helps block reverse current toward the battery.

[Vishay's ratings](https://www.vishay.com/en/product/76785/) are **60 V drain-source**, **±20 V gate-source**, and **52 A continuous with the case held at 25°C**. Maximum on-resistance is **7.2mΩ at a 10 V gate drive**.

This is deliberately a bit overkill, and that's a personal choice. For a one-off project, I'd rather pay a little extra for the margin and not worry about it.

## Q5

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

The output-side MOSFET controlled by U6's HGATE pin. With Q4, it allows the controller to disconnect the XT30 path; its controlled turn-on also limits capacitor-charging inrush (with the help of R56, C62, C63, also notice the direction the body diode faces).

[Vishay's ratings](https://www.vishay.com/en/product/76785/) are **60 V drain-source**, **±20 V gate-source**, and **52 A continuous with the case held at 25°C**. Maximum on-resistance is **7.2mΩ at a 10 V gate drive**.

This is deliberately a bit overkill, and that's a personal choice. For a one-off project, I'd rather pay a little extra for the margin and not worry about it.

## JP3

The XT30 DC-input connector. Its positive pin enters the protected Q4/Q5 path.

## TVS3

Value: 36V

[LCSC: C134984](https://www.lcsc.com/product-detail/C134984.html)

A transient suppressor across the XT30 input. In practice, this is a bit useless, as I am using USB-C input and there's no reason to ever connect a desktop DC input that is higher than 24V.

## C50

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Local high-frequency bypass across the XT30 input. It helps absorb fast disturbances near the connector and U6's input sensing.

## C51

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

The charge-pump reservoir between U6's `CAP` pin and the common-drain supply node. It supports gate drive above the MOSFET source voltage. See the [controller documentation](https://www.ti.com/product/LM7481-Q1).

## C52

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Decouples U6's common-drain supply node to ground. It provides local current for the controller and reduces supply noise during gate switching.

## R56

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

The series resistor in the slew-control branch on U6's `HGATE` pin. Together with C62 and C63, it shapes turn-on behavior rather than carrying the main load current.

## C62 and C63

Value: 100nF each

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Two 100nF capacitors in the slew-control branch on U6's `HGATE` pin.

They are in series because one capacitor's voltage rating isn't high enough for this position; the pair shares the applied voltage. Using two capacitors with values already used elsewhere in the design saves cost on PCBA, where loading a new component tape reel costs more than the single component itself.

Together they give about 50nF and slow Q5 turn-on to reduce bulk-capacitor inrush. See the [controller's inrush guidance](https://www.ti.com/product/LM7481-Q1).

## R53

Value: 82kΩ

[LCSC: C23254](https://www.lcsc.com/product-detail/C23254.html)

The upper resistor of U6's overvoltage-sense divider. With R54 in parallel with R55, it sets the nominal cutoff near 39.5 V.

## R54 and R55

R54 Value: 3kΩ

R55 Value: 22kΩ

[R54 LCSC: C4211](https://www.lcsc.com/product-detail/C4211.html)

[R55 LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

The two parallel lower resistors in U6's overvoltage divider. R54's 3kΩ in parallel with R55's 22kΩ gives about 2.64kΩ. With upper resistor R53, this sets the XT30 overvoltage cutoff threshold.

Using two resistors with values already used elsewhere in the design saves cost on PCBA, where loading a new component tape reel costs more than the single component itself.

## R34

Value: 22kΩ

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

Pulls U6's enable input up from the common-drain supply. This enables the XT30 path when Q8 is off; USB voltage turns Q8 on and overrides this pull-up.

# USB-PD Input

U7 negotiates USB-C power and controls the USB power path. A separate USB-presence circuit disables the XT30 path. The resistor settings request a profile; the charger and cable still have to support it. See [AP53781 documentation](https://www.diodes.com/part/view/AP53781).

## U7

Value: AP53781

[LCSC: C52730979](https://www.lcsc.com/product-detail/C52730979.html)

The USB-PD controller, configured as a power consumer. It negotiates a voltage/current request from its resistor settings and drives the external input MOSFETs. The requested profile still depends on charger and cable support. See the [AP53781 product page](https://www.diodes.com/part/view/AP53781), which provides examples of how to implement the circuitry around it.

## Q6

Value: DMTH64M2LPDWQ-13

[Digi-Key: DMTH64M2LPDWQ-13](https://www.digikey.com/en/products/detail/diodes-incorporated/DMTH64M2LPDWQ-13/28171648)

Two MOSFETs in one package (selected for convenience), connected back-to-back in the USB power path. U7 drives their common gate to admit negotiated power while allowing the path to block when disabled.

[Diodes' ratings](https://www.diodes.com/part/view/DMTH64M2LPDWQ) are **60 V drain-source**, **±20 V gate-source**, and **90 A continuous per MOSFET with the case held at 25°C**. Maximum on-resistance is **5mΩ per MOSFET at a 10 V gate drive**, or **10mΩ for the conducting pair** under those conditions.

This is deliberately a bit overkill, and that's a personal choice. For a one-off project, I'd rather pay a little extra for the margin and not worry about it.

## Q7

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

An additional controlled isolation MOSFET between the shared DC bus and the USB switching circuit. It helps keep high battery voltage away from the AP53781's lower-voltage circuitry when USB is not supplying power.

[Vishay's ratings](https://www.vishay.com/en/product/76785/) are **60 V drain-source**, **±20 V gate-source**, and **52 A continuous with the case held at 25°C**. Maximum on-resistance is **7.2mΩ at a 10 V gate drive**.

This is deliberately a bit overkill, and that's a personal choice. For a one-off project, I'd rather pay a little extra for the margin and not worry about it.

## JP2

[Digi-Key: UJC-H-G-SMT-2-P6-TR](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/UJC-H-G-SMT-2-P6-TR/24818567)

The charge-only USB-C connector. Its VBUS pins supply power and its CC1/CC2 pins carry USB-C/PD negotiation.

## R31 and R32

Value: 100Ω each

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

Two series resistors between U7's VOUT sense pin and the Q6/Q7 junction, totaling 200Ω. They limit transient current into the sense connection and isolate it from the power-switch junction. They do not add resistance to the main load-current path.

Using two resistors with values already used elsewhere in the design saves cost on PCBA, where loading a new component tape reel costs more than the single component itself.

## R30

Value: 4.3kΩ

[LCSC: C23159](https://www.lcsc.com/product-detail/C23159.html)

The series resistor from U7's VG_A output to the power-MOSFET gates. It limits gate-charging current and affects turn-on speed, including the initial capacitor-charging surge.

## R47

Value: 39kΩ

[LCSC: C23153](https://www.lcsc.com/product-detail/C23153.html)

The default programming resistor for U7's `VSEL` voltage-selection pin. Its 39kΩ path requests the documented 20 V profile; grounding the alternate JP5 path adds a parallel resistance to select a different request.

## R48

Value: 3kΩ

[LCSC: C4211](https://www.lcsc.com/product-detail/C4211.html)

Part of the alternate programming branch for U7's `VSEL` voltage-selection pin, with R49. Grounding that branch through JP5 puts their series resistance in parallel with R47, giving approximately 26kΩ overall for the 28 V request.

## R49

Value: 75kΩ

[LCSC: C23242](https://www.lcsc.com/product-detail/C23242.html)

Completes the alternate programming branch for U7's `VSEL` voltage-selection pin, from R48 to JP5. With R47, this is a resistor-code selection for USB-PD negotiation.

## JP5

The USB-PD selection header. The voltage shunt enables the alternate 28 V resistance code; the current shunt grounds U7's `ISEL` current-selection pin directly for a 5 A request. With the current shunt open and R46/R46B unpopulated, current selection is automatic.

## R46

Value: DNP

An optional hardwired programming branch from U7's `ISEL` current-selection pin, in series with R46B to ground. Their total resistance can set a fixed current request; JP5's direct-to-ground current shunt overrides that branch. With both positions unpopulated and no shunt, that pin uses the automatic setting.

## R46B

Value: DNP

An optional second resistor from the programming path for U7's `ISEL` current-selection pin to ground. It can provide a fixed current-selection resistance.

## Q8

Value: Si2318

[LCSC: C14498](https://www.lcsc.com/product-detail/C14498.html)

Detects voltage on the USB power rail (`USB-VBUS`) through R29/R28 and pulls U6's enable low. This gives USB priority over the XT30 path and helps prevent USB power from feeding a connected battery.

## R28

Value: 4.7kΩ

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The lower resistor of Q8's USB-presence divider and a gate pull-down. It makes Q8 turn off when the USB power rail (`USB-VBUS`) has no voltage, allowing the XT30 controller to enable again.

## R29

Value: 4.7kΩ

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The upper resistor from the USB power rail (`USB-VBUS`) to Q8's gate divider. With R28, it reduces the gate voltage and limits current into the TVS protection path.

## TVS4

Value: 15V

[LCSC: C2987151](https://www.lcsc.com/product-detail/C2987151.html)

Protects Q8's gate from excessive voltage and fast spikes on the USB-presence signal.

## TVS5

Value: 36V

[LCSC: C134984](https://www.lcsc.com/product-detail/C134984.html)

A transient suppressor across the USB power rail (`USB-VBUS`). In practice, this is useless in the same way that TVS3 is useless.

## C64

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Decouples U7's internal 5 V output pin (`V5V`). This is a local controller-support capacitor, not the main USB input reservoir. See the [AP53781 documentation](https://www.diodes.com/part/view/AP53781).
