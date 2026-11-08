# RF Class-E Amp and Matching

Q1 switches at 470 kHz, with the input supply feeding the RF stage directly. The LC network shapes the switching waveform and transforms the cartridge load. There is no main adjustable buck converter or transformer phase-feedback loop here. See the [Lite matching study](../design-study/impedance-matching-470kHz.md).

The transistor rapidly switches current through an inductor, storing energy in its magnetic field. When it switches off, the inductor keeps pushing current, charging the surrounding capacitance and raising the voltage. The inductors and capacitors shape the waveform so the voltage falls close to zero before the transistor switches on again, reducing wasted heat. See the [introduction to class-E amplifiers](https://www.allaboutcircuits.com/technical-articles/introduction-to-the-class-e-power-amplifier/).

This part of the circuit started by copying the original Radio Thermal design, then evolved as I adapted it for Hot-Wand Lite. See the [design notes](../design-study/design-study.md) for the original projects that inspired both versions.

## Q1

[Digi-Key: IXFP26N30X3](https://www.digikey.com/en/products/detail/ixys/IXFP26N30X3/8032441)

The main 470 kHz RF power switch. Its drain network shapes voltage and current for efficient class-E operation. Use one of the documented [MOSFET choices](../build-guide-lite-470khz/shopping.md#main-470-khz-rf-mosfet) but IXFP26N30X3 is preferred.

## L1

Value: 33μH

[LCSC: C2453886](https://www.lcsc.com/product-detail/C2453886.html) | [Digi-Key: PA4343.333NLT](https://www.digikey.com/en/products/detail/pulse-electronics/PA4343-333NLT/5641830)

A purchased 33μH RF choke feeding DC into Q1's drain. It resists the carrier-frequency current so the matching network (rather than the input supply) receives most of the RF. Its saturation current and loss matter as well as its inductance.

On the original Radio Thermal design, this is supposed to be a [SRP1265A-330M](https://www.digikey.com/en/products/detail/bourns-inc/SRP1265A-330M/4876626), which is pretty much the same thing but it was out-of-stock when I built Hot-Wand Lite.

## C1

Value: 15nF

[LCSC: C3858208](https://www.lcsc.com/product-detail/C3858208.html)

The essential shunt capacitor directly across Q1's drain and ground. It shapes the class-E switching waveform together with Q1's output capacitance. A missing or intermittent connection can cause severe switching stress. See the [drain-network explanation](../design-study/impedance-matching-470kHz.md#circuit-topology).

## C2-C5

C2-C4 Value: 22nF each

C5 Value: 15nF

[C2-C4 LCSC: C3862155](https://www.lcsc.com/product-detail/C3862155.html)

[C5 LCSC: C3858208](https://www.lcsc.com/product-detail/C3858208.html)

The parallel coupling bank between Q1's drain and L2: C2-C4 are 22nF each and C5 adds 15nF, giving 81nF total. The bank blocks drain DC while passing RF and contributes to the input-stage tuning.

## L2

Value: 6.2μH

A hand-wound inductor using one Magnetics Kool Mu 0077932A7 powder toroid and 22 AWG enamelled wire. It is the first series matching inductor. With the C6-C8 shunt bank, it transforms the load presented by the following impedance inverter. This pair is part of the match, not just an RF filter. See the [winding instructions](../build-guide-lite-470khz/custom-inductors.md) and [network study](../design-study/impedance-matching-470kHz.md#circuit-topology).

## C6-C8

Value: 10nF each

[LCSC: C2261646](https://www.lcsc.com/product-detail/C2261646.html)

Three parallel 10nF capacitors give 30nF from the node after L2 to ground. They form the input transformation stage with L2.

## L3

Value: 16.9μH

A hand-wound inductor using one Magnetics Kool Mu 0077932A7 powder toroid and 22 AWG enamelled wire. It is the first series inductor of the L3-C9-L4 impedance inverter. At 470 kHz its reactance is about 50Ω, matching the magnitudes of the other two elements. See the [winding instructions](../build-guide-lite-470khz/custom-inductors.md) and [inverter explanation](../design-study/impedance-matching-470kHz.md#the-l3-c9-l4-immittance-converter).

## C9

Value: 6.8nF

[LCSC: C2182121](https://www.lcsc.com/product-detail/C2182121.html)

The shunt capacitor between L3 and L4. Together these three parts form an impedance inverter near 470 kHz; C9 is not merely a supply-noise bypass. See the [inverter explanation](../design-study/impedance-matching-470kHz.md#the-l3-c9-l4-immittance-converter).

## L4

Value: 16.9μH

A hand-wound inductor using one Magnetics Kool Mu 0077932A7 powder toroid and 22 AWG enamelled wire. It is the second series inductor of the L3-C9-L4 impedance inverter. Its value should stay coordinated with L3 and C9; changing it also changes the load seen by Q1. See the [winding instructions](../build-guide-lite-470khz/custom-inductors.md).

## L5

Value: 11.5μH

A hand-wound inductor using one Magnetics Kool Mu 0077932A7 powder toroid and 22 AWG enamelled wire. It is the shunt inductor at the output end of the matching network. With the series C10/C11 bank, it transforms the cartridge-and-cable impedance before that load reaches the inverter. See the [winding instructions](../build-guide-lite-470khz/custom-inductors.md).

## C10 and C11

C10 Value: 6.8nF

C11 Value: 1nF

[C10 LCSC: C2182121](https://www.lcsc.com/product-detail/C2182121.html)

[C11 LCSC: C3859218](https://www.lcsc.com/product-detail/C3859218.html)

The final series matching bank between the last inductor node and the handpiece output. C10 is 6.8nF and C11 adds 1nF in parallel, giving 7.8nF total. Both are deliberate parts of the RF match.

## JP1

The SMA RF output connector for the 470 kHz handpiece and cable.

## D3

Value: ES1J

[LCSC: C64879](https://www.lcsc.com/product-detail/C64879.html)

Steers positive drain-voltage excursions into D4. It makes the TVS a protection branch while limiting its direct interaction with the normal RF waveform.

D3 lets D4 catch excessive drain-voltage peaks without making Q1 drive the TVS's full parasitic capacitance every RF cycle. This reduces how much the protection circuit disturbs the amplifier's tuning. D3 still adds some capacitance of its own, and its switching speed matters.

## D4

Value: SMCJ200CA

[LCSC: C2760132](https://www.lcsc.com/product-detail/C2760132.html)

The drain transient suppressor behind D3. The SMCJ200CA has a **200 V stand-off rating**, **224-247 V breakdown at 1 mA**, and a **324 V maximum clamp at 4.6 A** for a 10/1000μs surge pulse. That clamp rating exceeds the 300 V drain rating of some documented Q1 choices, so this part alone doesn't guarantee their protection.

Its **1500 W rating is for a brief surge**, not continuous RF power. This is a chonky SMC-packaged diode, but we still don't want it dissipating continuously.

## SJ1

The connection from Q1's source to ground. Normally bridge it for a low-impedance return; a small temporary shunt can be used here to measure source current during testing.

## SJ2-SJ5

Four parallel connection positions between the SMA shell/RF return and circuit ground. Normal builds bridge them. If used for current sensing, the other parallel paths must not bypass the measurement shunt.

# RF Amp Gate Driver

U2 drives Q1 directly through separate turn-on and turn-off resistors. This is simpler than the resonant two-amplifier driver in the 13.56 MHz version. See the [gate-driver comparison](../design-study/gate-driver-method-comparison.md).

The gate-driver circuit also started from the original Radio Thermal design and was adapted for Hot-Wand Lite.

## U2

Value: 1EDN8511BXUSA1

[LCSC: C3655509](https://www.lcsc.com/product-detail/C3655509.html)

The gate-driver IC, translating the microcontroller's logic waveform into strong charging and discharging pulses for Q1.

This is the same gate-driver part used in the Radio Thermal design that the Lite version is based on.

## R3 and R4

Value: 3.6Ω each

[LCSC: C445783](https://www.lcsc.com/product-detail/C445783.html)

The separate gate resistors for Q1: R3 connects U2's sourcing output for turn-on, while R4 connects its sinking output for turn-off. They limit charging/discharging current, damp ringing, and dissipate some gate-drive energy. The two paths can be adjusted independently.

The value is chosen for both availability and suitability.

## R1

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

A series resistor on the logic input to U2. It damps fast-edge ringing and limits transient current between the microcontroller and the driver input.

## R2

Value: 4.7kΩ

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Pulls U2's logic input low when the microcontroller output is undriven. This gives the RF switch a defined off command during reset or module removal.

## C18 and C19

C18 Value: 100nF

C19 Value: 1μF

[C18 LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

[C19 LCSC: C15849](https://www.lcsc.com/product-detail/C15849.html)

Local 12 V decoupling for U2. The 100nF C18 supplies fast gate-charge current pulses; the 1μF C19 adds nearby energy storage and reduces movement of the driver's supply during repeated pulses.

# Microcontroller

The removable module produces the carrier and controls average power using RF bursts. Mode inputs, a voltage divider, and a logic power switch guide firmware behavior. See the [burst-power study](../design-study/lite-power-attenuation.md).

## U1

The removable microcontroller module. It generates the 470 kHz carrier and burst-power patterns, reads input voltage and selection switches, and drives status LEDs and the fan. The symbol names a SAMD21 XIAO; other supported modules need matching firmware/pin mappings. See the [build guide](../build-guide-lite-470khz/build-guide-lite.md#3-microcontroller-and-firmware-bring-up).

## R26

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

A series resistor at the microcontroller's RF output, ahead of R1 and the U2 input. It reduces ringing on the logic connection.

## D2

Value: 1A 40V

[LCSC: C8598](https://www.lcsc.com/product-detail/C8598.html)

Feeds the microcontroller module's 5 V input from IC2 while blocking reverse current from the module into the board's 5 V rail.

## JP6

The output-power selection header. Grounding the mode-selection net `SEL2` selects Eco; grounding the `SEL3` net selects Sport, while neither grounded selects Adaptive in the current firmware. These inputs select firmware behavior, not the USB-PD request. See [power management](../../firmware-lite/src/power.cpp).

## R23 and R24

Value: 470Ω each

[LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

Series resistors between JP6 and the microcontroller's mode inputs: R23 serves the `SEL2` mode-selection net and R24 serves the `SEL3` net. They limit current from the exposed jumper connections; the microcontroller's input configuration supplies the pull-ups.

## JP4

An external connection for the RF on/off control net (`PWR-DIS`) and ground. It parallels the slide-switch control, allowing a remote contact to command RF on/off without switching the main supply current.

## SW1

[Digi-Key: SLW-864574-5A-RA-N-D](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/SLW-864574-5A-RA-N-D/24399231)

This is the power switch.

The slide switch grounds the RF on/off control net (`PWR-DIS`) in its selected position. Firmware treats the grounded input as the run request. This is a logic control, so the board remains powered when RF is off.

## R25

Value: 470Ω

[LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

The series resistor between the exposed power-switch/header connection and the microcontroller's RF on/off control net (`PWR-DIS`). It limits transient current.

## TVS4-TVS6

[LCSC: C840638](https://www.lcsc.com/product-detail/C840638.html)

ESD protection on the exposed control inputs: TVS4 and TVS5 protect the mode-selection nets `SEL2` and `SEL3`, respectively; TVS6 protects the RF on/off control net `PWR-DIS`.

Their respective series resistors R23, R24, and R25 add current limiting. These protect the logic signals rather than the high-current supply.

## R21 and R22

R21 Value: 100kΩ

R22 Value: 4.7kΩ

[R21 LCSC: C25803](https://www.lcsc.com/product-detail/C25803.html)

[R22 LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The fused input-voltage divider. R21 is the upper 100kΩ resistor; R22 is the lower 4.7kΩ resistor to ground. Their roughly 22.3:1 scaling gives about 0.90 V on the microcontroller's voltage-sense net (`ADC`) for a 20 V input. Firmware uses this for voltage supervision and power-mode behavior; C23 filters the signal.

The ratio was originally selected so that ESP32 microcontrollers can be used, before I realized all the other problems that ESP32 brings in.

## C23

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Filters the microcontroller's input-voltage sensing net (`ADC`) to ground. It smooths switching noise while R21 and R22 determine the DC scaling.

## LED1 and R5

R5 Value: 470Ω

[LED1 LCSC: C284935](https://www.lcsc.com/product-detail/C284935.html)

[R5 LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

The main firmware status LED. Blink patterns report operating state, input-voltage range, and power selection; see the [Lite operating guide](../operating-instructions-for-lite.md#led-indicators). R5 limits current to about 2.8 mA while lit, assuming a 3.3 V output and a 2 V LED forward voltage. Blinking reduces the average current.

## LED2 and R6

R6 Value: 470Ω

[R6 LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

An optional second status LED, driven by the same microcontroller output as LED1, so it mirrors LED1's indication. Its own resistor R6 limits current independently to about 2.8 mA while lit, assuming a 3.3 V output and a 2 V LED forward voltage.

# Secondary Power

IC1 produces 12 V for the gate driver and fan; IC2 drops that rail to 5 V for the microcontroller module. The fan switching circuit and its routing jumpers are in this schematic section too.

I chose the off-the-shelf buck module at IC1 for convenience and size. This isn't the "fun" part of the design, so I was happy to spend a bit more on it.

## IC1

Value: 12V R-78K12-2.0

[Digi-Key: R-78K12-2.0](https://www.digikey.com/en/products/detail/recom-power/R-78K12-2-0/18093036)

The fixed 12 V switching-regulator module. It powers the gate driver and fan, and also supplies the 5 V linear regulator through the 12 V rail.

Its input undervoltage lockout (UVLO) stops the module when the input falls through **12.7-13.8 V**, and allows it to restart when the input rises through **13.1-14.0 V**, according to [RECOM's specifications](https://recom-power.com/en/products/switching-regulators/switching-regulators-sip/rec-p-R-78K12-2.0.html) at 25°C. The microcontroller's 5 V supply also depends on this 12 V rail, so it loses its power source when IC1 shuts down.

## IC2

Value: AMS1117-5.0

[LCSC: C6187](https://www.lcsc.com/product-detail/C6187.html)

The 5 V linear regulator feeding the microcontroller module (through D2). It drops the 12 V rail to 5 V. The microcontroller module generates its own 3.3V logic supply using the 5V.

## C14

Value: 10μF

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Local input decoupling for IC1 on the fused main supply. It reduces supply-trace noise reaching the secondary regulator.

## D1

Value: 2A

[LCSC: C698873](https://www.lcsc.com/product-detail/C698873.html)

A series diode from IC1 to the 12 V rail. It blocks the charged output rail from feeding backward into IC1 when its input collapses, with a small forward-voltage cost.

## C15

Value: 10μF

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

The reservoir on the 12 V rail after D1. It supports the gate driver, fan, and 5 V regulator during changes in load.

## C16

Value: 10μF

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

The output capacitor on IC2's 5 V rail. It supports microcontroller load changes and is part of the linear regulator's stability requirements; capacitor type and ESR should suit the actual fitted regulator.

## Q7

Value: Si2318

[LCSC: C14498](https://www.lcsc.com/product-detail/C14498.html)

The fan-control MOSFET. The solder jumpers can route it to switch the fan ground or provide an open-drain PWM signal, depending on the fan type.

## FAN1

- Four-pin PWM fan: [Digi-Key: Molex 0470533000](https://www.digikey.com/en/products/detail/molex/0470533000/3262217).
- Two-pin fan: [Digi-Key: JST B2B-XH-A](https://www.digikey.com/en/products/detail/jst-sales-america-inc/B2B-XH-A/1651045), a 2.5 mm-pitch XH header.

The 12 V fan connector. SJ6 chooses the PWM source and SJ7 chooses the ground connection; a four-wire fan's speed input and a two-wire fan's switched supply require different routing.

If using a 2 pin fan, fit the two-pin header to FAN1's `V` and `G` pads. It has no separate PWM control pin; select the switched ground return through SJ7 for two-wire fan control.

## R27

Value: 470Ω

[LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

The series resistor between the microcontroller fan signal and Q7's gate. It limits brief gate-charging current and damps fast-edge ringing.

## R28

Value: 4.7kΩ

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Pulls the fan-control signal low when the microcontroller is not driving it. This gives Q7 a defined command during reset; jumper routing determines the resulting fan behavior.

## SJ6

Selects the fan PWM input between the direct microcontroller signal and Q7's open-drain output. The MOSFET path inverts the signal, so the firmware output mode must match the selected connection.

## SJ7

Selects whether the fan ground is permanently grounded or switched by Q7. Choose the connection for the intended fan mode; bridging both alternatives defeats switching.

## C17

Value: 10μF

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Local 12 V reservoir beside the fan connector. It reduces disturbances from fan startup and commutation on the shared driver supply.

# XT30 DC Input

U3 and Q2/Q3 protect the XT30 power path and slow startup. F1 and the large input capacitors serve both input sources after they join. See the [ideal-diode controller overview](https://www.ti.com/product/LM7481-Q1).

## U3

Value: LM74810QDRRRQ1

[LCSC: C3215601](https://www.lcsc.com/product-detail/C3215601.html)

The ideal-diode/disconnect controller on the XT30 path. It drives Q2 and Q3 for low-loss forward power, reverse-current blocking, and controlled startup. Q6 disables this input via the enable pin when USB power is present. See the [LM74810-Q1 documentation](https://www.ti.com/product/LM7481-Q1), which also contains example circuits that guided this design.

## Q2

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

The input-side MOSFET controlled by U3's DGATE output. It implements the low-loss ideal-diode path from XT30 and helps prevent reverse current toward the battery.

[Vishay's ratings](https://www.vishay.com/en/product/76785/) are **60 V drain-source**, **±20 V gate-source**, and **52 A continuous with the case held at 25°C**. Maximum on-resistance is **7.2mΩ at a 10 V gate drive**.

This is deliberately a bit overkill, and that's a personal choice. For a one-off project, I'd rather pay a little extra for the margin and not worry about it.

## Q3

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

The output-side MOSFET controlled by U3's HGATE output. It allows the XT30 input to be disconnected and turns on gradually to limit bulk-capacitor charging current, with the help of R29, C25, and C26. Notice the body-diode direction: it blocks input-to-output charging while Q3 is off, allowing the gate ramp to control inrush.

[Vishay's ratings](https://www.vishay.com/en/product/76785/) are **60 V drain-source**, **±20 V gate-source**, and **52 A continuous with the case held at 25°C**. Maximum on-resistance is **7.2mΩ at a 10 V gate drive**.

This is deliberately a bit overkill, and that's a personal choice. For a one-off project, I'd rather pay a little extra for the margin and not worry about it.

## JP3

The XT30 battery/DC connector.

## TVS1

Value: 36V

[LCSC: C134984](https://www.lcsc.com/product-detail/C134984.html)

A transient suppressor across the XT30 input. In practice, I don't expect it to do much in my USB-C-powered builds, where the XT30 path is unused. I don't intend to connect a desktop DC supply above 24 V either.

## C20

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Local high-frequency bypass across the XT30 input. It reduces fast connector/supply disturbances near U3's sensing and power input.

## C21

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

The charge-pump reservoir between U3's `CAP` pin and the common-drain node. It supports gate voltages above the MOSFET source voltage so the input switches can turn fully on.

## C24

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Decouples U3's supply at the MOSFET common-drain node. It supports local controller current and reduces gate-switching disturbances on that node.

## R29

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

The series resistor in the slew-control branch on U3's `HGATE` pin. With C25 and C26, it shapes Q3's startup ramp without carrying the main load current.

## C25 and C26

Value: 100nF each

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Two 100nF capacitors in the slew-control branch on U3's `HGATE` pin.

They are in series because one capacitor's voltage rating isn't high enough for this position; the pair shares the applied voltage. Using two capacitors with values already used elsewhere in the design saves cost on PCBA, where loading a new component tape reel costs more than the single component itself.

Together they give about 50nF and slow Q3 turn-on while the bulk capacitors charge. See the [inrush-control documentation](https://www.ti.com/product/LM7481-Q1).

## R30

Value: 51kΩ

[LCSC: C23196](https://www.lcsc.com/product-detail/C23196.html)

The upper resistor of U3's overvoltage divider. With the fitted 2kΩ R31, its 51kΩ sets a nominal cutoff around 32.6 V.

## R31

Value: 2kΩ

[LCSC: C22975](https://www.lcsc.com/product-detail/C22975.html)

The lower resistor of the XT30 overvoltage divider. It returns U3's OV pin to ground and works with R30 to set the cutoff threshold.

## R32

Value: DNP

An optional resistor position in parallel with R31. Fitting it changes the overvoltage divider and cutoff; the default DNP position leaves R31 as the lower arm.

## R12

Value: 22kΩ

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

Pulls U3's enable input up from the common-drain supply node. USB voltage turns Q6 on to override this pull-up and disable the battery/DC path.

## F1

Value: 7A 20×5mm

[Digi-Key: 3518P fuse clip](https://www.digikey.com/en/products/detail/keystone-electronics/3518P/316011)

The main 7 A input fuse shared by the XT30 and USB paths. It will blow when something goes wrong (tm). "Something" is a really large problem space here; a power component failing short is one example. The supplier link is for a fuse clip, not the fuse itself.

## C12 and C13

Value: 470μF each

[Digi-Key: UHW1H471MPD](https://www.digikey.com/en/products/detail/nichicon/UHW1H471MPD/3664388)

Two parallel 470μF bulk input capacitors, totaling 940μF when both are fitted. They supply brief RF current bursts, absorb some returned energy, and reduce demand spikes at the source. This large capacitance also makes controlled input charging important. See the [burst-power study](../design-study/lite-power-attenuation.md) and [assembly notes](../build-guide-lite-470khz/assembly-supplement.md#skipping-components-and-substitutions) for the C13 omission option in Sport-only builds.

# USB-PD Input

U4 negotiates USB-C power from resistor-coded voltage/current requests. USB presence disables the XT30 input through Q6. The requested profile still depends on charger and cable support. See the [AP53781 overview](https://www.diodes.com/part/view/AP53781).

## U4

Value: AP53781

[LCSC: C52730979](https://www.lcsc.com/product-detail/C52730979.html)

The USB-PD controller, used as a power consumer. Resistance codes on its `VSEL` voltage-selection pin and `ISEL` current-selection pin request voltage and current, and its gate driver controls the USB input MOSFETs. A request still depends on source/cable support. See the [AP53781 documentation](https://www.diodes.com/part/view/AP53781), which also gives examples of the circuitry around it.

## Q4

Value: DMTH64M2LPDWQ-13

[Digi-Key: DMTH64M2LPDWQ-13](https://www.digikey.com/en/products/detail/diodes-incorporated/DMTH64M2LPDWQ-13/28171648)

Two MOSFETs in one package (selected for convenience), connected back-to-back in the USB power path. U4 drives their common gate to connect negotiated USB power while allowing the input path to block when disabled.

[Diodes' ratings](https://www.diodes.com/part/view/DMTH64M2LPDWQ) are **60 V drain-source**, **±20 V gate-source**, and **90 A continuous per MOSFET with the case held at 25°C**. Maximum on-resistance is **5mΩ per MOSFET at a 10 V gate drive**, or **10mΩ for the conducting pair** under those conditions.

This is deliberately a bit overkill, and that's a personal choice. For a one-off project, I'd rather pay a little extra for the margin and not worry about it.

## Q5

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

An additional isolation MOSFET between the common DC bus and the USB switching circuit. It helps keep high battery voltage away from the AP53781's lower-voltage pins.

[Vishay's ratings](https://www.vishay.com/en/product/76785/) are **60 V drain-source**, **±20 V gate-source**, and **52 A continuous with the case held at 25°C**. Maximum on-resistance is **7.2mΩ at a 10 V gate drive**.

This is deliberately a bit overkill, and that's a personal choice. For a one-off project, I'd rather pay a little extra for the margin and not worry about it.

## JP2

[Digi-Key: UJC-H-G-SMT-2-P6-TR](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/UJC-H-G-SMT-2-P6-TR/24818567)

The charge-only USB-C connector. Its VBUS pins carry power and its CC1/CC2 pins carry USB-C/PD negotiation.

## R10 and R11

Value: 100Ω each

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

Two series resistors between U4's VOUT sense pin and the Q4/Q5 junction, totaling 200Ω. They limit transient current into the sense connection and isolate it from disturbances at the power-switch junction, without adding resistance to the main supply path.

Using two resistors with values already used elsewhere in the design saves cost on PCBA, where loading a new component tape reel costs more than the single component itself.

## R9

Value: 4.3kΩ

[LCSC: C23159](https://www.lcsc.com/product-detail/C23159.html)

The series resistor between U4's VG_A output and the power-MOSFET gates. It limits charging current and affects turn-on speed, including startup inrush.

## USB-PD Settings (JP5, R13-R20, SJ8, and SJ9)

- R13 Value: 24kΩ ([LCSC: C23352](https://www.lcsc.com/product-detail/C23352.html)); R14 Value: 2kΩ ([LCSC: C22975](https://www.lcsc.com/product-detail/C22975.html))
- R15 Value: 51kΩ ([LCSC: C23196](https://www.lcsc.com/product-detail/C23196.html)); R16 Value: 1kΩ ([LCSC: C21190](https://www.lcsc.com/product-detail/C21190.html))
- R17 Value: 100kΩ ([LCSC: C25803](https://www.lcsc.com/product-detail/C25803.html)); R18 Value: 56kΩ ([LCSC: C23206](https://www.lcsc.com/product-detail/C23206.html))
- R19 Value: 51kΩ ([LCSC: C23196](https://www.lcsc.com/product-detail/C23196.html)); R20 Value: 1kΩ ([LCSC: C21190](https://www.lcsc.com/product-detail/C21190.html)); **both DNP**

U4's `VSEL` pin reads a resistance to ground to choose the USB-PD voltage request; `ISEL` selects the current request. The charger and cable must support the selected profile. See the [AP53781 selection settings](https://www.diodes.com/part/view/AP53781) for the full resistor-code table. These hardware settings are separate from JP6's firmware power modes.

- **15 V default:** R15 + R16 = 52kΩ is already connected to ground.
- **20 V:** R17 + R18 = 156kΩ; grounding the `USB-V-SEL-20V` net through JP5 or SJ8 gives **52kΩ ∥ 156kΩ = 39kΩ**.
- **28 V option:** R19 + R20 adds another 52kΩ branch through JP5, giving **52kΩ ∥ 52kΩ = 26kΩ**, with the 20 V branch disabled. Both resistors are unpopulated in the current BOM, so moving the shunt alone cannot select 28 V. Reusing the 51kΩ and 1kΩ values avoids loading another resistor value for PCBA.
- **Current:** R13 + R14 = 26kΩ requests **3 A by default**. JP5 or SJ9 can ground the `USB-I-SEL` net directly to request **5 A**. An open `ISEL` pin would select Auto, but these fitted resistors provide the normal 3 A setting.

On JP5, **1–2 selects 5 A**; **2–3 is ground-to-ground storage** for the current shunt, preserving 3 A. The voltage shunt selects **20 V on 5–6** or the optional **28 V on 4–5**. Unlike the full design, there is no ground-to-ground voltage storage position: with SJ8 open, removing that shunt leaves 15 V selected.

## Q6

Value: Si2318

[LCSC: C14498](https://www.lcsc.com/product-detail/C14498.html)

The USB-presence switch. The USB power rail (`USB-VBUS`) drives its gate through R8/R7, and its drain pulls U3's enable low so the XT30 path is disabled while USB is present.

## R7

Value: 4.7kΩ

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The lower resistor of Q6's USB-presence divider and a gate pull-down. It returns Q6's gate toward ground when USB power is absent.

## R8

Value: 4.7kΩ

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The upper resistor from the USB power rail (`USB-VBUS`) to Q6's gate divider. It scales the signal with R7 and limits current into the gate-protection TVS.

## TVS3

Value: 15V

[LCSC: C2987151](https://www.lcsc.com/product-detail/C2987151.html)

Protects Q6's gate from excessive voltage and fast spikes on the USB-presence signal.

It protects the USB-presence MOSFET rather than clamping the full VBUS rail.

## TVS2

Value: 36V

[LCSC: C134984](https://www.lcsc.com/product-detail/C134984.html)

A transient suppressor across the USB power rail (`USB-VBUS`). In practice, I don't expect much work from this one with the intended USB-PD profiles, much like TVS1 on the XT30 input.

## C22

Value: 100nF

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Local decoupling for U4's internal 5 V output pin (`V5V`). It supports the PD controller's own circuitry, rather than storing the main RF burst energy.
