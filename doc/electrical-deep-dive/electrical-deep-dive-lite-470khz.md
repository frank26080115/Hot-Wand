# RF Class-E Amp and Matching

Q1 switches at 470 kHz, with the input supply feeding the RF stage directly. The LC network shapes the switching waveform and transforms the cartridge load. There is no main adjustable buck converter or transformer phase-feedback loop here. See the [Lite matching study](../design-study/impedance-matching-470kHz.md).

## Q1

Value: NCHAN

[Digi-Key: IXFP26N30X3, one documented option](https://www.digikey.com/en/products/detail/ixys/IXFP26N30X3/8032441)

The main 470 kHz RF power switch. Its drain network shapes voltage and current for efficient class-E operation. The schematic leaves the model as NCHAN; use one of the documented [MOSFET choices](../build-guide-lite-470khz/shopping.md#main-470-khz-rf-mosfet), with suitable gate charge and voltage margin.

## L1

Value: 33u

[LCSC: C2453886](https://www.lcsc.com/product-detail/C2453886.html) | [Digi-Key: PA4343.333NLT](https://www.digikey.com/en/products/detail/pulse-electronics/PA4343-333NLT/5641830)

A purchased 33 uH RF choke feeding DC into Q1's drain. It resists the carrier-frequency current so the matching network, rather than the input supply, receives most of the RF. Its saturation current and loss matter as well as its inductance.

## C1

Value: 15n

[LCSC: C3858208](https://www.lcsc.com/product-detail/C3858208.html)

The essential shunt capacitor directly across Q1's drain and ground. It shapes the class-E switching waveform together with Q1's output capacitance. A missing or intermittent connection can cause severe switching stress. See the [drain-network explanation](../design-study/impedance-matching-470kHz.md#circuit-topology).

## C2-C5

C2-C4 Value: 22n each

C5 Value: 15n

[C2-C4 LCSC: C3862155](https://www.lcsc.com/product-detail/C3862155.html)

[C5 LCSC: C3858208](https://www.lcsc.com/product-detail/C3858208.html)

The parallel coupling bank between Q1's drain and L2: C2-C4 are 22 nF each and C5 adds 15 nF, giving 81 nF total. The bank blocks drain DC while passing RF and contributes to the input-stage tuning.

## L2

Value: 6u2

A hand-wound inductor using one Magnetics Kool Mu 0077932A7 powder toroid and 22 AWG enamelled wire. It is the first series matching inductor. With the C6-C8 shunt bank, it transforms the load presented by the following impedance inverter. This pair is part of the match, not just an RF filter. See the [winding instructions](../build-guide-lite-470khz/custom-inductors.md) and [network study](../design-study/impedance-matching-470kHz.md#circuit-topology).

## C6-C8

Value: 10n each

[LCSC: C2261646](https://www.lcsc.com/product-detail/C2261646.html)

Three parallel 10 nF capacitors give 30 nF from the node after L2 to ground. They form the input transformation stage with L2.

## L3

Value: 16u9

A hand-wound inductor using one Magnetics Kool Mu 0077932A7 powder toroid and 22 AWG enamelled wire. It is the first series inductor of the L3-C9-L4 impedance inverter. At 470 kHz its reactance is about 50 ohms, matching the magnitudes of the other two elements. See the [winding instructions](../build-guide-lite-470khz/custom-inductors.md) and [inverter explanation](../design-study/impedance-matching-470kHz.md#the-l3-c9-l4-immittance-converter).

## C9

Value: 6n8

[LCSC: C2182121](https://www.lcsc.com/product-detail/C2182121.html)

The shunt capacitor between L3 and L4. Together these three parts form an impedance inverter near 470 kHz; C9 is not merely a supply-noise bypass. See the [inverter explanation](../design-study/impedance-matching-470kHz.md#the-l3-c9-l4-immittance-converter).

## L4

Value: 16u9

A hand-wound inductor using one Magnetics Kool Mu 0077932A7 powder toroid and 22 AWG enamelled wire. It is the second series inductor of the L3-C9-L4 impedance inverter. Its value should stay coordinated with L3 and C9; changing it also changes the load seen by Q1. See the [winding instructions](../build-guide-lite-470khz/custom-inductors.md).

## L5

Value: 11u5

A hand-wound inductor using one Magnetics Kool Mu 0077932A7 powder toroid and 22 AWG enamelled wire. It is the shunt inductor at the output end of the matching network. With the series C10/C11 bank, it transforms the cartridge-and-cable impedance before that load reaches the inverter. See the [winding instructions](../build-guide-lite-470khz/custom-inductors.md).

## C10 and C11

C10 Value: 6n8

C11 Value: 1n

[C10 LCSC: C2182121](https://www.lcsc.com/product-detail/C2182121.html)

[C11 LCSC: C3859218](https://www.lcsc.com/product-detail/C3859218.html)

The final series matching bank between the last inductor node and the handpiece output. C10 is 6.8 nF and C11 adds 1 nF in parallel, giving 7.8 nF total. Both are deliberate parts of the RF match.

## JP1

Value: COAX-SMA

The SMA RF output connector for the 470 kHz handpiece and cable. Its shell returns through SJ2-SJ5. Use the compatible 470 kHz cartridge system; the full version's 13.56 MHz cartridges are a different load.

## D3

Value: ES1J

[LCSC: C64879](https://www.lcsc.com/product-detail/C64879.html)

Steers positive drain-voltage excursions into D4. It makes the TVS a protection branch while limiting its direct interaction with the normal RF waveform.

## D4

Value: SMCJ200CA

[LCSC: C2760132](https://www.lcsc.com/product-detail/C2760132.html)

The drain transient suppressor behind D3. The SMCJ200CA has a **200 V stand-off rating**, **224-247 V breakdown at 1 mA**, and a **324 V maximum clamp at 4.6 A** for a 10/1000 us surge pulse. That clamp rating exceeds Q1's 300 V drain rating, so this part alone doesn't guarantee Q1 protection. Its **1500 W rating is for a brief surge**, not continuous RF power.

## SJ1

Value: SJ-SMALL-1206-SJ

The connection from Q1's source to ground. Normally bridge it for a low-impedance return; a small temporary shunt can be used here to measure source current during testing.

## SJ2-SJ5

Value: SJ-SMALL-1206-SJ each

Four parallel connection positions between the SMA shell/RF return and circuit ground. Normal builds bridge them. If used for current sensing, the other parallel paths must not bypass the measurement shunt.

## PCB1

Value: PCB-OUTLINE

The board-outline CAD component. It defines mechanical board geometry and its grounding feature; it has no RF tuning value or active electronic function.

## PCB2

Value: PCB-ART-VER-LITE

The PCB version-artwork component. It records the board identity on the manufactured PCB and has no electrical role in the amplifier.

# RF Amp Gate Driver

U2 drives Q1 directly through separate turn-on and turn-off resistors. This is simpler than the resonant two-amplifier driver in the 13.56 MHz version. See the [gate-driver comparison](../design-study/gate-driver-method-comparison.md).

## U2

Value: 1EDN8511BXUSA1

[LCSC: C3655509](https://www.lcsc.com/product-detail/C3655509.html)

The gate-driver IC, translating the microcontroller's logic waveform into strong charging and discharging pulses for Q1. At 470 kHz it drives the main MOSFET directly; the full version needs an extra resonant amplifier. See the [driver comparison](../design-study/gate-driver-method-comparison.md) and [driver product page](https://www.infineon.com/part/1EDN8511B).

## R3 and R4

Value: 3R6 each

[LCSC: C445783](https://www.lcsc.com/product-detail/C445783.html)

The separate gate resistors for Q1: R3 connects U2's sourcing output for turn-on, while R4 connects its sinking output for turn-off. They limit charging/discharging current, damp ringing, and dissipate some gate-drive energy. The two paths can be adjusted independently.

## R1

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

A series resistor before U2's logic input. It damps fast-edge ringing and limits transient input current without carrying the main MOSFET's gate-charge current.

## R2

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Pulls U2's logic input low when the microcontroller output is undriven. This gives the RF switch a defined off command during reset or module removal.

## C18 and C19

C18 Value: 100n

C19 Value: 1uF

[C18 LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

[C19 LCSC: C15849](https://www.lcsc.com/product-detail/C15849.html)

Local 12 V decoupling for U2. The 100 nF C18 supplies fast gate-charge current pulses; the 1 uF C19 adds nearby energy storage and reduces movement of the driver's supply during repeated pulses.

# Microcontroller

The removable module produces the carrier and controls average power using RF bursts. Mode inputs, a voltage divider, and a logic power switch guide firmware behavior; this schematic has no direct tip-temperature sensor or tip-continuity detector. See the [burst-power study](../design-study/lite-power-attenuation.md).

## U1

Value: SEEED-XIAO-SAMD21-TH-LOCKING

The removable microcontroller module. It generates the 470 kHz carrier and burst-power patterns, reads input voltage and selection switches, and drives status LEDs and the fan. The symbol names a SAMD21 XIAO; other supported modules need matching firmware/pin mappings. See the [build guide](../build-guide-lite-470khz/build-guide-lite.md#3-microcontroller-and-firmware-bring-up).

## R26

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

A series resistor at the microcontroller's RF output, ahead of R1 and the U2 input. It reduces ringing on the logic connection; it is not part of the handpiece's RF power path.

## D2

Value: 1A 40V

[LCSC: C8598](https://www.lcsc.com/product-detail/C8598.html)

Feeds the microcontroller module's 5 V input from IC2 while blocking reverse current from the module into the board's 5 V rail. This does not replace the build guide's requirement to disconnect main power before flashing.

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

The slide switch grounds the RF on/off control net (`PWR-DIS`) in its selected position. Firmware treats the grounded input as the run request. This is a logic control, so the board remains powered when RF is off.

## R25

Value: 470Ω

[LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

The series resistor between the exposed power-switch/header connection and the microcontroller's RF on/off control net (`PWR-DIS`). It limits transient current; it does not carry or interrupt the RF amplifier supply.

## TVS4-TVS6

[LCSC: C840638](https://www.lcsc.com/product-detail/C840638.html)

ESD protection on the exposed control inputs: TVS4 and TVS5 protect the mode-selection nets `SEL2` and `SEL3`, respectively; TVS6 protects the RF on/off control net `PWR-DIS`. The linked Bourns CDSOD323-T03SC has a **3.3 V stand-off rating**, **4 V minimum breakdown at 1 mA**, and an **8 V maximum clamp at 1 A**. It catches brief spikes rather than holding the input at exactly 3.3 V.

Their respective series resistors R23, R24, and R25 add current limiting. These protect the logic signals rather than the high-current supply.

## R21 and R22

R21 Value: 100K

R22 Value: 4K7

[R21 LCSC: C25803](https://www.lcsc.com/product-detail/C25803.html)

[R22 LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The fused input-voltage divider. R21 is the upper 100 kohm resistor; R22 is the lower 4.7 kohm resistor to ground. Their roughly 22.3:1 scaling gives about 0.90 V on the microcontroller's voltage-sense net (`ADC`) for a 20 V input. Firmware uses this for voltage supervision and power-mode behavior; C23 filters the signal.

## C23

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Filters the microcontroller's input-voltage sensing net (`ADC`) to ground. It smooths switching noise while R21 and R22 determine the DC scaling.

## Indicator LEDs and Current-Limiting Resistors

LED1 and LED2 Value: LED

Firmware status indicators. Blink patterns report operating state, input-voltage range, and power selection; see the [Lite operating guide](../operating-instructions-for-lite.md#led-indicators). Both use the same microcontroller output, with a separate resistor limiting each LED's current.

| LED | Purpose | Current-limiting resistor | Resistor value | Expected drive current while lit |
| --- | --- | --- | --- | --- |
| LED1 ([LCSC](https://www.lcsc.com/product-detail/C284935.html)) | Main firmware status indicator | R5 ([LCSC](https://www.lcsc.com/product-detail/C23179.html)) | 470 ohm | About 2.8 mA |
| LED2 | Optional second indicator; mirrors LED1 | R6 ([LCSC](https://www.lcsc.com/product-detail/C23179.html)) | 470 ohm | About 2.8 mA when fitted |

These estimates use `I = (Vdrive - VLED) / R`, assuming a **3.3 V logic-high output** and **2 V LED forward voltage**. Actual current depends on the LED and output voltage; blinking reduces the average current.

# Secondary Power

IC1 produces 12 V for the gate driver and fan; IC2 drops that rail to 5 V for the microcontroller module. The fan switching circuit and its routing jumpers are in this schematic section too.

## IC1

Value: 12V R-78K12-2.0

[Digi-Key: R-78K12-2.0](https://www.digikey.com/en/products/detail/recom-power/R-78K12-2-0/18093036)

The fixed 12 V switching-regulator module. It powers the gate driver and fan, and also supplies the 5 V linear regulator through the 12 V rail.

## IC2

Value: AMS1117-5.0

[LCSC: C6187](https://www.lcsc.com/product-detail/C6187.html)

The 5 V linear regulator feeding the microcontroller module through D2. It drops the 12 V rail to 5 V and dissipates the difference as heat; the module generates its own logic supply.

## C14

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Local input decoupling for IC1 on the fused main supply. It reduces supply-trace noise reaching the secondary regulator.

## D1

Value: 2A

[LCSC: C698873](https://www.lcsc.com/product-detail/C698873.html)

A series diode from IC1 to the 12 V rail. It blocks the charged output rail from feeding backward into IC1 when its input collapses, with a small forward-voltage cost.

## C15

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

The reservoir on the 12 V rail after D1. It supports the gate driver, fan, and 5 V regulator during changes in load.

## C16

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

The output capacitor on IC2's 5 V rail. It supports microcontroller load changes and is part of the linear regulator's stability requirements; capacitor type and ESR should suit the actual fitted regulator.

## Q7

Value: Si2318

[LCSC: C14498](https://www.lcsc.com/product-detail/C14498.html)

The fan-control MOSFET. The solder jumpers can route it to switch the fan ground or provide an open-drain PWM signal, depending on the fan type.

## FAN1

Value: FAN-CONN-X4-CONN-X4

- Four-pin PWM fan: [Digi-Key: Molex 0470531000](https://www.digikey.com/en/products/detail/molex/0470531000/2421261).
- Two-pin fan: [Digi-Key: JST B2B-XH-A](https://www.digikey.com/en/products/detail/jst-sales-america-inc/B2B-XH-A/1651045), a 2.5 mm-pitch XH header.

The 12 V fan connector. SJ6 chooses the PWM source and SJ7 chooses the ground connection; a four-wire fan's speed input and a two-wire fan's switched supply require different routing.

Fit the two-pin header to FAN1's `V` and `G` pads. It has no separate PWM control pin; select the switched ground return through SJ7 for two-wire fan control.

## R27

Value: 470Ω

[LCSC: C23179](https://www.lcsc.com/product-detail/C23179.html)

The series resistor between the microcontroller fan signal and Q7's gate. It limits brief gate-charging current and damps fast-edge ringing.

## R28

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

Pulls the fan-control signal low when the microcontroller is not driving it. This gives Q7 a defined command during reset; jumper routing determines the resulting fan behavior.

## SJ6

Selects the fan PWM input between the direct microcontroller signal and Q7's open-drain output. The MOSFET path inverts the signal, so the firmware output mode must match the selected connection.

## SJ7

Selects whether the fan ground is permanently grounded or switched by Q7. Choose the connection for the intended fan mode; bridging both alternatives defeats switching.

## C17

Value: 10u

[LCSC: C13585](https://www.lcsc.com/product-detail/C13585.html)

Local 12 V reservoir beside the fan connector. It reduces disturbances from fan startup and commutation on the shared driver supply.

# XT30 DC Input

U3 and Q2/Q3 protect the XT30 power path and slow startup. F1 and the large input capacitors serve both input sources after they join. See the [ideal-diode controller overview](https://www.ti.com/product/LM7481-Q1).

## U3

Value: LM74810QDRRRQ1

[LCSC: C3215601](https://www.lcsc.com/product-detail/C3215601.html)

The ideal-diode/disconnect controller on the XT30 path. It drives Q2 and Q3 for low-loss forward power, reverse-current blocking, and controlled startup. USB presence disables it through Q6. See the [controller documentation](https://www.ti.com/product/LM7481-Q1).

## Q2

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

The input-side MOSFET controlled by U3's DGATE output. It implements the low-loss ideal-diode path from XT30 and helps prevent reverse current toward the battery.

[Vishay's ratings](https://www.vishay.com/en/product/76785/) are **60 V drain-source**, **±20 V gate-source**, and **52 A continuous with the case held at 25°C**. Maximum on-resistance is **7.2 milliohms at a 10 V gate drive**.

This is deliberately a bit overkill, and that's a personal choice. For a one-off project, I'd rather pay a little extra for the margin and not worry about it.

## Q3

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

The output-side MOSFET controlled by U3's HGATE output. It allows the XT30 input to be disconnected and turns on gradually to limit bulk-capacitor charging current.

[Vishay's ratings](https://www.vishay.com/en/product/76785/) are **60 V drain-source**, **±20 V gate-source**, and **52 A continuous with the case held at 25°C**. Maximum on-resistance is **7.2 milliohms at a 10 V gate drive**.

This is deliberately a bit overkill, and that's a personal choice. For a one-off project, I'd rather pay a little extra for the margin and not worry about it.

## JP3

Value: XT-CONNECTOR-XT30PW

The XT30 battery/DC connector. Its positive contact enters the protected input MOSFETs before reaching F1 and the RF amplifier supply.

## TVS1

Value: 36V

[LCSC: C134984](https://www.lcsc.com/product-detail/C134984.html)

A transient suppressor across the XT30 input. The SMAJ36CA has a **36 V stand-off rating**, **40-44.2 V breakdown at 1 mA**, and a **58.1 V maximum clamp at 6.9 A** for a 10/1000 us surge pulse. Its 36 V rating therefore isn't a fixed clamp level or a recommended supply voltage; the board's allowed input is limited by the whole design.

## C20

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Local high-frequency bypass across the XT30 input. It reduces fast connector/supply disturbances near U3's sensing and power input.

## C21

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

The charge-pump reservoir between U3's `CAP` pin and the common-drain node. It supports gate voltages above the MOSFET source voltage so the input switches can turn fully on.

## C24

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Decouples U3's supply at the MOSFET common-drain node. It supports local controller current and reduces gate-switching disturbances on that node.

## R29

Value: 100Ω

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

The series resistor in the slew-control branch on U3's `HGATE` pin. With C25 and C26, it shapes Q3's startup ramp without carrying the main load current.

## C25 and C26

Value: 100n each

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Two 100 nF capacitors in the slew-control branch on U3's `HGATE` pin. **They are in series because one capacitor's voltage rating isn't high enough for this position; the pair shares the applied voltage.** Together they give about 50 nF and slow Q3 turn-on while the bulk capacitors charge. See the [inrush-control documentation](https://www.ti.com/product/LM7481-Q1).

## R30

Value: 51K

[LCSC: C23196](https://www.lcsc.com/product-detail/C23196.html)

The upper resistor of U3's overvoltage divider. With the fitted 2 kohm R31, its 51 kohms sets a nominal cutoff around 32.6 V. This is an input disconnect threshold, not an RF-power regulator.

## R31

Value: 2K

[LCSC: C22975](https://www.lcsc.com/product-detail/C22975.html)

The lower resistor of the XT30 overvoltage divider. It returns U3's OV pin to ground and works with R30 to set the cutoff threshold.

## R32

Value: DNP

An optional resistor position in parallel with R31. Fitting it changes the overvoltage divider and cutoff; the default DNP position leaves R31 as the lower arm.

## R12

Value: 22K

[LCSC: C31850](https://www.lcsc.com/product-detail/C31850.html)

Pulls U3's enable input up from the common-drain supply node. USB voltage turns Q6 on to override this pull-up and disable the battery/DC path.

## F1

Value: 20x5MM 7A

[Digi-Key: 3518P fuse clip](https://www.digikey.com/en/products/detail/keystone-electronics/3518P/316011)

The main 7 A input fuse shared by the XT30 and USB paths. It interrupts sustained excessive current, such as a failed-short power component, but cannot prevent every fast RF or semiconductor failure. The supplier link is for a fuse clip.

## C12 and C13

Value: 470u each

[Digi-Key: UHW1H471MPD](https://www.digikey.com/en/products/detail/nichicon/UHW1H471MPD/3664388)

Two parallel 470 uF bulk input capacitors, totaling 940 uF when both are fitted. They supply brief RF current bursts, absorb some returned energy, and reduce demand spikes at the source. This large capacitance also makes controlled input charging important. See the [burst-power study](../design-study/lite-power-attenuation.md) and [assembly notes](../build-guide-lite-470khz/assembly-supplement.md#skipping-components-and-substitutions) for the C13 omission option in Sport-only builds.

# USB-PD Input

U4 negotiates USB-C power from resistor-coded voltage/current requests. USB presence disables the XT30 input through Q6. The schematic and BOM take precedence over older prose describing a different jumper default. See the [AP53781 overview](https://www.diodes.com/part/view/AP53781).

## U4

Value: AP53781

[LCSC: C52730979](https://www.lcsc.com/product-detail/C52730979.html)

The USB-PD controller, used as a power consumer. Resistance codes on its `VSEL` voltage-selection pin and `ISEL` current-selection pin request voltage and current, and its gate driver controls the USB input MOSFETs. A request still depends on source/cable support. See [AP53781 documentation](https://www.diodes.com/part/view/AP53781).

## Q4

Value: DMTH64M2LPDWQ-13

[Digi-Key: DMTH64M2LPDWQ-13](https://www.digikey.com/en/products/detail/diodes-incorporated/DMTH64M2LPDWQ-13/28171648)

A dual MOSFET package forming the back-to-back USB power switch. U4 drives the common gate to connect negotiated USB power and allows the input path to block when disabled.

[Diodes' ratings](https://www.diodes.com/part/view/DMTH64M2LPDWQ) are **60 V drain-source**, **±20 V gate-source**, and **90 A continuous per MOSFET with the case held at 25°C**. Maximum on-resistance is **5 milliohms per MOSFET at a 10 V gate drive**, or **10 milliohms for the conducting pair** under those conditions.

This is deliberately a bit overkill, and that's a personal choice. For a one-off project, I'd rather pay a little extra for the margin and not worry about it.

## Q5

Value: SIS862ADN-T1-GE3

[Digi-Key: SIS862ADN-T1-GE3](https://www.digikey.com/en/products/detail/vishay-siliconix/SIS862ADN-T1-GE3/10273791)

An additional isolation MOSFET between the common DC bus and the USB switching circuit. It helps keep high battery voltage away from the AP53781's lower-voltage pins.

[Vishay's ratings](https://www.vishay.com/en/product/76785/) are **60 V drain-source**, **±20 V gate-source**, and **52 A continuous with the case held at 25°C**. Maximum on-resistance is **7.2 milliohms at a 10 V gate drive**.

This is deliberately a bit overkill, and that's a personal choice. For a one-off project, I'd rather pay a little extra for the margin and not worry about it.

## JP2

Value: USBC-CHGONLY-4M-DRILL

[Digi-Key: UJC-H-G-SMT-2-P6-TR](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/UJC-H-G-SMT-2-P6-TR/24818567)

The charge-only USB-C connector. Its VBUS pins carry power and its CC1/CC2 pins carry USB-C/PD negotiation; this is not a USB data connection for flashing the microcontroller module.

## R10 and R11

Value: 100Ω each

[LCSC: C441975](https://www.lcsc.com/product-detail/C441975.html)

Two series resistors between U4's VOUT sense pin and the Q4/Q5 junction, totaling 200 ohms. They limit transient current into the sense connection and isolate it from disturbances at the power-switch junction, without adding resistance to the main supply path.

## R9

Value: 4K3

[LCSC: C23159](https://www.lcsc.com/product-detail/C23159.html)

The series resistor between U4's VG_A output and the power-MOSFET gates. It limits charging current and affects turn-on speed, including startup inrush.

## R15 and R16

R15 Value: 51K

R16 Value: 1K

[R15 LCSC: C23196](https://www.lcsc.com/product-detail/C23196.html)

[R16 LCSC: C21190](https://www.lcsc.com/product-detail/C21190.html)

The default resistance code for U4's `VSEL` voltage-selection pin: R15 is 51 kohms and R16 adds 1 kohm in series, giving about 52 kohms to ground for a 15 V request. This programs USB-PD negotiation; it does not directly regulate the bus voltage. Enabled alternate branches change the effective programming resistance.

## R17 and R18

R17 Value: 100K

R18 Value: 56K

[R17 LCSC: C25803](https://www.lcsc.com/product-detail/C25803.html)

[R18 LCSC: C23206](https://www.lcsc.com/product-detail/C23206.html)

The alternate 20 V selection branch. R17 and R18 total 156 kohms in series. Grounding the voltage-selection control net (`USB-V-SEL-20V`) through JP5 or SJ8 puts this branch in parallel with the default R15/R16 branch, giving about 39 kohms at U4's `VSEL` pin and requesting 20 V.

## R19 and R20

R19 Value: 51K

R20 Value: 1K

[R19 LCSC: C23196](https://www.lcsc.com/product-detail/C23196.html)

[R20 LCSC: C21190](https://www.lcsc.com/product-detail/C21190.html)

The optional 28 V selection branch. Their 51 kohms and 1 kohm in series add another 52 kohm path when grounded through JP5. In parallel with the default R15/R16 branch, this gives approximately 26 kohms at U4's `VSEL` voltage-selection pin for a 28 V request. Both parts are DNP in the current BOM, so the shunt alone cannot enable this option unless they are fitted.

## JP5

The USB-PD profile-selection header. It grounds the current-code path and either alternate voltage branch. The current BOM fits the 20 V branch and leaves the 28 V pair unpopulated; these selections are separate from JP6's firmware power modes.

## R13 and R14

R13 Value: 24K

R14 Value: 2K

[R13 LCSC: C23352](https://www.lcsc.com/product-detail/C23352.html)

[R14 LCSC: C22975](https://www.lcsc.com/product-detail/C22975.html)

The default resistance code for U4's `ISEL` current-selection pin: R13 and R14 total 26 kohms to ground, requesting 3 A. A JP5 current shunt or SJ9 overrides this pair with a direct ground connection to request 5 A. These are negotiation settings; the source and cable must support the requested current.

## SJ8

Value: SJ-SMALL-2S-NO

A solder-jumper alternative to the JP5 20 V shunt. Bridging it permanently grounds the voltage-selection control net (`USB-V-SEL-20V`), enabling the R17/R18 branch.

## SJ9

Value: SJ-SMALL-2S-NO

A solder-jumper alternative to JP5's current-selection shunt. Bridging it shorts the current-selection control net (`USB-I-SEL`) directly to ground, overriding the default R13/R14 resistance and requesting 5 A. It selects current independently of SJ8's voltage choice.

## Q6

Value: Si2318

[LCSC: C14498](https://www.lcsc.com/product-detail/C14498.html)

The USB-presence switch. The USB power rail (`USB-VBUS`) drives its gate through R8/R7, and its drain pulls U3's enable low so the XT30 path is disabled while USB is present.

## R7

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The lower resistor of Q6's USB-presence divider and a gate pull-down. It returns Q6's gate toward ground when USB power is absent.

## R8

Value: 4K7

[LCSC: C23162](https://www.lcsc.com/product-detail/C23162.html)

The upper resistor from the USB power rail (`USB-VBUS`) to Q6's gate divider. It scales the signal with R7 and limits current into the gate-protection TVS.

## TVS3

Value: 15V

[LCSC: C2987151](https://www.lcsc.com/product-detail/C2987151.html)

Clamps excessive voltage and fast spikes on Q6's gate-sense node. The linked TECH PUBLIC SD15C-01FTG is listed with a **15 V stand-off rating** and a **35 V clamp rating at 10 A** for an 8/20 us pulse. So "15V" doesn't mean the gate is held at exactly 15 V. The BOM also lists other part options, whose clamp ratings differ.

It protects the USB-presence MOSFET rather than clamping the full VBUS rail.

## TVS2

Value: 36V

[LCSC: C134984](https://www.lcsc.com/product-detail/C134984.html)

A transient suppressor across the USB power rail (`USB-VBUS`). The SMAJ36CA has a **36 V stand-off rating**, **40-44.2 V breakdown at 1 mA**, and a **58.1 V maximum clamp at 6.9 A** for a 10/1000 us surge pulse. It catches short spikes above that rail's normal voltage; it does not regulate the negotiated voltage or make the USB controller tolerant of sustained overvoltage.

## C22

Value: 100n

[LCSC: C14663](https://www.lcsc.com/product-detail/C14663.html)

Local decoupling for U4's internal 5 V output pin (`V5V`). It supports the PD controller's own circuitry, rather than storing the main RF burst energy.
