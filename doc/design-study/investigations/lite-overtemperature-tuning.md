# Lite 470 kHz overtemperature investigation

Initial study: 2026-09-22. Latest bench report and fine-trim recompute: 2026-10-10.
Results are candidate bench modifications, not validated
temperature settings or a guaranteed safe operating envelope.

## 2026-10-10 progress: smaller idle correction

On the same circuit, the user fitted a **new SHP-P and a PM75CH178 cartridge**.
Reported idle input draw is approximately **18 W**, with the tip **possibly
reaching 410 C**. This is encouraging compared with the earlier approximately
30 W / 470 C condition, which turned the tip blue and raised concern about
damage to components. No component damage was confirmed in this report.
These are different cartridge/handpiece conditions, not an A/B test of an
electrical modification; the improvement cannot be assigned to a tuning change.

The present objective is only **another 5-10 C reduction at idle**, roughly
400-405 C if the 410 C reading is confirmed, while retaining as much cold
heating and recovery power as possible. This is a relative tuning target, not
a verified cartridge setpoint. Today's supply voltage, RF mode, actual carrier,
fitted C11 value, and measured L2 were not supplied; record them before using
the comparisons below. The earlier 20 V condition is a simulation assumption,
not a newly confirmed bench condition.

The three candidate methods remain:

- Reduce the C10/C11 output bank by removing or replacing **C11 (1 nF)**.
  For this smaller correction, a near-original replacement is preferable to
  immediate full removal if the original 1 nF is still fitted.
- Shift carrier frequency in small available steps; its usefulness depends
  strongly on the capacitor configuration.
- Increase **L2**, the first series matching inductor (nominal 6.2 uH), by
  bringing its turns slightly closer together. Establish the actual inductance
  change by measurement; squeezing is not a calibrated adjustment. **L1 is
  the supply choke and is not the inductor meant here.**

### Scale of the remaining correction

For an illustrative 25 C ambient, a constant-conductance heat-loss model gives
`P_loss = G * (T_tip - T_ambient)`. Moving from 410 C to 405/400 C reduces
required steady heating by approximately **1.3% / 2.6%**. A constant-emissivity,
pure-radiation calculation, using absolute kelvin temperatures in
`P_loss = K * (T_tip^4 - T_ambient^4)`, gives **3.0% / 5.9%**. These examples
suggest starting with a **few percent hot-heating reduction**, not the earlier
40% modeled reduction from complete C11 removal. They are not bounds on this
assembly: convection, handpiece conduction and the cartridge's changing
electrical impedance are unmeasured.

The 18 W USB input reading includes amplifier, cable and other losses. Do not
multiply it by these percentages to prescribe a wattage target, or infer a
temperature change from lower input power alone. Confirm settled temperature
and cold heating/recovery on the actual PM75CH178.

### Small electrical recompute

One-at-a-time reruns use the same inherited **STP-CN04** cold/just-below-Curie/
above-Curie impedances, 20 V continuous RF, nominal inductors and 470.000 kHz
unless stated otherwise. **Neither SHP-P nor PM75CH178 impedance has been
measured or substituted into the model.** These are sensitivity comparisons,
not predictions of the new cartridge's temperature. Powers are `Rtip`
dissipation, not USB input power.

With the original C11 fitted:

| Single change | Cold W | Just below Curie W | Above Curie W | Hot reduction | Cold reduction |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original 1 nF C11, 470 kHz | 65.78 | 77.66 | 5.727 | reference | reference |
| C11 replaced by 950 pF | 64.71 | 77.09 | 5.587 | 2.4% | 1.6% |
| C11 replaced by 900 pF | 63.63 | 76.48 | 5.449 | 4.8% | 3.3% |
| C11 replaced by 820 pF | 61.86 | 75.43 | 5.235 | 8.6% | 6.0% |
| C11 removed; C10 unchanged (6.8 nF total) | 42.85 | 59.69 | 3.419 | 40.3% | 34.9% |
| Original bank + 47 nF in series; equivalent total 6.6898 nF | 40.47 | 57.01 | 3.223 | 43.7% | 38.5% |
| L2 +0.5%, 6.231 uH | 64.55 | 75.66 | 5.602 | 2.2% | 1.9% |
| L2 +1%, 6.262 uH | 63.32 | 73.64 | 5.481 | 4.3% | 3.7% |
| L2 +2%, 6.324 uH | 60.80 | 69.56 | 5.250 | 8.3% | 7.6% |
| 170 clocks, 470.588 kHz | 65.64 | 76.96 | 5.720 | 0.1% | 0.2% |
| 168 clocks, 476.190 kHz | 64.27 | 69.26 | 5.708 | 0.3% | 2.3% |
| 166 clocks, 481.928 kHz | 62.30 | 60.34 | 5.766 | -0.7% (increase) | 5.3% |

### Radio Thermal authors' 47 nF series suggestion

The user shared the bench observations with the Radio Thermal authors, **not
this analysis**. Their response, as quoted by the user:

> it seems like a 47nF capacitor in series with the tip should be enough to reduce the power consumption while at full temperature by about 50%

For ideal capacitors, adding 47 nF in series with the **original C10 || C11
bank** is equivalent to reducing that bank's capacitance:

```text
Cbank = 6.8 nF + 1.0 nF = 7.8 nF
Ceq   = Cbank * 47 nF / (Cbank + 47 nF)
      = 6.689781 nF
C11 replacement, with C10 fixed = Ceq - 6.8 nF
                               = -0.110219 nF = -110.219 pF
```

**There is no passive C11-only replacement that gives this exact equivalent
with C10 left at 6.8 nF.** Removing C11 gives 6.8 nF, only 1.65% above the
6.6898 nF target, and is the closest achievable C11-only change. To realize
the exact nominal capacitance without a series part, remove C11 and reduce
C10 to 6.6898 nF total. Replacing C11 by 910 pF instead gives 7.710 nF; that
is a much smaller trim than the authors' suggested series change.

This calculation assumes the original 1 nF C11 is still fitted. If it is
already removed, adding 47 nF in series with the remaining 6.8 nF gives
`6.8 * 47 / (6.8 + 47) = 5.94052 nF`; that is a different operating point,
not the 6.6898 nF row above. The equivalence is for ideal series capacitance;
an actual additional capacitor, cable and mounting introduce their own losses
and parasitics.

The added rows use the existing `verify.csv` high-resolution runs (800 cycles,
5 ns maximum timestep), including its exact `series47n` equivalent. In this
inherited load model, the series suggestion reduces **above-Curie tip heating
by 43.7%**, but **total hot input power by 23.6%**. For clarity, the input
comparison is:

| Modification at 20 V / 470 kHz continuous | Cold input W | Just below Curie input W | Above Curie input W | Hot input reduction |
| --- | ---: | ---: | ---: | ---: |
| Original bank | 78.28 | 94.74 | 10.605 | reference |
| C11 removed, C10 unchanged | 50.50 | 70.57 | 8.275 | 22.0% |
| Original bank + 47 nF in series / 6.6898 nF equivalent | 47.78 | 67.29 | 8.102 | 23.6% |

The authors' approximately 50% estimate is retained as their advice based on
the reported hardware observations. This model does not reproduce the actual
18 W idle input or PM75CH178 impedance, so it cannot verify that estimate for
the new setup. The calculated series equivalent is a substantially larger
attenuation than the fine 900-950 pF C11 trims; compare cold heating and
recovery as well as idle draw when evaluating it.

**If C11 is still 1 nF, first try about 950 pF effective C11, then 900 pF
only if needed.** Keep C10, L2 and carrier fixed. For example, short-mounted
820+130 pF or 820+82 pF in parallel give nominal 950 or 902 pF; the 900 pF
simulation is an approximate comparison for the latter. Use C0G/NP0 parts
with suitable RF current capability and voltage rating, and account for
tolerance and mounting parasitics. A 50 pF trim is only 0.64% of the complete
7.8 nF bank, so actual component values matter. Relative to baseline, the
950/900 pF candidates retain 98.4%/96.7% of modeled cold heating and
99.3%/98.5% just below Curie. Full removal is now too coarse as a first trial.

L2 +0.5% is an alternative fine adjustment, followed by +1% if needed. Those
are only +0.031/+0.062 uH from a nominal 6.2 uH baseline, much smaller than
the earlier +0.1 to +0.3 uH trials. Fixture repeatability may limit resolution;
do not treat an unresolvable LCR reading as a precise setting. The model costs
2.6%/5.2% of just-below-Curie heating for these two L2 steps, so C11 trimming
has the better hot-versus-useful-heating tradeoff among these original-bank
candidates. Winding parasitics and loss can change that result on the bench.

With **C11 already removed**, the local sensitivity changes:

| Single change from C11-removed baseline | Cold W | Just below Curie W | Above Curie W | Hot reduction | Cold reduction |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline, 470 kHz | 42.85 | 59.69 | 3.419 | reference | reference |
| L2 +0.5%, 6.231 uH | 41.84 | 58.17 | 3.327 | 2.7% | 2.4% |
| L2 +1%, 6.262 uH | 40.86 | 56.68 | 3.238 | 5.3% | 4.7% |
| 170 clocks, 470.588 kHz | 42.75 | 59.41 | 3.404 | 0.5% | 0.2% |
| 168 clocks, 476.190 kHz | 42.12 | 56.74 | 3.300 | 3.5% | 1.7% |
| 166 clocks, 481.928 kHz | 41.95 | 53.93 | 3.265 | 4.5% | 2.1% |

If the present board has C11 removed and runs at 170 clocks, **try 168 clocks
first**, keeping the inductors fixed. From that actual 170-clock reference,
the modeled hot reduction is 3.1%, with 1.5% less cold and 4.5% less
just-below-Curie heating. This is a more useful frequency tradeoff than with
the original bank. Check recovery before considering 166 clocks. If C11 has
an intermediate replacement value, neither table directly applies; use that
value in a rerun rather than combining the percentages above.

The fine capacitor candidates raise modeled hot drain peak from 119.7 V to
120.4/121.2 V and MOSFET drain loss from 0.716 W to 0.741/0.766 W. Small L2
increases instead slightly reduce those hot-state quantities. With C11 removed,
170 to 168 clocks reduces modeled hot peak from 131.0 to 125.7 V and MOSFET
drain loss from 1.176 to 0.899 W. A lower USB reading can therefore partly
reflect lower amplifier loss rather than lower tip heating. The earlier model
omissions and waveform checks still apply.

Make one change at a time, reuse the electrical screening below and the
[L2 adjustment procedure](../lite-inductor-tuning.md), and stop tuning when a
repeatable 5-10 C idle improvement is achieved with acceptable recovery.
Record an unchanged-configuration baseline, settled candidate, restored
baseline and repeated candidate using the same tip contact, airflow and
soldering load. Confirm the approximate 410 C reading before pursuing a 5 C
difference. Continuous results do not qualify Eco operation.

Local reproduction: `python doc/design-study/sim-470khz/overtemperature/fine_idle_sweep.py`
and the same command with `--without-c11`. Each run uses 800 carrier cycles,
a 5 ns maximum timestep and a final 40-cycle average; adjacent steady-window
power changes were below 0.006%. The script, case lists, netlists, logs and
CSV results are retained locally under the already Git-ignored
`sim-470khz/overtemperature/` directory. Thermal estimates above use only the
stated illustrative heat-loss formulas.

## Initial recommendation for the earlier 470 C condition

**Remove C11 (1 nF), leave C10 (6.8 nF) fitted, and keep the carrier at its
original approximately 470 kHz.** C10 and C11 are parallel between `M4` and
`RF-OUT`. This reduces the output coupling bank from 7.8 nF to 6.8 nF without
cutting a trace or inserting a component into the output cable.

At 20 V, the inherited above-Curie load model receives about 40% less heating
power, while the just-below-Curie model retains about 77% of its original power.
That is a better tradeoff than the useful parallel-only changes examined here.
The hot-state power is close to the original network at 15 V, which is the
user's known working temperature condition. This is an electrical comparison;
it does not predict that the modified hardware will idle at 376 C.

If full removal reduces heating more than desired, fit 220 pF or 470 pF at C11
instead. Use C0G/NP0 with the original voltage rating or higher and suitable RF
current capability. Keep the existing C10. Do not increase C10/C11 to cure this
problem: increasing this bank increased above-Curie heating in the model.

## Reported hardware behavior

- K75C002 cartridge, intended 75 temperature series.
- Approximately 470 C at 20 V; inline USB-C meter indicates roughly 30 W input.
- Approximately 376 C at 15 V.
- Approximately 470 C in Eco at 20 V, described as 60% burst operation.
- Temperature measured with a dedicated tip thermometer using a disposable
  three-arm contact sensor. Reliable Eco input wattage is unavailable.
- Fitted MOSFET and actual cold/warm/hot K75C002 impedances are unconfirmed.

The manufacturer's [K75C002 datasheet](https://www.thermaltronics.com/downloads/datasheets/K75C002_Technical_Datasheet.pdf)
specifies 350-398 C. That range covers geometry and measurement differences;
398 C is not an exact setpoint for every cartridge geometry. The Curie change
does not eliminate every real electrical loss or create a hard temperature
ceiling. Equilibrium still depends on delivered power and heat loss.

As a diagnostic, keep the tip on the thermometer and briefly disable RF. An
abrupt reading step, distinct from normal cooling, would suggest RF-dependent
measurement error or sensor heating. This is a check, not a conclusion that
the reported temperature is wrong.

## Simulation and its limits

The study uses LTspice 26.0.2 and the component values and connectivity in
`electrical/hot-wand-lite.sch`:

```text
Q1 drain -- C2..C5 (81 nF) -- L2 (6.2 uH) -- M2
M2 -- L3 (16.9 uH) -- M3 -- L4 (16.9 uH) -- M4
M2 -- C6..C8 (30 nF) -- GND
M3 -- C9 (6.8 nF) -- GND
M4 -- L5 (11.5 uH) -- GND
M4 -- C10 || C11 (6.8 nF || 1 nF) -- RF-OUT -- cable -- tip
Q1 drain -- C1 (15 nF) -- GND
Supply -- L1 (33 uH) -- Q1 drain
```

The load states come from the local `from_discord_sept5_2026.asc`, whose comments
identify an **STP-CN04**, not the user's K75C002:

| Model state | R | L |
| --- | ---: | ---: |
| Cold | 11.6 ohm | 5.5 uH |
| Just below Curie | 14.0 ohm | 7.2 uH |
| Above Curie | 1.54 ohm | 1.7 uH |

The active parameter table's 1.54 ohm value is retained. A separate 0.1 ohm
tip-inductor DCR and 0.5 ohm cable resistance are also retained. Reported tip
power means dissipation in `Rtip`; it excludes cable and modeled inductor DCR.

The drive is simplified to 12 V with 50 ns source edges, 50% midpoint duty, and
5 ohm source impedance. Each real split-driver path has one 3.6 ohm resistor;
5 ohm includes assumed driver impedance. The inherited FDP18N20F transistor
model is retained because the fitted transistor is unknown. Supply impedance
is 50 milliohm; bulk capacitance is 940 uF with assumed 50 milliohm ESR.
RF ESR/DCR values are inherited estimates, not measurements of this board.

**D3/D4 protection, MOSFET avalanche, magnetic-core loss/saturation, PCB/cable
parasitics, temperature-dependent transistor behavior, and thermal feedback
are not modeled.** Drain peaks are unclamped electrical-model results. Values
above the modeled device's rating describe an invalid hardware operating
condition, not a prediction that it survives. No numerical temperature can be
calculated from these three fixed load states.

The original model draws about 10.6 W in its above-Curie state at 20 V, unlike
the reported approximate 30 W. Thus it does not reproduce the user's hardware
quantitatively. Real cartridge impedance, fitted inductances, losses, waveform,
and metering remain important uncertainties. Use these results to choose a
reversible experiment, not to assert a certain temperature reduction.

![Electrical sweep summary](sim-470khz/overtemperature/summary.png)

## Capacitor results

Continuous carrier, nominal 470 kHz. Powers below are modeled tip heating,
not USB input power. C11 trimming cases were run for 800 cycles with a 5 ns
maximum timestep; final powers average the last 40 cycles.

| Modification | Cold W | Just below Curie W | Above Curie W |
| --- | ---: | ---: | ---: |
| Original, 20 V | 65.8 | 77.7 | 5.73 |
| Original, 15 V | 37.1 | 43.8 | 3.23 |
| C11 replaced by 680 pF, 20 V | 58.7 | 73.4 | 4.88 |
| C11 replaced by 470 pF, 20 V | 53.7 | 69.8 | 4.38 |
| C11 replaced by 220 pF, 20 V | 47.8 | 64.7 | 3.84 |
| **C11 removed, 20 V** | **42.9** | **59.7** | **3.42** |
| Original bank + 47 nF in series (6.6898 nF equivalent), 20 V | 40.5 | 57.0 | 3.22 |
| Add 22 nF across C2-C5, 20 V | 53.6 | 58.5 | 4.67 |
| Add 47 nF across C2-C5, 20 V | 45.7 | 47.6 | 4.07 |
| Add 100 nF across C2-C5, 20 V | 37.8 | 37.5 | 3.47 |

The output bank's series reactance changes from approximately -j43.4 ohm to
-j49.8 ohm when C11 is removed. The complete loaded network then supplies less
current to the low-resistance above-Curie load. This is not based on treating
one adjacent LC pair as the whole amplifier's resonance.

If adding a parallel part is preferred, bridge the two pads of any one of
C2-C5 with 22 nF initially, then 47 nF total added if needed. All four footprints
are across the same drain-to-M1 bank. **This is not a capacitor to ground.**
The larger bank increases the series branch's net inductive reactance at
470 kHz. It reduces power, but sacrifices more just-below-Curie heating than
removing C11. Use short connections and an appropriate C0G/NP0 or RF-rated film
part; a DC voltage rating alone does not establish RF ripple-current capability.

Other screened changes are less attractive:

- Adding 4.7 nF across C1 cuts hot power only about 10%. Larger additions
  eventually force substantial turn-on voltage and MOSFET loss; adding 22 nF
  gave about 10 W of modeled hot-state MOSFET drain loss.
- Adding 1 nF across C9 cuts hot power about 15%, but changes switching timing.
  Adding 2.2 nF produced about 40 V on the drain before hot-state turn-on.
- Added capacitance across C6-C8 reduces useful heating relatively strongly.
- Added capacitance across L5 or from RF-OUT to ground increased hot-tip power.
- Adding 1 nF to C10/C11 raised hot power from 5.73 to about 8.77 W.

Removing C11 has its own stress tradeoff. The all-time hot-model drain peak,
including startup, rises from about 120 to 132 V at 20 V. Modeled hot-state
MOSFET drain loss rises from about 0.72 to 1.21 W, although cold/warm losses
fall. C10 carries approximately 1.92/2.06/1.49 A RMS in cold/warm/hot states
after removal. Its existing share before removal was approximately
2.08/2.05/1.68 A RMS, assuming ideal proportional sharing with C11. In other
words, removing C11 does not simply leave C10 carrying an extra 15% current;
the circuit operating point changes too. These are nominal electrical estimates.

## Frequency direction and bench range

**Try a small upward change first if evaluating frequency, but it is not the
preferred cure.** With the original capacitors, model hot power has a shallow
minimum near 470-476 kHz. Moving much further upward reduces useful heating
while increasing hot-state heating and switching loss.

| Carrier | Cold W | Just below Curie W | Above Curie W | Hot MOSFET drain loss W |
| --- | ---: | ---: | ---: | ---: |
| 450 kHz | 73.4 | 89.0 | 7.02 | 2.19 |
| 470 kHz | 65.8 | 77.6 | 5.73 | 0.71 |
| 480 kHz | 63.1 | 63.3 | 5.74 | 0.33 |
| 490 kHz | 57.5 | 48.3 | 5.99 | 0.71 |
| 500 kHz | 48.7 | 35.8 | 7.52 | 5.05 |
| 520 kHz | 29.0 | 19.4 | 20.45 | 23.08 |

There is **no established universally safe sweep range** for the assembled board.
For the original capacitors, use approximately **470-482 kHz as the initial
scope-supervised search window at 15 V**, with the cartridge connected. This
window is a cautious test proposal, not a certified operating band. Check cold,
warming, and hot conditions at every point. Avoid a blind 400-600 kHz sweep or
assuming a lower input current means less tip heating. Downward movement from
470 kHz increased modeled hot heating and worsened warm-state turn-on timing.

For the ESP32 80 MHz RMT clock, even period counts retain exactly 50% duty:

| Period clocks | High/low clocks | Actual frequency |
| ---: | ---: | ---: |
| 170 | 85/85 | 470.588 kHz |
| 168 | 84/84 | 476.190 kHz |
| 166 | 83/83 | 481.928 kHz |

The subsequent firmware change adds `rfgen_set_freq(hertz)` and the serial
`freq <hertz>` command; the fixed 170/85/85 assertions have been removed.
`RFGEN_FREQUENCY_HZ` in `firmware-lite/inc/rfgen.h` remains the boot default.
Verify actual frequency/duty at the gate. Evaluate capacitor
changes at the original frequency first, because changing both obscures the
cause and invalidates the original-network sweep guidance.

## Why Eco can stay hot

The current source sets Eco to 70% in `firmware-lite/src/power.cpp`, although
the tested hardware may have an earlier 60% build. Neither is a calibrated
measurement of delivered heating power.

The common generator's 60% table contains 18 normal carrier periods followed
by an eight-period long entry. That long entry begins with another high pulse:
the actual waveform has 19 highs per 26 carrier periods. At 70%, there are
28 highs per 35 periods. The 12-cycle startup weighting assumes 80% energy
delivery and is not adjusted for cartridge impedance.

Simulating those explicit, complete-pulse patterns gives:

| Pattern | Original cold W | Original hot W | C11 removed cold W | C11 removed hot W |
| --- | ---: | ---: | ---: | ---: |
| Continuous | 65.77 | 5.73 | 42.85 | 3.42 |
| Nominal 60% | 35.58 | 5.30 | 28.80 | 3.05 |
| Nominal 70% | 43.48 | 6.30 | 32.13 | 3.66 |

The nominal 60% pattern delivers about **92% of continuous hot-state heating**;
70% delivers about **110%**. Gate-off time does not disconnect the tip from
the passive network. Stored energy keeps flowing and the next burst starts
from a different electrical state. A cold-load startup weighting cannot
predict this for all tip states. These results provide a plausible mechanism
for the observation without requiring an RMT pulse-splitting defect.

The burst averages cover 910 carrier periods, an integer multiple of both
envelopes, after 910 periods of settling. Current source enables explicit
short bursts on C3; the older RMT investigation describes a historical build
and should not be assumed to describe the currently flashed firmware.

Unclamped burst peaks also exceed the continuous values: the nominal warm
model gives about 229/214 V for the original 60%/70% patterns and 181/164 V
after removing C11. Real protection conduction will change these results.
Consequently, satisfactory continuous waveforms do not validate Eco; inspect
burst starts, stops, and ringing separately. The fitted MOSFET's rating and
TVS dissipation matter even if mean input power is lower.

## Bench sequence

1. Record the original gate/drain waveform at 15 V and 20 V with the cartridge
   connected, including transitions into and out of bursts. Confirm the fitted
   MOSFET part and set voltage/current/temperature stop limits accordingly.
2. Power down and discharge the board; remove **C11 only**. Keep C10 fitted.
3. Start at 15 V with continuous carrier and the original frequency. Confirm
   that the drain returns close to zero before turn-on through cold, warming,
   and hot conditions. Check peak voltage, current, and MOSFET/inductor heating.
4. Increase toward 20 V in small steps, repeating those checks and recording
   settled temperature. Do not use an open output as a tuning load.
5. Validate Eco separately, including burst boundaries. If continuous operation
   is satisfactory but Eco is not, correct the burst strategy rather than
   treating its percentage label as a thermal-power measurement.
6. If C11 removal reduces useful heating too far, try 220 pF or 470 pF at C11.
   Record heating recovery as well as idle temperature.

Use the grounded-probe precautions and measurement setup in
[`lite-proposed-tuning-test-plan.md`](lite-proposed-tuning-test-plan.md). Scope
ground clips go to circuit ground, not to the drain or either side of an RF
component. For a 200 V MOSFET, the existing plan uses a 160 V development stop
limit; use the limits of the actual fitted parts, probes and protection circuit.

## Reproduction

Files are under [`sim-470khz/overtemperature`](sim-470khz/overtemperature/).

```powershell
python doc/design-study/sim-470khz/overtemperature/sweep.py
python doc/design-study/sim-470khz/overtemperature/plot.py
```

Python needs numpy and matplotlib for analysis/plotting. `--ltspice` selects
another executable path. `--group` selects one sweep; `--analyze-only` reuses
existing RAW/log files. Generated netlists, logs and CSV tables retain the
conditions and results. Large RAW files are locally ignored by Git.

Broad sweeps use 400 cycles and 10 ns maximum steps. Baseline and the selected
C11-removal candidate were rerun with 800 cycles and 5 ns steps. The baseline
tip powers differed by less than 0.04% from the broader run. Comparisons use
whole carrier/envelope periods; the voltage-at-turn-on column samples the
rising gate crossing at 3 V, before the inherited model's 4 V threshold.
It is a timing indicator, not a complete switching-loss or SOA test.
