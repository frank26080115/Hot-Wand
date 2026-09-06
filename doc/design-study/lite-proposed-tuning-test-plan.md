# Hot-Wand Lite Proposed Tuning Test Plan

## Purpose

This plan uses only a synchronized four-channel oscilloscope to:

1. make a simple pass/fail decision on the 470 kHz power stage; and
2. try to improve its performance by adding capacitors in parallel with the
   existing capacitor banks.

The main load is a cold K-family cartridge. A second test is made after the
cartridge becomes hot. Cold is expected to be the more stressful load, but that
is an assumption until both states have been measured: a temperature-dependent
impedance change can make the hot state worse for zero-voltage switching even
when its current is lower.

This is a proposed development procedure, not a production certification. It
does not replace the voltage, current, pulse-energy, safe-operating-area, and
thermal limits in the data sheets for the parts actually fitted.

The calculations and nominal network behavior are described in
[`impedance-matching-470kHz.md`](impedance-matching-470kHz.md).

## Relevant Schematic Nets

The renamed nets in `electrical/hot-wand-lite.sch` make the RF chain:

```text
Q1 drain / MOSFET-DRAIN
  -> C2..C5 -> M1 -> L2 -> M2 -> L3 -> M3
  -> L4 -> M4 -> C10..C11 -> RF-OUT -> cartridge
  -> RF-RETURN -> SJ2||SJ3||SJ4||SJ5 -> GND

Q1 source -> MOSFET-SOURCE -> SJ1 -> GND
```

C6 through C8 shunt `M2` to ground, C9 shunts `M3` to ground, and L5
shunts `M4` to ground. All four output-return jumpers are connected across the
same two nodes; they are one four-resistor shunt bank, not four independent
sense points.

## Equipment and Preconditions

- A four-channel oscilloscope with waveform math, saved references, and CSV
  export if possible.
- One probe suitable for the full Q1 drain transient voltage. A normal low-
  voltage 10x probe is not automatically suitable.
- Three other compensated probes. A low-noise 1x probe may be useful on the
  two shunts if its bandwidth and capacitance are acceptable.
- Short probe ground springs. Do not use long alligator ground leads around the
  RF stage.
- A current-limited DC source is strongly preferred. Start at the lowest input
  voltage at which the 12 V gate-driver rail regulates correctly.
- The K-family cartridge and the exact handpiece/cable that will be used in
  service.
- D3/D4 and all other intended protection components fitted before testing.
- The actual Q1 part number and its data sheet recorded in the test log. The
  project shopping list suggests IRF640NPBF, but the schematic intentionally
  calls Q1 only `NCHAN` and permits alternatives.

Never defeat the oscilloscope protective-earth connection. All ground clips
from both oscilloscopes are normally common through protective earth. Connect
every ground clip only to circuit `GND`. In particular:

- do not ground `MOSFET-SOURCE`, because that bypasses SJ1;
- do not ground `RF-RETURN`, because that bypasses SJ2 through SJ5;
- do not ground `RF-OUT`, `M1` through `M4`, `MOSFET-DRAIN`, or Q1's heatsink;
  and
- confirm that grounding circuit `GND` does not short a non-isolated power
  source or another connected instrument.

Always connect the cartridge before enabling RF. Never use an open output as a
tuning test. Power down and discharge the board before moving probes or adding
a capacitor.

## Measurement Resistors

### SJ1: Q1 source-current shunt

Populate SJ1 with a low-inductance `0.010 ohm`, 1% current-sense resistor as a
starting value:

```text
IQ1_source = V(MOSFET-SOURCE) / 0.010 ohm
           = 100 A/V * V(MOSFET-SOURCE)

10 mV = 1 A
50 mV = 5 A
```

Use a pulse-rated part whose power rating is adequate for the planned burst:

```text
PSJ1 = IQ1_source_rms^2 * 0.010 ohm
```

SJ1 measures MOSFET source current, which includes drain current plus gate and
parasitic displacement currents. It is a useful switching-stress measurement,
but it is not the same as the 470 kHz load current. Its resistance and
inductance also alter Q1's effective gate-to-source drive, so SJ1 should be
shorted again after development unless it is intentionally part of the final
design.

### SJ2 through SJ5: cartridge-current shunt

Populate SJ2, SJ3, SJ4, and SJ5 with four equal low-inductance `1.00 ohm`, 1%
resistors. In parallel they give:

```text
RRF_shunt = 1.00 ohm / 4 = 0.250 ohm
Icartridge = V(RF-RETURN) / 0.250 ohm
           = 4 A/V * V(RF-RETURN)

250 mV = 1 A
500 mV = 2 A
```

For matched resistors, each resistor dissipates:

```text
Peach = Icartridge_rms^2 / 16
```

At `2 A RMS`, each 1 ohm resistor dissipates `0.25 W`. Use parts with adequate
continuous and pulse ratings, and stop before exceeding either their rating or
the board's thermal capability. Do not fit a zero-ohm link on any one of these
four footprints; one link bypasses the entire measurement bank.

Measure the actual installed shunt resistances if suitable low-resistance test
equipment is available. Otherwise, use the nominal values for approximate
current and treat scope-derived power as comparative rather than calibrated.

## Oscilloscope Preparation

1. Compensate all probes.
2. Set the attenuation and voltage rating correctly in the scope for every
   channel.
3. Connect the probes to the same low-voltage square wave and deskew them. At
   470 kHz, one degree is only about `5.91 ns`.
4. Use at least `100 MS/s` when available. Capture several complete carrier
   cycles away from the beginning or end of a power-control burst.
5. Use full bandwidth for drain overshoot and switching-edge checks. A 20 MHz
   bandwidth limit or high-resolution acquisition can be used separately for
   fundamental phase and shunt-amplitude measurements.
6. Trigger on the rising edge of `FET-GATE` and save the baseline setup and
   waveform as a scope reference.

Do not combine timing or phase measurements from the separate two-channel
scope with the four-channel scope. The second scope may monitor slow or
independent signals such as the input rail and 12 V gate-driver rail, but its
waveforms are not synchronized closely enough for RF phase or switching-loss
calculations.

## Four-Channel Probe Configurations

Two acquisitions are used because the synchronized scope has four channels.
Do not move probes while the board is powered.

### Configuration A: switching safety

| Channel | Probe tip | Derived quantity |
| --- | --- | --- |
| CH1 | `FET-GATE` | Gate voltage relative to ground |
| CH2 | `MOSFET-SOURCE` | `IQ1_source = CH2 / RSJ1` |
| CH3 | `MOSFET-DRAIN` | Drain voltage relative to ground |
| CH4 | `RF-RETURN` | `Icartridge = CH4 / RRF_shunt` |

Use scope math or exported samples:

```text
VGS = CH1 - CH2
VDS = CH3 - CH2
IQ1_source = CH2 / 0.010
Icartridge = CH4 / 0.250
```

This configuration establishes Q1 gate margin, turn-on drain voltage, drain
peak, Q1 current, and output current on the same timebase.

### Configuration B: load and delivered power

| Channel | Probe tip | Purpose |
| --- | --- | --- |
| CH1 | `FET-GATE` | Trigger and switching reference |
| CH2 | `MOSFET-DRAIN` | Drain peak and approximate ZVS check |
| CH3 | `RF-OUT` | Output voltage relative to ground |
| CH4 | `RF-RETURN` | Output-current shunt voltage |

Calculate:

```text
Vcartridge = CH3 - CH4
Icartridge = CH4 / 0.250
Pcartridge = mean(Vcartridge * Icartridge)
```

`CH3 - CH4` is essential: CH3 alone includes the voltage across the return
shunt. If the scope cannot subtract, multiply, and average simultaneously,
save all four channels to CSV and perform the arithmetic afterward.

For the fundamental impedance:

```text
|Zcartridge| = Vcartridge_rms / Icartridge_rms
theta = phase(Vcartridge) - phase(Icartridge)
Rcartridge = |Zcartridge| * cos(theta)
Xcartridge = |Zcartridge| * sin(theta)
```

Positive `theta` means current lags and the load is inductive. Negative `theta`
means current leads and the load is capacitive. Use a fundamental sinusoidal
fit or FFT phase, not the switching edges.

Changing an upstream capacitor does not force this cartridge phase toward
zero. The output phase identifies the cartridge/cable impedance; Q1's drain
waveform and delivered power determine whether the amplifier is well tuned.

# Test Plan 1: Simple Pass/Fail

## Pass/fail limits to record before power-up

Fill in these limits from the fitted parts and probes:

| Limit | Test value |
| --- | ---: |
| Fitted Q1 | __________ |
| Q1 absolute `VDS` rating | __________ V |
| Test `VDS` stop limit: no more than 80% of Q1 rating | __________ V |
| Drain probe peak rating | __________ V |
| Lower of Q1 and probe stop limits | __________ V |
| Q1 absolute `VGS` rating | __________ V |
| SJ1 RMS/pulse power limit | __________ W |
| Each SJ2-SJ5 RMS/pulse power limit | __________ W |
| Selected DC input-current limit | __________ A |

For an IRF640NPBF, the manufacturer specifies a `200 V` drain-to-source
breakdown rating and an absolute `+/-20 V` gate-to-source limit. This plan uses
`160 V` and `+/-15 V` as conservative development stop limits for that part.
Use different limits if a different Q1 is fitted. D4 is an SM15T150CA TVS with
a `128 V` maximum stand-off rating; repeated flat-topping or visible TVS
conduction is a failed tuning condition, not normal operation.

## Immediate stop conditions

Turn RF off immediately if any of the following occurs:

- the DC source enters current limit unexpectedly;
- `VDS` reaches the recorded stop limit or the drain probe rating;
- `VGS` exceeds the recorded limit, shows sustained negative drive, or does
  not return close to zero while Q1 should be off;
- the drain waveform clips repeatedly against the TVS or shows increasing
  cycle-to-cycle ringing;
- Q1 current exceeds the chosen device/SOA or SJ1 pulse limit;
- either shunt exceeds its calculated pulse or RMS power limit;
- waveforms drift rapidly at unchanged settings; or
- there is arcing, odor, smoke, unexpected sound, or abnormal heating.

## Cold-cartridge procedure

1. Let the K-family cartridge, handpiece, board, inductors, and Q1 return to
   room temperature. Record the cartridge and cable identifiers.
2. Connect the cartridge before power. Select the lowest firmware power setting
   that still provides clean groups of 470 kHz cycles.
3. Use Configuration A. Set a low DC current limit and begin at the lowest
   input voltage that gives a regulated gate-driver rail.
4. Capture the first stable carrier cycles inside a short burst. Ignore the
   start/stop transient only for phase and RMS calculations; its peak voltage
   must still remain below the stop limit.
5. Increase input voltage or power one small step at a time. At every step,
   save `VGS`, `VDS`, Q1 source current, and cartridge current.
6. Stop at the intended normal operating point, nominally 20 V for this
   project. Do not proceed to 24 V or 28 V until 20 V passes with margin.
7. Power down, discharge the board, and change to Configuration B. Repeat the
   same operating points and record output impedance and real power.
8. Repeat each important capture three times. A marginal result that changes
   materially between nominally identical cold starts is a failure.

## Cold pass criteria

The cold test passes only if all of the following are true at the intended
operating point:

| Item | Pass criterion |
| --- | --- |
| Carrier | Stable `470 kHz`, within `+/-1%` (`465.3` to `474.7 kHz`) |
| `VGS` high | Approximately 10 to 13.5 V for the 12 V driver, measured gate-to-source |
| `VGS` low | Returns within about -1 to +1 V with no unintended turn-on |
| `VGS` excursions | Remain inside the recorded conservative limit; `+/-15 V` for IRF640NPBF |
| Q1 turn-on | `VDS` is at its valley and no more than `max(5 V, 10% of VDS_peak)` when `VGS` crosses its rising threshold |
| Drain slope at turn-on | Near its minimum; no large positive-slope hard-switching edge |
| Drain peak | Below the recorded stop limit; `160 V` for IRF640NPBF, and below the probe rating |
| Drain clamp | No repeated flat-topping or obvious TVS/avalanche operation |
| Q1 and load current | Stable, repeatable, and below device, shunt, supply, and connector limits |
| Output real power | Positive, repeatable, and sufficient to begin heating the cartridge |
| Burst boundaries | No single start/stop transient violates any voltage or current limit |

The `5 V or 10%` turn-on criterion is a practical screening threshold, not a
claim of mathematically perfect class-E operation. A waveform that clearly
turns Q1 on while substantial drain voltage remains is a fail even if the
average current looks acceptable.

## Hot-cartridge procedure and pass criteria

1. Begin only after the cold test passes.
2. Keep the same handpiece, probes, input voltage, firmware setting, and scope
   timing.
3. Run long enough for the cartridge impedance and waveforms to stabilize at
   its hot operating state. Do not use the startup transient for the hot
   measurement.
4. Capture Configuration A, then power down and repeat the stabilized test with
   Configuration B.
5. Apply every cold pass criterion to the hot captures as well.
6. Compare cold and hot `VDS_peak`, turn-on `VDS`, Q1 peak/RMS current,
   cartridge impedance, and delivered power. A large unexplained drift or an
   improving output accompanied by worse switching stress is a fail.

## Simple verdict

- **PASS:** both cold and hot tests satisfy every criterion through the normal
  20 V operating point with repeatable margin.
- **CONDITIONAL PASS:** 20 V passes, but a higher optional input mode does not.
  Disable or document the higher mode.
- **FAIL:** any immediate stop condition occurs, either temperature state fails
  ZVS/stress limits, or the result is not repeatable.

# Test Plan 2: Capacitor-Addition-Only Tuning

## Scope of permitted changes

Do not remove or reduce any existing production capacitor. Trial capacitors are
temporarily placed in parallel with a bank; an unsuccessful trial must be
removed before the next trial. Only a capacitor addition that passes both cold
and hot tests becomes permanent.

Use C0G/NP0 or another RF-appropriate low-loss dielectric, with voltage,
ripple-current, package, and temperature ratings at least as good as the
existing parts. Keep leads and tack connections extremely short. Power down and
discharge before every change.

Do not tune by attempting to make cartridge voltage and current exactly in
phase. At a fixed frequency their phase is principally a property of the
cartridge and cable. Use these synchronized figures of merit instead:

1. lower Q1 `VDS` at turn-on;
2. lower worst-case `VDS_peak` without repeated TVS conduction;
3. lower Q1 switching-overlap energy;
4. lower Q1 RMS/peak current for the same delivered power; and
5. greater delivered real cartridge power without worsening items 1 through 4.

An oscilloscope-derived switching-loss proxy can be calculated from
Configuration A:

```text
VDS = CH3 - CH2
IQ1_source = CH2 / RSJ1
Eswitch_proxy = integral_over_one_cycle(max(VDS * IQ1_source, 0))
Pswitch_proxy = Eswitch_proxy * 470000
```

This is comparative, not a calibrated semiconductor-loss measurement. Source
current contains displacement currents, probe delay creates multiplication
error, and the shunt/probes perturb the circuit. Use the same deskew, probes,
scales, sample rate, and integration window for every candidate.

## Bank order and trial increments

Change only one bank at a time. Test each value relative to the current best
configuration, not cumulatively without intermediate measurements.

| Order | Bank | Existing total | Suggested independent additions | Reason |
| ---: | --- | ---: | --- | --- |
| 1 | C10 + C11 | 7.8 nF series bank | 100 pF, 220 pF, 470 pF, 1 nF | Direct output-match variable; most appropriate first adjustment for a measured cold cartridge |
| 2 | C6 + C7 + C8 | 30 nF shunt bank at `M2` | 220 pF, 470 pF, 1 nF, 2.2 nF | Adjusts the transformation presented toward the class-E drain |
| 3 | C2 + C3 + C4 + C5 | 81 nF series bank | 1 nF, 2.2 nF, 4.7 nF | Small drain-side reactive trim because the existing bank is only about `-j4.18 ohms` at 470 kHz |
| 4 | C9 | 6.8 nF shunt bank at `M3` | 22 pF, 47 pF, 100 pF maximum initial trials | Last resort only; C9 with L3/L4 sets the approximately 469.5 kHz immittance converter |

Do not start with C9. Large additions there move the deliberately tuned
L3-C9-L4 inverter away from the carrier and make the behavior of the other
banks harder to interpret.

The additions in the table are exploration values, not guaranteed answers.
If the smallest practical addition causes an immediate stress increase, stop
testing that direction at that bank. The correct mathematical solution may
require less capacitance; that solution is outside this add-only plan.

## Establish the baseline

1. Complete Test Plan 1 with no added tuning capacitors.
2. Choose a repeatable cold starting condition and a fixed point inside the RF
   burst for measurements.
3. Save three Configuration A and three Configuration B acquisitions at the
   selected reduced-power tuning point and at normal 20 V operation.
4. Record the mean and spread of:
   - Q1 turn-on `VDS`;
   - `VDS_peak`;
   - Q1 source peak and RMS current;
   - cartridge peak and RMS current;
   - `Pcartridge`;
   - cartridge phase and complex impedance; and
   - `Pswitch_proxy` if it can be calculated reliably.
5. Repeat at the stabilized hot state. Define the worse cold/hot result for
   each stress metric as the baseline to beat.

## Candidate test loop

For each temporary capacitor addition:

1. Power down and discharge the board.
2. Add one trial capacitor to the selected bank with the shortest possible
   connection. Record its measured or marked value and location.
3. Return the cartridge to the defined cold condition.
4. Repeat Configuration A first at the reduced-power tuning point. Reject the
   candidate immediately if any pass criterion worsens into a failure or any
   stop condition occurs.
5. If safe, repeat Configuration A at 20 V, followed by Configuration B at the
   same operating point. Capture three repetitions.
6. Heat the cartridge to its stable hot state and repeat both configurations.
7. Compare the candidate with the baseline using the acceptance rules below.
8. Keep the best passing addition as the new baseline, or remove the failed
   trial and restore the previous best configuration.
9. Test the next value or bank only after the previous state has been clearly
   recorded and restored.

## Candidate acceptance rules

A capacitor addition is accepted only if:

- every pass/fail criterion remains satisfied in both cold and hot states;
- no stress metric worsens by more than the larger of 5% or the measured
  three-run repeatability band; and
- it produces at least one meaningful improvement:
  - at least 10% lower Q1 turn-on `VDS` or switching-loss proxy while delivered
    power stays within 5% of baseline; or
  - at least 5% greater delivered real power while Q1 turn-on voltage, drain
    peak, Q1 current, and switching-loss proxy do not materially worsen.

When metrics conflict, prefer lower Q1 turn-on voltage and lower worst-case
drain peak over a small increase in output power. Reject an improvement that
exists only when hot but worsens the cold start, or vice versa.

Stop the add-only program when:

- all four banks have been tried without an accepted improvement;
- the next addition would exceed a component or probe rating;
- an accepted configuration has repeatable margin in both cold and hot tests;
  or
- the trend indicates that less capacitance is required.

In the last case, document **no add-only solution**. Do not force an addition
merely because removing/replacing an existing capacitor is outside the allowed
scope.

## Test Record

Use one row per baseline or capacitor trial and attach the saved scope files.

| Run | State | Added bank/value | Input V | `VDS` at turn-on | `VDS` peak | Q1 I peak/RMS | Load I RMS | Load phase | Load real power | Loss proxy | Verdict |
| --- | --- | --- | ---: | ---: | ---: | --- | ---: | ---: | ---: | ---: | --- |
| B0 | Cold | None | | | | | | | | | |
| B0 | Hot | None | | | | | | | | | |
| | Cold | | | | | | | | | | |
| | Hot | | | | | | | | | | |

Record rejected trials as well as accepted ones. A negative result establishes
which tuning direction should not be repeated and is especially important when
the permitted adjustment is addition-only.

## Data-Sheet References

- [Infineon IRF640N data sheet](https://www.infineon.com/assets/row/public/documents/24/49/infineon-irf640n-datasheet-en.pdf)
- [STMicroelectronics SM15T150CA product page](https://www.st.com/en/protections-and-emi-filters/sm15t150ca.html)

