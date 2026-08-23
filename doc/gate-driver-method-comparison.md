# 13.56 MHz Gate-Driver Method Comparison

SergeyMax's design has two power MOSFETs instead of driving the main RF MOSFET directly. In the original SergeyMax design, a MAX17602 gate-driver IC drives the relatively small IRF510 Q2. Q2, L7, C37/C38, L8, and the capacitance of Q1 form a small RF power amplifier whose load is the gate of the main STP19NF20 Q1.

Hot-Wand preserves that two-stage RF architecture but substitutes a 1EDN8511B for the MAX17602. This substitution was selected after comparing available drivers and is considered a potentially better fit for this implementation; it is not a component used or specified by SergeyMax. In the Hot-Wand schematic, U2 therefore means the 1EDN8511B. The two stages are:

1. U2 hard-switches the gate of Q2.
2. Q2 and the resonant network move charge into and out of the much larger gate of Q1.

The apparently simpler alternative would be to delete Q2 and its RF network and connect the 1EDN8511B directly to Q1. The driver has enough *peak-current* capability to make this look plausible: Infineon specifies 4 A source and 8 A sink. Peak current is not the main problem, however. At 13.56 MHz, the average amount of charge moved on every cycle, the resulting power dissipation, and the very small thermal mass of the SOT-23 driver all matter.

## Original Driver and Hot-Wand Substitution

The [MAX17602](https://www.analog.com/en/products/max17602.html) is a dual MOSFET driver with one inverting and one non-inverting channel. Analog Devices specifies 4 A peak source and sink current, 12 ns typical propagation delay, and typical 6 ns rise and 5 ns fall times with a 1 nF load.

The 1EDN8511B is a single-channel driver with separate source and sink output pins. It provides 4 A peak source current, 8 A peak sink current, and permits separate turn-on and turn-off gate resistors. It also specifies reverse-current robustness at its output. Those features made it look attractive for controlling Q2, particularly because rapidly and firmly turning Q2 off is useful in this RF switching stage.

The substitution is not better in every specification. The MAX17602 has a shorter typical propagation delay and provides two driver channels, while the 1EDN8511B has a stronger sink output and a simpler interface for separate source and sink paths. Calling the 1EDN8511B "potentially better" means that it appears better suited to this particular implementation; final judgment still depends on the measured waveform, power consumption, and temperature at 13.56 MHz.

All calculations below that refer to U2 or to the 1EDN8511B describe the Hot-Wand substitution. They should not be read as specifications or analysis of the original MAX17602 implementation.

## Datasheet Values Used

The relevant numbers are:

| Part      | Role                  | Input capacitance, C_iss | Total gate charge, Qg | Gate-charge test condition              |
|:----------|:----------------------|-------------------------:|----------------------:|:----------------------------------------|
| STP19NF20 | Main RF amplifier, Q1 | 800 pF typical           | 24 nC typical         | VDS = 160 V, ID = 15 A, VGS = 0 to 10 V |
| IRF510    | Gate RF amplifier, Q2 | 180 pF typical           | 8.3 nC maximum        | VDS = 80 V, ID = 5.6 A, VGS = 0 to 10 V |

These come from the [STP19NF20 datasheet](https://www.st.com/resource/en/datasheet/stb19nf20.pdf) and the [Vishay IRF510 datasheet](https://www.vishay.com/docs/91015/irf510.pdf). The conditions do not exactly reproduce this class-E circuit, so the results below are engineering estimates, not precision loss predictions. In particular, Qg changes with drain voltage, drain current, and the gate-voltage excursion.

The [1EDN751x/1EDN851x datasheet](https://www.infineon.com/assets/row/public/documents/24/49/infineon-1edn751x-1edn851x-datasheet-en.pdf) specifies the following typical characteristics at a 12 V supply:

- 4 A peak source and 8 A peak sink capability;
- 0.85 ohm source and 0.35 ohm sink output resistance;
- 19 ns propagation delay;
- 6.5 ns rise time and 4.5 ns fall time with a 1.8 nF test load; and
- 170 degrees C/W junction-to-ambient thermal resistance for the SOT-23-6 package on the specified test board.

The RF period is only:

```text
T = 1 / 13.56e6 = 73.75 ns
```

The propagation delay mostly shifts the waveform in time and can be compensated by phase. Rise time, fall time, power, and temperature are the harder constraints. Infineon's power-versus-frequency graph only extends to 1 MHz, so operation at 13.56 MHz is an extrapolation even though the individual timing specifications are fast enough.

## C_iss Is Useful for Tuning, but Qg Is Better for Power

C_iss is measured as a small-signal capacitance with VGS = 0 V. It is useful for the first resonance estimate for L8:

```text
L = 1 / ((2 * pi * f)^2 * C_iss)
L = 1 / ((2 * pi * 13.56e6)^2 * 800e-12)
L = 172 nH
```

That explains the nominal 180 nH L8. It does not mean that Q1 behaves like an ideal, constant 800 pF capacitor during switching. The gate-drain capacitance changes as the drain voltage moves, and the driver must also supply the Miller charge. The datasheet's total gate charge is a better starting point for driver power.

The difference can be seen with a simple capacitor calculation. Charging an ideal 800 pF gate from 0 V to 12 V requires:

```text
Q = C * V
Q = 800e-12 * 12
Q = 9.6 nC

P = C * V^2 * f
P = 800e-12 * 12^2 * 13.56e6
P = 1.56 W
```

The 9.6 nC result is much lower than Q1's specified 24 nC total gate charge. Therefore, using only C_iss would substantially understate the load seen by a hard-switching driver. It is still useful when estimating the resonant frequency because the resonator responds to incremental capacitance, but even that result must be tuned on the real hardware.

## Charge Moved on Every Cycle

For a conventional gate driver, Qg enters the gate during turn-on and approximately the same charge leaves during turn-off. Consequently:

```text
charge delivered by the supply per cycle = Qg
absolute charge moved in and out per cycle = 2 * Qg

average supply current = Qg * f
average charge traffic = 2 * Qg * f
gate-drive power from the supply = Qg * Vdrive * f
```

The 4 A and 8 A driver specifications describe brief current peaks during an edge. They do not mean that the driver continuously consumes 4 A. The averages calculated from Qg are more useful for power budgeting.

At 13.56 MHz, the charge comparison is:

| Hard-driven MOSFET | Qg used | Charge in + out per cycle | Average supply current | Average charge traffic | Power at 10 V | 12 V estimate using the same Qg |
|:-------------------|--------:|--------------------------:|-----------------------:|-----------------------:|--------------:|--------------------------------:|
| Q1, STP19NF20      | 24 nC   | 48 nC                     | 325 mA                 | 651 mA                 | 3.25 W        | 3.91 W                          |
| Q2, IRF510         | 8.3 nC  | 16.6 nC                   | 113 mA                 | 225 mA                 | 1.13 W        | 1.35 W                          |

The 12 V column is deliberately labelled as an estimate. Both Qg values were specified for a 0-to-10 V gate excursion, and Q1's charge is typical while Q2's is a maximum. The table is most useful for showing scale: directly hard-switching Q1 makes the 1EDN8511B handle about 2.9 times as much gate charge as it handles while driving Q2.

This also explains why a direct driver is easy at 470 kHz but troublesome here. With the same Q1 and a 10 V gate drive:

```text
Pgate at 470 kHz  = 24e-9 * 10 * 470e3   = 0.113 W
Pgate at 13.56 MHz = 24e-9 * 10 * 13.56e6 = 3.25 W

13.56 MHz / 470 kHz = 28.9
```

The driver moves the same charge on each edge, but it must do it 28.9 times as often.

### Gate resistor recommendation for the IRF510

The 13.56 MHz design uses separate 3.6 ohm turn-on and turn-off resistors between the 1EDN8511B and the IRF510. These parts must not be selected as ordinary low-power gate resistors. Using the 1.35 W, 12 V gate-power estimate above, 1EDN8511B's typical output resistances, and the IRF510's 2.5 ohm typical internal gate resistance gives approximately:

```text
P(R-turn-on)  ~= (1.35 W / 2) * 3.6 / (0.85 + 3.6 + 2.5) = 0.35 W
P(R-turn-off) ~= (1.35 W / 2) * 3.6 / (0.35 + 3.6 + 2.5) = 0.38 W
```

The [IRF510 datasheet](https://www.vishay.com/docs/91015/irf510.pdf) specifies 2.5 ohms only as a typical gate-input resistance at 1 MHz; it gives no guaranteed minimum. Conservatively ignoring that internal resistance raises the estimates to about 0.55 W for R_turn_on and 0.62 W for R_turn_off. The real losses depend on Q2's actual gate-charge operating point and waveform, but these estimates rule out ordinary 0.1 W or 0.25 W parts and leave too little assurance for an ordinary 0.5 W part.

**Recommendation:** fit both resistors with the 3.6 ohm `1206` footprint pulse-withstanding resistor rated at least 0.75 W at 70 degrees C. The Yageo part `SR1206FR-7T3R6L` is an AEC-Q200 surge-resistant thick-film resistor rated 0.75 W at 70 degrees C, giving useful margin over the expected 0.35 W to 0.38 W. Its permissible power derates above 70 degrees C, so the assembled board should be tested at continuous RF output and the resistor temperatures checked in the final enclosure.

## What Happens If the 1EDN8511B Drives Q1 Directly?

The approximately 3.25 W at 10 V, or roughly 3.9 W at 12 V, is not all dissipated inside the driver IC. It is divided among the driver's output transistors, external gate resistors, Q1's internal gate resistance, and other series resistance. Nevertheless, all of it becomes heat somewhere; none of the energy placed in the gate is recovered by a conventional push-pull connection.

As a deliberately simplified illustration, suppose the existing 3.6 ohm source and sink resistors were reused and Q1's internal gate resistance and PCB resistance were ignored. The share dissipated by U2 would be approximately:

```text
source-edge driver share = 0.85 / (0.85 + 3.6) = 19.1%
sink-edge driver share   = 0.35 / (0.35 + 3.6) =  8.9%

P(U2) ~= (3.91 W / 2) * (19.1% + 8.9%)
P(U2) ~= 0.55 W
```

At 170 degrees C/W, 0.55 W corresponds to a 94 degrees C junction rise on the datasheet test board. This estimate omits the driver's own high-frequency switching loss and is strongly dependent on layout and resistance, so the thermal margin would be uncomfortable. Adding Q1's internal resistance would reduce U2's share, but that only moves some of the heat into Q1.

The same simplified split puts about 1.6 W into the turn-on resistor and about 1.8 W into the turn-off resistor. Q1's internal gate resistance would reduce those figures, but ordinary 0603 resistors would still not be a sensible assumption for a direct-drive experiment. Reducing the external resistance makes the edges faster but moves more heat into U2; increasing it protects U2 and the resistors but increases Q1's switching time and drain switching loss.

There is also a timing problem not included in Pgate. Q1's datasheet gives typical rise and fall times of 22 ns and 11 ns under its specified test conditions. A complete RF period is only 73.75 ns. Those datasheet switching times cannot be copied directly into the class-E operating point, but they show why merely having adequate peak gate current does not guarantee a clean, efficient 13.56 MHz power stage.

## What the RF Gate Amplifier Changes

The resonant method does **not** make Q1's gate charge disappear. Approximately the same charge still moves into and out of Q1, and substantial RF current can circulate through L8 and the gate. What changes is where that charge comes from.

With a direct driver, charge comes from the 12 V rail during every turn-on and is dumped to ground during every turn-off. With L8 tuned against Q1's gate capacitance, energy alternates between the electric field in the gate capacitance and the magnetic field in L8. Q2 only has to replenish the energy lost in the real resistances and deliberately shape the oscillation. The large Q1 gate current circulates locally instead of all of it passing through the tiny 1EDN8511B package.

For the idealized 800 pF gate charged to a 12 V peak, the maximum stored energy is:

```text
E = 0.5 * C * Vpeak^2
E = 0.5 * 800e-12 * 12^2
E = 57.6 nJ
```

A hard driver draws twice that amount from its supply per cycle:

```text
Esupply per cycle = C * V^2 = 115.2 nJ
```

Half is lost while charging the capacitance and the energy remaining in the capacitance is lost while discharging it. An ideal resonator keeps the 57.6 nJ and passes it back and forth. A real resonator only needs its losses replaced. For a loaded quality factor Qloaded:

```text
Presonator_loss ~= 2 * pi * f * E / Qloaded
```

Using the idealized 800 pF and 12 V peak gives:

| Assumed loaded Q | Estimated resonator loss |
|-----------------:|-------------------------:|
| 5                | 0.98 W                   |
| 10               | 0.49 W                   |
| 20               | 0.25 W                   |

This is only a scale estimate. Q1's capacitance is nonlinear, its drain is moving, L8 has resistance, and Q2 is not an ideal lossless switch. R12 also deliberately damps and discharges the Q1 gate. With a 12 V peak sine wave, R12 alone would dissipate:

```text
Vrms = 12 / sqrt(2) = 8.49 V
P(R12) = Vrms^2 / 150 = 0.48 W
```

That corresponds to a parallel quality factor of about 10 for an 800 pF load at 13.56 MHz, before coil, MOSFET, and PCB losses are counted. The actual simulated waveform has a DC offset and is not a perfect sine, so R12's real dissipation must be measured from its RMS voltage.

## Whole-Circuit Power Comparison

The important comparison is not 800 pF versus zero. The present circuit saves energy at Q1 but pays to hard-switch Q2 and to operate Q2's drain resonator.

| Method | Hard-switched gate power | Additional RF-stage power | Napkin total from the 12 V rail |
|:-------|-------------------------:|--------------------------:|---------------------------------:|
| 1EDN8511B directly driving Q1 | about 3.25 W at 10 V; about 3.9 W as a 12 V estimate | none | about 3.3 to 4 W, plus driver self-consumption |
| 1EDN8511B driving Q2, then resonant drive of Q1 | about 1.13 W at 10 V; about 1.35 W as a 12 V estimate | approximately 1.2 W in the existing tuned simulation | approximately 2.3 to 2.6 W, plus driver self-consumption |

The 1.2 W RF-stage figure is the simulated 100 mA draw from a 12 V source after tuning. The simulation uses an ideal source at Q2's gate, so Q2's gate-drive power must be added; otherwise the comparison double-counts the benefit. The physical board may differ substantially because the hand-wound coil, Q1 capacitance, Q2 switching loss, and waveform are all imperfect.

On this basis, directly driving Q1 from 12 V would consume roughly 4 W, while the complete resonant approach is estimated to consume about 2.3 to 2.6 W. The RF gate amplifier therefore plausibly saves around 1 to 2 W and, just as importantly, **moves much of the remaining dissipation out of the small U2 package and the small resistors**. These are still napkin estimates: the final difference depends on Q1's operating-point gate charge, the loaded Q of the resonant network, Q2's switching losses, and the actual RMS voltage across R12.

## If L8 Is Badly Tuned

As an example, suppose a badly tuned L8 makes the complete gate-drive circuit draw 500 mA from the 12 V bus:

```text
Pinput = V * I
Pinput = 12 * 0.5
Pinput = 6 W
```

The gate-drive subsystem is then receiving 6 W. The 2 A rating of the 12 V buck converter means that it can supply this current without being overloaded; it does not say where the 6 W is dissipated. A mistuned resonant network redistributes that power among its switching, damping, magnetic, and clamping losses.

The extra power caused by mistuning can appear in several places:

| Component or path | Effect of detuning |
|:------------------|:-------------------|
| Q2                | Conduction and switching loss increase when its drain waveform moves away from class-E operation. The heatsink removes average heat from Q2, while drain-voltage peaks and avalanche operation remain separate electrical stresses. |
| R12               | It continuously damps the Q1 gate waveform. Its dissipation is `Vgate_rms^2 / 150`. A 17.3 V RMS waveform corresponds to its 2 W rating because `sqrt(2 * 150) = 17.3`. |
| TVS2 and D9       | They conduct when the gate waveform reaches their clamping regions. Visible clipping therefore identifies some of the additional input power as clamp dissipation. |
| L8                | Its circulating RF current can be much greater than the 500 mA drawn from the supply. Skin effect, proximity effect, and ordinary copper resistance convert part of that circulating current into heat. |
| L7                | It carries the gate amplifier's DC input current plus RF ripple. Both copper loss and core loss depend on the resulting current waveform. |
| C37/C38 and PCB resistance | Capacitor ESR, dielectric loss, and trace resistance dissipate part of the circulating RF energy. Detuning can also increase the voltage across the coupling capacitors. |
| U2, R16, and R17  | These primarily pay for charging Q2's gate rather than directly absorbing all of the additional tank loss. Q2's altered drain waveform can still change its Miller charge and the current returned through the driver. |

The 6 W figure only describes the power entering from the 12 V bus. Resonant voltage and current magnification must be considered separately. Q2's drain voltage can be much greater than 12 V, and L8's circulating current can be much greater than 500 mA. Q2's heatsink helps with its share of the real power loss, but it does not reduce either of those RF magnification effects.

L8 tuning also affects the main amplifier through Q1's gate waveform. If Q1 does not turn fully on and off at the intended times, its additional dissipation is supplied by the main adjustable buck converter. That power is not included in the 6 W gate-drive calculation, so the 12 V current measurement and the main RF power measurement describe two different parts of the circuit.

During initial tuning, the useful measurements are:

1. Total current drawn from the 12 V bus.
2. Q1 VGS, including its minimum, maximum, and RMS values.
3. Q2 VDS, especially its peak voltage and the shape of the drain waveform.
4. RMS voltage across R12, from which its power can be calculated directly.
5. Continuous or frequent conduction of TVS2 or D9, visible as clipping of the gate waveform.
6. Temperatures of Q2, R12, L7, L8, U2, R16, and R17 after reaching thermal equilibrium.
7. Main-buck input power, which reveals losses caused by an incorrectly driven Q1 but does not appear in the 12 V gate-driver measurement.

Starting with the main buck output disabled or limited and using a current-limited 12 V source makes these quantities easier to inspect while L8 is adjusted. The final tuned condition is identified by the required Q1 gate waveform, a class-E-like Q2 drain waveform, low clamp conduction, and a minimum in the gate amplifier's 12 V input current.

## Conclusion

Driving Q1 directly with the 1EDN8511B might produce a waveform on the bench, because the IC has adequate peak current and its edge times are short relative to a 73.75 ns period. It is not an attractive drop-in simplification. It asks a SOT-23 device and its gate resistors to process several watts of gate energy at a frequency far beyond the datasheet's power-consumption plot, leaves little thermal margin, and can add main-MOSFET switching loss if the edges or phase are wrong.

The RF-amplifier method is more complicated and must be tuned, but it has a sound purpose: in Hot-Wand, the substituted 1EDN8511B hard-switches the IRF510's smaller gate, while L8 and Q1 exchange most of Q1's much larger gate energy locally. The expected saving is probably a factor around 1.5 to 2 in total auxiliary power, rather than an order of magnitude. The final comparison should be made by measuring total 12 V current, U2 temperature, R12 temperature, and Q1's VGS waveform on the assembled board.

## Appendix: Calculations for 470 kHz Lite version

Hot-Wand Lite eliminates the resonant gate amplifier and drives its power MOSFET directly. Its U2 is still a 1EDN8511B powered from 12 V, with separate 3.6 ohm source and sink resistors. The calculations below describe continuous 470 kHz operation, corresponding to a 100 percent RF pulse density. At lower power settings, the average gate-drive power scales approximately with the fraction of carrier pulses that are present.

Two possible power MOSFETs are compared:

| MOSFET | C_iss used | Qg used | Gate-charge test condition |
|:-------|-----------:|--------:|:---------------------------|
| IRF640NPBF | 1160 pF typical | 67 nC maximum | VDS = 160 V, ID = 11 A, VGS = 0 to 10 V |
| STP19NF20 | 800 pF typical | 24 nC typical | VDS = 160 V, ID = 15 A, VGS = 0 to 10 V |

The IRF640NPBF values come from the [Infineon IRF640N datasheet](https://www.infineon.com/assets/row/public/documents/24/49/infineon-irf640n-datasheet-en.pdf). As in the 13.56 MHz calculations, Qg is a better estimate of hard-switching power than C_iss because it includes the voltage-dependent gate-drain and Miller charge under the datasheet's switching condition.

### Gate charge, current, and total drive power

For a conventional push-pull driver:

```text
average supply current = Qg * f
average charge traffic = 2 * Qg * f
gate-drive power from the supply = Qg * Vdrive * f
```

At 470 kHz this gives:

| MOSFET | Qg used | Charge in + out per cycle | Average supply current | Average charge traffic | Power at 10 V | 12 V estimate using the same Qg |
|:-------|--------:|--------------------------:|-----------------------:|-----------------------:|--------------:|--------------------------------:|
| IRF640NPBF | 67 nC | 134 nC | 31.5 mA | 63.0 mA | 0.315 W | 0.378 W |
| STP19NF20 | 24 nC | 48 nC | 11.3 mA | 22.6 mA | 0.113 W | 0.135 W |

For example, the IRF640NPBF calculation is:

```text
Isupply = 67e-9 * 470e3 = 31.5 mA
Pgate at 10 V = 67e-9 * 10 * 470e3 = 0.315 W
Pgate at 12 V ~= 67e-9 * 12 * 470e3 = 0.378 W
```

The 12 V results are estimates because both datasheet Qg values describe a 0-to-10 V gate excursion. The real charge required to reach 12 V will be somewhat greater and will also depend on the MOSFET's drain voltage and current in the assembled circuit.

Using C_iss alone would predict substantially less power:

```text
IRF640NPBF: P = 1160e-12 * 12^2 * 470e3 = 0.0785 W
STP19NF20:  P =  800e-12 * 12^2 * 470e3 = 0.0541 W
```

For the IRF640NPBF, `C_iss * 12 V` represents only 13.9 nC, compared with its specified 67 nC total gate charge. For the STP19NF20 it represents 9.6 nC, compared with 24 nC. These capacitor-only results should therefore not be used to rate U2 or the gate resistors.

### Estimated U2 and gate-resistor heating

Using the same deliberately simplified model as the main calculation, take U2's typical source resistance as 0.85 ohm and its sink resistance as 0.35 ohm. Ignore the MOSFET's internal gate resistance and PCB resistance. The fractions of each edge's loss assigned to U2 are then:

```text
source-edge driver share = 0.85 / (0.85 + 3.6) = 19.1%
sink-edge driver share   = 0.35 / (0.35 + 3.6) =  8.9%
```

Half of the total gate-drive power is associated with charging the gate and half with discharging it. At the 12 V estimates:

| MOSFET | U2 source loss | U2 sink loss | Total U2 output-stage loss | 3.6 ohm turn-on resistor | 3.6 ohm turn-off resistor |
|:-------|---------------:|-------------:|---------------------------:|------------------------:|-------------------------:|
| IRF640NPBF | 36.1 mW | 16.7 mW | 52.8 mW | 0.153 W | 0.172 W |
| STP19NF20 | 12.9 mW | 6.0 mW | 18.9 mW | 0.0548 W | 0.0617 W |

The IRF640NPBF driver calculation, for example, is:

```text
P(U2) ~= (0.378 W / 2) * (19.1% + 8.9%)
P(U2) ~= 0.0528 W

P(turn-on resistor)  ~= (0.378 W / 2) * 3.6 / (0.85 + 3.6)
P(turn-on resistor)  ~= 0.153 W

P(turn-off resistor) ~= (0.378 W / 2) * 3.6 / (0.35 + 3.6)
P(turn-off resistor) ~= 0.172 W
```

With the 1EDN8511B's specified 170 degrees C/W junction-to-ambient thermal resistance, these output-stage losses correspond to idealized junction rises of approximately:

```text
IRF640NPBF: 0.0528 W * 170 degrees C/W = 9.0 degrees C
STP19NF20:  0.0189 W * 170 degrees C/W = 3.2 degrees C
```

This is much more comfortable for U2 than direct drive at 13.56 MHz. It does not include U2's quiescent consumption, input switching loss, cross-conduction, PCB thermal conditions, or the MOSFET's internal gate resistance. Internal gate resistance would reduce U2's and the external resistors' shares, but the corresponding energy would still become heat inside the MOSFET.

The external resistors are the tighter concern with the IRF640NPBF. Its estimates of 0.153 W and 0.172 W each exceed the common 0.1 W nominal rating of an ordinary 0603 resistor, even before temperature derating and the underestimate caused by extending a 10 V Qg value to 12 V. The STP19NF20 estimates of about 55 mW and 62 mW are below 0.1 W, although pulse capability, temperature derating, and the actual assembled waveform still need to be checked against the selected resistor datasheet.

Ignoring MOSFET internal gate resistance, the initial edge-current estimates are the same for either MOSFET:

```text
initial source current ~= 12 / (0.85 + 3.6) = 2.70 A
initial sink current   ~= 12 / (0.35 + 3.6) = 3.04 A
```

Both are below U2's 4 A source and 8 A sink peak ratings. The IRF640NPBF requires the current pulses to move substantially more charge, which is why it produces approximately 2.8 times the driver and resistor heating of the STP19NF20 at the same frequency and voltage.

These calculations cover only the gate-drive subsystem. They do not predict the power MOSFET's drain switching loss, which depends on the measured VDS and ID overlap, the load network, and the achieved gate waveform. The final check should measure both gate resistors' temperatures, U2's temperature, Q1 VGS, and the input power difference between RF enabled and disabled at continuous full output.
