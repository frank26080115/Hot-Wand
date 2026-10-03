# 13.56 MHz board commanded to approximately 470 kHz

2026-10-01. Screening answer for using the original 13.56 MHz output network
with a 470 kHz cartridge. This is a comparison of circuit behavior, not an
approved conversion design.

The STM32 timer runs from 27.12 MHz. A 58-count period gives 467,586 Hz,
with 29 counts HIGH and 29 LOW at nominal 50% duty. The current firmware uses
two counts per cycle, so a source change and reflash are required.

The gate-driving amplifier is separate from the output matching network.
Directly driving Q1 from U2 at 470 kHz is plausible after isolating Q2/L8 and
connecting both U2 outputs through their individual 3.6 ohm resistors to the
Q1 gate. This rewiring has not been laid out or tested. The original Q1 gate
clamp and discharge network need to be retained and the driver return kept
close to Q1 source. This bypass does not remove the output matching network.

Original output path from [Eagle schematic](../../../electrical/hot-wand.sch):

```text
9 uH supply choke -> Q1 drain -> 400 nF series bank -> 180 nH L4
  -> 600 pF shunt bank -> 400 nH L5 -> 382 pF shunt bank
  -> 540 nH L6 -> 235 pF output shunt bank -> cartridge
```

At 467.6 kHz the three shunt bank reactances are about 567, 891 and
1450 ohms, while the three series inductors have reactances about 0.53,
1.18 and 1.59 ohms. The passive network therefore has a qualitatively
different behavior from its 13.56 MHz design. Frequency scaling alone would
suggest L4/L5/L6 around 5.2/11.5/15.6 uH and shunt banks around
17.3/11.0/6.8 nF to preserve *each component's reactance*. It would also
require roughly 11.5 uF for the 400 nF series bank and 260 uH for the
9 uH choke. Those values are **not a valid recipe**: the real 470 kHz tip
has a different load impedance, large inductors require different cores and
wire, and an 11.5 uF RF coupling bank is not feasible on four 0805 footprints.
In particular, the 400 nF bank already has only about 0.85 ohm reactance at
470 kHz; keeping it or selecting another physically fitted value is possible
if the whole ladder is designed for the actual 470 kHz load. The 11.5 uF
scaling number is not a conversion requirement.
The Lite board uses a different topology with a drain shunt capacitor and an
output shunt inductor, which cannot be reproduced by merely replacing the
13.56 MHz board's existing passive values.

The 13.56 MHz board's three-series-inductor, three-shunt-capacitor ladder can
in principle be designed as a distinct 470 kHz matching network. It has
several component values to choose and does not need to copy the Lite topology.
The new values must meet *both* a useful cold/warm-to-hot power contrast and
acceptable Q1 switching over all cartridge states. The existing T130-6 coils
have 4, 6 and 7 turns. Raising inductance into several uH with the same
core would require many more turns (approximately 22, 32 and 38 for example
5.2, 11.5 and 15.6 uH at about 11 nH/turn squared). This raises winding-fill,
RF loss and mounting questions; alternative cores/wire may fit the existing
large footprints more naturally. The multiple 0805 shunt-bank pads can accept
different nF-rated parts, subject to voltage, current and dielectric limits.
An added, low-inductance drain shunt capacitor may be needed for Class-E
timing; this board has no convenient dedicated bank for that function.

## Simplified warning simulation

[at-470khz.py](at-470khz.py) generates [the netlist](at-470khz.cir) and
[measurement log](at-470khz.log). It models the unmodified 13.56 MHz output
network at 467.6 kHz with direct, idealized 0-12 V Q1 gate drive and 20 V
**at the RF stage**, which is not necessarily the USB input voltage. It uses
the inherited 470 kHz STP-CN04 cold/warm/hot impedance states, **not** measured
K75C002 impedances. It omits the drain TVS, avalanche, the actual main buck,
core loss and saturation, and thermal feedback. The unclamped drain voltages
above the nominal 200 V transistor rating invalidate literal hardware
predictions; they are evidence of a severe mismatch to investigate.

| Load state | Modeled tip W | Final 40-cycle drain peak | Drain at gate rising through 3 V |
| --- | ---: | ---: | ---: |
| Cold | 14.3 | 401.5 V | -0.8 V |
| Below Curie | 12.1 | 414.3 V | 28.8 V |
| Above Curie | 15.8 | 566.6 V | -0.4 V |

In this particular model the hot cartridge receives *more* power than the
cold one; it loses the desired self-regulating power relationship. The actual
K75C002 can behave differently. The capacitor, inductor and gate changes
must be designed together using the measured cartridge impedances and
verified Q1 gate/drain waveforms, including startup and open-load behavior.

The large existing inductor footprints and capacitor banks offer room for
some substitutions, but a full 470 kHz topology may need a daughterboard with
short, rated RF connections. The main buck and cartridge detector must also
be checked at the new frequency. This is effectively a new 470 kHz RF stage
using the 13.56 MHz board's control and supply sections.

## Reusing measurement and regulation

Firmware reads DC input voltage, buck output voltage and buck current. Its
attenuation PWM modifies buck feedback and can cap the RF-stage supply
voltage/current/power after recalibration under the new load. These are DC
quantities: they do not directly measure output RF real power, phase or tip
temperature. Lowering the buck voltage alone tends to reduce cold and hot
heating together; the passive network supplies the desired discrimination.

The output current transformer and diode-ring detector are a potentially
valuable extra feedback path, but should be considered a second retuning
project. The [current-transformer study](../current-transformer.md) identifies
a 1:14:14 transformer and a 10 pF voltage-sensing capacitor (C33 on this
board). At 470 kHz the
10 pF reactance is about 34 kilohms versus 1.17 kilohms at 13.56 MHz, so
its injected RF current is roughly 29 times lower at equal voltage. The
specified Fair-Rite 5961004901 core is about 80 nH/turn squared, giving
approximately 15.7 uH for 14 turns; its magnetizing reactance falls from
about 1.34 kilohms to 46 ohms. Burden, diode thresholds, core behavior and
phase error all require new validation. Changing the capacitor alone cannot
guarantee that the detector functions as intended. It feeds the buck directly,
so an incorrectly signed response could increase voltage when the tip gets hot.
Keep this analog feedback isolated during first RF-stage tuning, then verify
the sign and strength at cold, warming and hot states before enabling it.
