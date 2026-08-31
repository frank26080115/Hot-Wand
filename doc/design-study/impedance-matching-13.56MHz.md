# Hot-Wand 13.56 MHz Impedance Matching

## Circuit Topology

The main RF power stage is a class-E amplifier built around Q1. L3, a 9 µH
inductor, feeds DC from the main power supply to the drain of Q1 while acting as
an RF choke. C9 through C12 are four parallel 100 nF capacitors, giving a total
coupling capacitance of 400 nF between the drain and the matching network.

The matching network is a three-section low-pass ladder:

```text
Q1 drain -- C9..C12 -- L4 -----+-- L5 -----+-- L6 -----+-- T1 primary -- RF output -- tip
             400 nF    180 nH  |   400 nH  |   540 nH  |
                               |           |           |
                               C13..C18    C20..C26    C27..C31
                                600 pF      382 pF      235 pF
                                   |           |           |
                                  GND         GND         GND
```

The fitted values are:

| Element | Components | Total value | Function |
| --- | --- | ---: | --- |
| Drain choke | L3 | 9 µH | Supplies DC to Q1 and presents a high impedance to RF |
| Coupling capacitor | C9–C12 | 400 nF | Blocks DC between Q1 and the matching network |
| Section 1 series inductor | L4 | 180 nH | First series element |
| Section 1 shunt capacitor | C13–C18 | 600 pF | Six 100 pF capacitors in parallel |
| Section 2 series inductor | L5 | 400 nH | Second series element |
| Section 2 shunt capacitor | C20–C26 | 382 pF | Six 47 pF capacitors plus one 100 pF capacitor |
| Section 3 series inductor | L6 | 540 nH | Third series element |
| Section 3 shunt capacitor | C27–C31 | 235 pF | Five 47 pF capacitors in parallel |

C19 and C32 are unpopulated tuning positions and are not included in these
totals.

After the third section, the RF current passes through the one-turn primary of
current transformer T1 and then reaches the output connector and iron tip.

## Calculation Method

This first-order calculation treats every fitted inductor and capacitor as an
ideal component at exactly 13.56 MHz. The angular frequency is:

```text
f = 13.56e6 Hz
omega = 2*pi*f = 85.200e6 rad/s
```

The impedance of an inductor is:

```text
ZL = j*omega*L
```

The impedance of a capacitor is:

```text
ZC = 1/(j*omega*C) = -j/(omega*C)
```

For a shunt capacitor, the impedance seen looking into that node is:

```text
parallel(Zbranch, ZC) = 1/(1/Zbranch + 1/ZC)
```

The network is most easily calculated backward, starting at the tip and moving
toward Q1. Define `LCT` as an assumed effective series inductance for the current
transformer primary:

```text
Z0     = Ztip + j*omega*LCT
Z1     = parallel(Z0, ZC27..C31)
Z2     = j*omega*L6 + Z1
Z3     = parallel(Z2, ZC20..C26)
Z4     = j*omega*L5 + Z3
Z5     = parallel(Z4, ZC13..C18)
Z6     = j*omega*L4 + Z5
Zdrain = ZC9..C12 + Z6
```

The detailed tables below initially use `LCT = 30 nH` as a sensitivity
assumption. This should not be mistaken for a measured value; the current
transformer is discussed separately below.

## Reactance of Every Element at 13.56 MHz

| Element | Value | Reactance at 13.56 MHz |
| --- | ---: | ---: |
| L3 | 9 µH | `+j766.8 ohms` |
| C9–C12 | 400 nF | `-j0.0293 ohms` |
| L4 | 180 nH | `+j15.336 ohms` |
| C13–C18 | 600 pF | `-j19.562 ohms` |
| L5 | 400 nH | `+j34.080 ohms` |
| C20–C26 | 382 pF | `-j30.725 ohms` |
| L6 | 540 nH | `+j46.008 ohms` |
| C27–C31 | 235 pF | `-j49.945 ohms` |
| Assumed current-transformer contribution | 30 nH | `+j2.556 ohms` |

The 400 nF coupling bank has essentially no effect on the fundamental-frequency
impedance: its reactance is only about 0.03 ohms. Its important job is blocking
the drain's DC voltage.

L3 is also not one of the matching ladder's series elements. The bypassed DC
supply is approximately RF ground, so L3 appears as a high-impedance shunt path
at the drain. Its approximately 767 ohm reactance is much larger than the
roughly 10 ohm load presented by the matching network. The linear calculations
below therefore omit L3. L3, Q1's output capacitance, and the switching waveform
must instead be included in a complete class-E analysis.

## What the Three Sections Are Doing

It is tempting to treat each inductor and the capacitor immediately after it as
an independent resonant circuit. That is not quite correct because each section
is loaded by every section and by the tip to its right. Two useful numbers can
still be calculated for each nominal LC pair:

```text
impedance scale = sqrt(L/C)
isolated LC frequency = 1/(2*pi*sqrt(L*C))
```

| Section | L | C | `sqrt(L/C)` | Isolated LC frequency |
| --- | ---: | ---: | ---: | ---: |
| L4 / C13–C18 | 180 nH | 600 pF | 17.32 ohms | 15.31 MHz |
| L5 / C20–C26 | 400 nH | 382 pF | 32.36 ohms | 12.88 MHz |
| L6 / C27–C31 | 540 nH | 235 pF | 47.94 ohms | 14.13 MHz |

The impedance scale rises from about 17 ohms at the transistor end to about
48 ohms at the tip end. This is the signature of a tapered impedance-transforming
ladder: it transforms a tip impedance in the neighborhood of 50 ohms into a much
lower drain load. The network also acts as a low-pass harmonic filter. The fact
that the three isolated LC frequencies surround 13.56 MHz does not mean that
each pair resonates separately at 13.56 MHz; only the loaded ladder as a whole
has the behavior calculated below.

### Why Three Sections Instead of One?

One section would be enough to match two fixed, purely resistive impedances at
one exact frequency. For example, suppose Q1 should see 12 ohms and the tip is
an ideal 50 ohm load. A low-pass L-match with a series inductor followed by a
shunt capacitor can be calculated from:

```text
Qmatch = sqrt(Rload/Rsource - 1)
       = sqrt(50/12 - 1)
       = 1.780

Xseries = Qmatch*Rsource = 21.35 ohms
Xshunt  = Rload/Qmatch   = 28.10 ohms

Lseries = Xseries/omega    = 251 nH
Cshunt  = 1/(omega*Xshunt) = 418 pF
```

At 13.56 MHz, an ideal 251 nH series inductor and 418 pF shunt capacitor would
therefore transform exactly 50 ohms into exactly 12 ohms. Three sections are not
required merely because the impedance ratio is approximately four to one.

The limitation is that the one-section network has only two adjustable
reactances. Both are consumed by the requirement to cancel the input reactance
and obtain the desired resistance at one frequency and one load. There is no
remaining freedom to shape the response at harmonics or at the other tip
impedances.

The actual six-reactance ladder gives the designer several additional benefits:

- **A gradual impedance transformation.** The nominal `sqrt(L/C)` scales step
  from 17.3 to 32.4 to 47.9 ohms rather than making the entire transformation
  at one node. This is analogous to a stepped transmission-line transformer.
- **A much steeper low-pass response.** A single LC section is second order and
  ultimately rolls off at about 40 dB per decade. Three ideal LC sections form
  a sixth-order ladder that can approach 120 dB per decade far into its
  stopband. This matters because a switching class-E drain waveform contains
  strong harmonics at 27.12 MHz, 40.68 MHz, and above. The fundamental should
  reach the tip, while harmonic current should be kept out of the output cable
  and load.
- **More control over changing tip impedance.** The tip is not a fixed 50 ohm
  resistor. It moves from inductive to capacitive and then strongly inductive as
  it heats. Additional sections allow the cold and warm cases to present useful
  loads to Q1 while preserving a large mismatch and phase change in the hot
  state.
- **Distributed reactive energy and component stress.** RF voltage and current
  transformation occurs over several nodes and components instead of being
  concentrated in one inductor and one capacitor. The parallel capacitor banks
  also divide RF current among several physical capacitors.

A numerical comparison illustrates the load-variation advantage. The following
uses ideal components and omits T1 so that both networks use the same load
reference plane. The hypothetical one-section network is the 251 nH / 418 pF
match calculated above; it is not an optimized replacement for the actual
network.

| Tip state | Hypothetical one-section input | Actual three-section input |
| --- | ---: | ---: |
| Cold | `16.555 - j0.835 ohms` | `11.775 + j0.770 ohms` |
| Warm | `8.737 + j0.262 ohms` | `10.434 - j6.381 ohms` |
| Hot | `58.920 + j13.376 ohms` | `4.231 + j7.484 ohms` |

The one-section example provides its exact 12 ohm match only for the ideal
50 ohm load used to design it. Its transformed resistance changes greatly for
the measured tip states. The actual ladder keeps the cold and warm input
resistances in a narrower range while making the hot state distinctly reactive.
That is useful for both class-E operation and the current-transformer phase
detector.

The cost is more parts, more loss, more parasitic resonances, and potentially
greater sensitivity to component tolerances. Three sections are therefore not
automatically better than one. They are worthwhile when the harmonic filtering
and load-response shaping are requirements, as they appear to be here.

Without the original designer's synthesis notes, these calculations cannot
prove which requirement determined every component value. They do show that
the three sections are doing more than a one-frequency impedance match: this is
also a harmonic filter and a deliberately shaped transformation for a load that
changes with temperature.

## Measured Tip Loads

The tip measurements came from a third party's measurements of an STTC-147 tip,
not from the designer of this matching network. The original source is
[Random Fun Projects](http://randomfunprojects.co.uk/metcal.html).

The equivalent reactive component is calculated from:

```text
inductive:  L = X/omega
capacitive: C = 1/(omega*abs(X))
```

For comparison against a 50 ohm reference impedance:

```text
Gamma = (Ztip - 50)/(Ztip + 50)
SWR = (1 + abs(Gamma))/(1 - abs(Gamma))
return loss = -20*log10(abs(Gamma))
```

| Tip state | Measured impedance | Equivalent component | Reported SWR | Reported S11 | SWR calculated from impedance | Return loss calculated from impedance |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Cold | `42.3 + j13 ohms` | 153 nH | 1.4 | -16 dB | 1.39 | 15.80 dB |
| Warm, below Curie temperature | `55 - j16 ohms` | 734 pF | 1.1 | -23 dB | 1.37 | 16.04 dB |
| Hot, above Curie temperature | `12 + j24 ohms` | 282 nH | 5.1 | -3.4 dB | 5.17 | 3.40 dB |

The reported cold and hot values agree with values recalculated from their
complex impedances after rounding. The reported warm-state `1.1` SWR and
`-23 dB` S11 do not agree with `55 - j16 ohms`; that impedance gives about
`1.37` SWR and `-16.04 dB` return loss. The impedance, SWR, and S11 may have
been captured at slightly different temperatures or reference planes, or one
of the reported warm-state numbers may be a transcription error. The matching
calculations below use the complex impedance because it contains both the
resistive and reactive information needed by the circuit model.

## Stage-by-Stage Impedance Transformation

The following table applies the backward recursion above. It assumes ideal
components and models the current transformer as a 30 nH series inductance. Each
entry is the impedance looking to the right from the named point.

| Point in calculation, moving from tip to Q1 | Cold | Warm | Hot |
| --- | ---: | ---: | ---: |
| Measured tip load | `42.300 + j13.000` | `55.000 - j16.000` | `12.000 + j24.000` |
| After adding assumed 30 nH T1 primary | `42.300 + j15.556` | `55.000 - j13.444` | `12.000 + j26.556` |
| After shunt C27–C31, 235 pF | `35.505 - j21.080` | `19.480 - j27.494` | `43.317 + j34.484` |
| After series L6, 540 nH | `35.505 + j24.928` | `19.480 + j18.514` | `43.317 + j80.492` |
| After shunt C20–C26, 382 pF | `25.899 - j26.497` | `34.791 - j8.915` | `9.394 - j41.518` |
| After series L5, 400 nH | `25.899 + j7.583` | `34.791 + j25.165` | `9.394 - j7.438` |
| After shunt C13–C18, 600 pF | `12.172 - j13.932` | `10.721 - j21.288` | `4.399 - j6.920` |
| After series L4, 180 nH | `12.172 + j1.404` | `10.721 - j5.952` | `4.399 + j8.416` |
| At Q1 drain after 400 nF coupling bank | `12.172 + j1.374` | `10.721 - j5.982` | `4.399 + j8.387` |

For example, the first transformation of the cold-tip impedance is:

```text
Ztip+T1 = 42.3 + j*(13 + 2.556)
         = 42.3 + j15.556 ohms

Z235pF  = -j49.945 ohms

Zafter  = parallel(42.3 + j15.556, -j49.945)
         = 35.505 - j21.080 ohms
```

Every other shunt-capacitor row uses the same parallel-impedance calculation;
every series-inductor row simply adds that inductor's positive reactance.

## Correlation with Tip Temperature

The final drain-facing results are:

| Tip state | Load presented to Q1 matching-network port | Magnitude | Phase angle | Fundamental power factor, `R/abs(Z)` |
| --- | ---: | ---: | ---: | ---: |
| Cold | `12.172 + j1.374 ohms` | 12.249 ohms | +6.44 degrees | 0.994 |
| Warm | `10.721 - j5.982 ohms` | 12.277 ohms | -29.16 degrees | 0.873 |
| Hot | `4.399 + j8.387 ohms` | 9.471 ohms | +62.33 degrees | 0.465 |

The cold tip is transformed to an almost purely resistive load of about
12.2 ohms. The warm tip still presents a similar impedance magnitude, but its
input is moderately capacitive. Above the Curie temperature, the transformed
load becomes strongly inductive and its resistive component falls to about
4.4 ohms. Thus the network does not attempt to make all three temperature states
look identical to Q1. It provides a good fundamental-frequency load while the
tip is demanding heat and deliberately preserves a large phase change when the
hot tip changes impedance.

That large phase change also explains why the output current transformer can be
used as a power-factor detector. At the tip, current lags voltage by about
17 degrees in the cold state, leads voltage by about 16 degrees in the warm
state, and lags voltage by about 63 degrees in the hot state. The detector senses
this output voltage/current phase relationship and uses it to influence the buck
converter and reduce RF power.

As an illustration only, if the matching network were driven by a fixed RMS
sinusoidal voltage, real power would be proportional to the real part of its
input admittance, `real(1/Zdrain)`:

| Tip state | Input conductance | Relative to cold |
| --- | ---: | ---: |
| Cold | 81.1 mS | 100% |
| Warm | 71.1 mS | 87.7% |
| Hot | 49.0 mS | 60.5% |

Q1 is a switching class-E amplifier rather than an ideal sinusoidal voltage
source, so these percentages are not predictions of actual heater power. They
only show the direction of the change in the linear fundamental-frequency
model. The class-E waveform and the buck converter's power-factor feedback make
the real behavior more nonlinear.

## Current Transformer Uncertainty

The T1 primary is one conductor passing once through the Fair-Rite 5961004901
core. The core's published low-signal inductance factor is
[`A_L = 80 nH/turn^2`, with ±25% tolerance](https://fair-rite.com/printer_friendly_datasheet.php?part=5961004901).
An unloaded one-turn winding would therefore have a nominal magnetizing
inductance near 80 nH, not 30 nH (as if it was using the K16 core).

However, using 80 nH directly as a series inductance is not an accurate current-
transformer model. Magnetizing inductance, leakage inductance, the two
secondaries, the diode network, and impedances reflected from the secondaries
all contribute differently. The detector is also nonlinear because its diodes
conduct during only part of each RF cycle. A measurement or a complete coupled-
inductor model is required to determine the primary's effective insertion
impedance.

The sensitivity of the final result to three hypothetical series-inductance
values is:

| Assumed T1 series inductance | Cold drain load | Warm drain load | Hot drain load |
| ---: | ---: | ---: | ---: |
| 0 nH | `11.775 + j0.770` | `10.434 - j6.381` | `4.231 + j7.484` |
| 30 nH | `12.172 + j1.374` | `10.721 - j5.982` | `4.399 + j8.387` |
| 80 nH | `12.878 + j2.419` | `11.226 - j5.298` | `4.701 + j9.973` |

The conclusion is unchanged over this range: cold is close to resistive, warm
is capacitive, and hot is strongly inductive. For accurate component stress or
class-E switching calculations, T1 must not be reduced to this simple series
inductor approximation.

## Limits of This Calculation

This calculation explains the nominal 13.56 MHz impedance transformation, but
it is not a substitute for measurement or a switching simulation. A complete
model must include:

- Inductor tolerance, winding resistance, core loss, and self-capacitance.
- Capacitor tolerance, ESR, ESL, and voltage dependence.
- PCB trace, connector, and cable inductance.
- Q1's nonlinear output capacitance and the finite impedance of L3.
- The class-E drain waveform and harmonic impedances, not only its fundamental.
- The current transformer's coupled windings and nonlinear detector load.
- The exact VNA reference plane used for the tip measurements.

The most useful verification is to measure the assembled network with a VNA at
the actual tip connector and compare it with a SPICE model using measured
inductor and transformer parameters. Drain-voltage waveform verification must
then be performed at reduced supply voltage before operating at full power.
