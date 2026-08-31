# Hot-Wand Lite 470 kHz Impedance Matching

## Origin and Scope

The Hot-Wand Lite RF power stage is derived from the open-source
[RadioThermal 470 kHz soldering station](https://github.com/RadioThermal/RadioThermal_Soldering_OSHW).
It drives Metcal/OKi STP, SFP, and SCP series cartridges and compatible
Thermaltronics P-series cartridges through a 50 ohm RG-174 cable and SMA
connectors.

This document calculates the ideal fundamental-frequency behavior of the
components fitted in `electrical/hot-wand-lite.sch`. It does not assume that the
cartridge itself is a perfect 50 ohm resistor. Actual cold, warm, and hot
impedance measurements at 470 kHz are still needed for a temperature-correlated
model.

## Circuit Topology

The complete drain-to-tip network is:

```text
DC supply
|
L1, 33 uH RF choke
|
+-- C1, 15 nF -- GND
|
Q1 drain -- C2..C5 -- L2 -------+-- L3 -------+-- L4 -------+-- C10..C11 -- RF output -- tip
             81 nF    6.2 uH    |   16.9 uH   |   16.9 uH   |    7.8 nF
                                |             |             |
                              C6..C8          C9            L5
                               30 nF         6.8 nF        11.5 uH
                                |             |             |
                               GND           GND           GND
```

The capacitor-bank totals are:

```text
C2..C5   = 22 nF + 22 nF + 22 nF + 15 nF = 81 nF
C6..C8   = 10 nF + 10 nF + 10 nF         = 30 nF
C10..C11 = 6.8 nF + 1 nF                 = 7.8 nF
```

The parts are easier to understand as functional blocks rather than as a chain
of independent resonators:

| Block | Components | Purpose |
| --- | --- | --- |
| Class-E drain network | L1 and C1 | L1 supplies DC as an RF choke; C1 helps shape the drain waveform |
| DC coupling and input transformation | C2–C5, L2, and C6–C8 | Blocks drain DC and transforms the inverter's input into the inductive load required by the class-E stage |
| Immittance converter | L3, C9, and L4 | A nearly ideal 50 ohm impedance inverter at 470 kHz |
| Output matching | L5 and C10–C11 | Transforms the cartridge-side impedance before it reaches the inverter |

The original RadioThermal LTspice file labels these areas as the class-E
amplifier, matching network, immittance converter, and final matching network.
That source file is
[`Full 470kHz schem.asc`](https://github.com/RadioThermal/RadioThermal_Soldering_OSHW/blob/main/Power%20Supply/Full%20470kHz%20schem.asc).
Its component numbering and some values predate the current Hot-Wand Lite
schematic, so it is used here only to identify the intended functional blocks;
all numerical calculations use the values in `electrical/hot-wand-lite.sch`.

## Calculation Method

At the carrier frequency:

```text
f = 470e3 Hz
omega = 2*pi*f = 2.9531e6 rad/s
```

The ideal component impedances are:

```text
ZL = j*omega*L
ZC = 1/(j*omega*C) = -j/(omega*C)
```

Parallel impedances are combined with:

```text
parallel(Za, Zb) = 1/(1/Za + 1/Zb)
```

Starting with the cartridge impedance and working backward toward Q1 gives:

```text
Z0      = Ztip + ZC10..C11
Z1      = parallel(Z0, ZL5)
Z2      = Z1 + ZL4
Z3      = parallel(Z2, ZC9)
Z4      = Z3 + ZL3
Z5      = parallel(Z4, ZC6..C8)
Z6      = Z5 + ZL2
Zbranch = Z6 + ZC2..C5
Zdrain  = parallel(Zbranch, ZC1, ZL1)
```

`Zbranch` is the load branch connected to the drain. `Zdrain` additionally
includes the class-E shunt capacitor and the finite fundamental-frequency
impedance of the DC-feed choke. Q1 itself and its nonlinear output capacitance
are not part of this linear calculation.

## Reactance of Every Element at 470 kHz

| Element | Value | Reactance at 470 kHz |
| --- | ---: | ---: |
| L1 | 33 µH | `+j97.452 ohms` |
| C1 | 15 nF | `-j22.575 ohms` |
| C2–C5 | 81 nF | `-j4.181 ohms` |
| L2 | 6.2 µH | `+j18.309 ohms` |
| C6–C8 | 30 nF | `-j11.288 ohms` |
| L3 | 16.9 µH | `+j49.907 ohms` |
| C9 | 6.8 nF | `-j49.798 ohms` |
| L4 | 16.9 µH | `+j49.907 ohms` |
| L5 | 11.5 µH | `+j33.961 ohms` |
| C10–C11 | 7.8 nF | `-j43.414 ohms` |

Several things are immediately visible:

- C2–C5 has only 4.18 ohms of reactance, so it behaves mostly as a DC-blocking
  capacitor while still contributing some series tuning.
- L1 has much more reactance than the matching branch, allowing it to supply DC
  without carrying most of the RF current. Its finite 97.5 ohm reactance is not
  infinite, however, so it is included in the final fundamental calculation.
- L3, C9, and L4 all have almost exactly 50 ohms of reactance magnitude. This is
  deliberate and identifies the middle block as an impedance inverter.

## The L3-C9-L4 Immittance Converter

The resonance calculation for L3 or L4 with C9 is:

```text
f = 1/(2*pi*sqrt(16.9e-6 * 6.8e-9))
  = 469.49 kHz
```

That is within about 0.1% of the 470 kHz carrier. More importantly, the network
is a symmetrical series-L, shunt-C, series-L T-network:

```text
input -- jK --+-- jK -- output
              |
             -jK
              |
             GND
```

At its design frequency:

```text
K = omega*L = 1/(omega*C) = about 49.85 ohms
```

For exactly equal reactance magnitudes, its two-port ABCD matrix becomes:

```text
[ A  B ]   [  0    jK ]
[ C  D ] = [ j/K    0 ]
```

The input impedance is therefore:

```text
Zin = K^2/Zload
```

This is why the block is called an *immittance converter*: it converts an
impedance into a scaled admittance. A high impedance becomes low, a low
impedance becomes high, and the sign of a load's reactance is reversed by the
complex reciprocal.

The actual component values give `XL = 49.907 ohms` and
`abs(XC) = 49.798 ohms`, so the converter is extremely close to the ideal
`K = 50 ohms` case. It is the lumped-element equivalent of a quarter-wave
50 ohm transmission-line transformer, without needing a physically enormous
quarter-wave cable at 470 kHz.

## The Other Sections

The remaining LC combinations do not all resonate independently at 470 kHz:

| Pair considered by itself | `sqrt(L/C)` impedance scale | Isolated LC frequency |
| --- | ---: | ---: |
| L2 and C6–C8 | 14.38 ohms | 369.0 kHz |
| L3 or L4 and C9 | 49.85 ohms | 469.5 kHz |
| L5 and C10–C11 | 38.40 ohms | 531.4 kHz |

Only the middle row is intentionally at the carrier because it is the
immittance inverter. L5 and C10–C11 form an output L-match around the cartridge
load. L2 and C6–C8 then transform the inverter output into the low-resistance,
inductive branch impedance required at a class-E drain. The complete result
depends on all sections and the connected cartridge; pairing adjacent parts and
calling each pair a separate resonator gives the wrong answer.

## Nominal 50 Ohm Example

No cold/warm/hot 470 kHz cartridge measurements are currently available in this
repository. A purely resistive 50 ohm load is used here as a nominal design
example because the system uses 50 ohm cable and connectors and because the
immittance converter itself has a 50 ohm inverter constant. This is a benchmark,
not a claim that a real cartridge measures exactly 50 ohms.

Starting at `Ztip = 50 ohms`:

| Point in calculation, moving from tip to Q1 | Impedance |
| --- | ---: |
| Nominal tip load | `50.000 + j0.000 ohms` |
| After series C10–C11, 7.8 nF | `50.000 - j43.414 ohms` |
| After parallel L5, 11.5 µH | `22.270 + j38.171 ohms` |
| After series L4, 16.9 µH | `22.270 + j88.078 ohms` |
| After parallel C9, 6.8 nF | `28.158 - j98.198 ohms` |
| After series L3, 16.9 µH | `28.158 - j48.291 ohms` |
| After parallel C6–C8, 30 nF | `0.826 - j9.540 ohms` |
| After series L2, 6.2 µH | `0.826 + j8.770 ohms` |
| After series C2–C5, 81 nF | `0.826 + j4.589 ohms` |
| After parallel C1, 15 nF | `1.299 + j5.700 ohms` |
| After also including L1, 33 µH | `1.159 + j5.400 ohms` |

The middle three rows can also be checked with the impedance-inverter shortcut.
The load presented to the right side of L4 is the output-match result:

```text
Zright = 22.270 + j38.171 ohms

Zleft = K^2/Zright
      = 49.85^2/(22.270 + j38.171)
      = approximately 28.16 - j48.29 ohms
```

That agrees with the explicit L4, C9, and L3 calculation.

The matching branch alone presents approximately
`0.826 + j4.589 ohms` to the drain. Its reactance-to-resistance ratio is about
5.56. This strongly inductive, high-Q fundamental load is intentional in a
class-E amplifier; it must not be judged as though Q1 were a 50 ohm sinusoidal
signal generator. C1, Q1's output capacitance, L1, the switching duty cycle, and
the harmonic impedances together determine whether the drain reaches the
required zero-voltage-switching condition.

## Sensitivity to Cartridge Resistance

The next table repeats the complete backward calculation for purely resistive
25, 50, and 100 ohm loads. These are sensitivity examples, not measured
temperature states.

| Point in calculation | 25 ohm load | 50 ohm load | 100 ohm load |
| --- | ---: | ---: | ---: |
| Tip load | `25.000 + j0.000` | `50.000 + j0.000` | `100.000 + j0.000` |
| After C10–C11 | `25.000 - j43.414` | `50.000 - j43.414` | `100.000 - j43.414` |
| After parallel L5 | `40.362 + j49.223` | `22.270 + j38.171` | `11.431 + j35.041` |
| After L4 | `40.362 + j99.130` | `22.270 + j88.078` | `11.431 + j84.949` |
| After parallel C9 | `24.637 - j79.910` | `28.158 - j98.198` | `20.749 - j113.600` |
| After L3 | `24.637 - j30.003` | `28.158 - j48.291` | `20.749 - j63.693` |
| After parallel C6–C8 | `1.358 - j9.012` | `0.826 - j9.540` | `0.437 - j9.709` |
| After L2 | `1.358 + j9.297` | `0.826 + j8.770` | `0.437 + j8.600` |
| Matching branch after C2–C5 | `1.358 + j5.117` | `0.826 + j4.589` | `0.437 + j4.419` |
| After parallel C1 | `2.257 + j6.441` | `1.299 + j5.700` | `0.675 + j5.479` |
| Including parallel L1 | `1.985 + j6.084` | `1.159 + j5.400` | `0.605 + j5.191` |

The impedance inverter is visible in the trend: increasing the cartridge
resistance ultimately decreases the real part presented to the drain. The
reactive sections prevent this from being a simple `2500/Rtip` relationship at
Q1, but the inverse relationship remains.

## What Is Needed for Cold, Warm, and Hot Calculations

The 13.56 MHz STTC-147 impedance measurements used in the other document cannot
be reused here. The Lite design uses different 470 kHz cartridge families, and
the impedance of a Curie-point heater is frequency dependent. Scaling only the
reactive part by frequency would also be invalid because the magnetic material's
permeability and losses change with both frequency and temperature.

The useful measurement set would contain, for each supported cartridge:

| State | Resistance at 470 kHz | Reactance at 470 kHz | Measurement condition |
| --- | ---: | ---: | --- |
| Cold | TBD | TBD | Room temperature |
| Warm | TBD | TBD | Below the Curie temperature while absorbing power |
| Hot | TBD | TBD | At or above the Curie temperature |

Measurements should be made with a VNA or impedance analyzer at the handpiece
input or cartridge terminals. The cable and fixture must either be included as
part of the intended load or de-embedded so the reference plane is stated
unambiguously. Once each complex value `R + jX` is known, it can be substituted
directly for `Ztip` in the recursion above.

## Disconnected and Shorted Loads

In the ideal lossless model, both a disconnected tip and a shorted output remove
the real part of the drain load:

| Fault condition | Matching branch at Q1 drain | Including C1 and L1 |
| --- | ---: | ---: |
| Short circuit | `j7.548 ohms` | `j10.158 ohms` |
| Open circuit | `j4.358 ohms` | `j5.118 ohms` |

With no resistive destination for the stored energy, the real circuit is then
limited by winding loss, capacitor ESR, MOSFET loss, arcing, and protection
devices. These ideal impedances do not predict the transient peak voltage, but
they explain why either fault can destroy the zero-voltage-switching condition
and produce dangerous drain-voltage excursions.

## Why This Network Has So Many Parts

A single L-match can transform one fixed impedance at one frequency, but this
network has several simultaneous jobs:

- Provide the inductive, high-Q load required for class-E switching.
- Block the DC drain voltage from the output connector.
- Convert impedance through the 50 ohm immittance inverter.
- Match the cartridge and cable to that inverter.
- Suppress carrier harmonics generated by Q1's switching waveform.
- Remain usable while the cartridge impedance changes around its Curie point.

Multiple parallel capacitors also divide RF current and permit values that are
not available as a single preferred-value capacitor. The cost is more loss,
more tolerance accumulation, and more possible parasitic resonances. The values
must therefore be evaluated as one loaded network, not as several isolated LC
resonators.

## Limits of This Calculation

This ideal 470 kHz calculation omits:

- Measured complex cartridge impedance versus temperature.
- Inductor winding resistance, core loss, tolerance, and DC-bias dependence.
- Capacitor ESR, ESL, tolerance, and voltage dependence.
- Q1's nonlinear output capacitance and switching transitions.
- Cable, SMA connector, handpiece, and PCB parasitics.
- Harmonic impedances at 940 kHz, 1.41 MHz, and above.
- Heating and saturation of the Kool Mu inductor cores.

The original RadioThermal project recommends checking the custom inductors with
a VNA or LCR meter. The completed amplifier should also be validated at reduced
supply voltage while observing Q1's drain waveform with an appropriately rated,
low-capacitance probe. Full-power operation should begin only after confirming
that the drain voltage returns close to zero before Q1 turns on.
