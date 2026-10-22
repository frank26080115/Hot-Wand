# CT reaction: dummy-load polarity investigation

2026-10-21. Bench results reported by the builder for the 13.56 MHz Hot Wand.
The question is whether the current-transformer phase detector acts in the
intended direction, or whether all four ring diodes need to be reversed.

**Working hypothesis:** the combined dummy-load and scope observations support
the intended detector/feedback direction and an idle-bias explanation for the
RF-on voltage rise. The cold-cartridge tests now demonstrate both a buck
voltage increase and a decrease, accompanied by detector excursions below
and above the previously observed 0.8 V idle bias. Keep the present diode
orientation. On 2026-10-22 the builder also reported successful full-firmware
warm-up, decreasing indicated power as the cartridge heats, and very low
power in the magnetic stand. These are qualitative operating observations;
calibrated tip-temperature and RF-power measurements were not supplied.
Both capacitive dummy-load attempts still trip tip detection.

## Setup and measurement

The dummy load has 50 ohms of resistance on an actively cooled aluminum
heatsink. Its optional reactive components and nominal impedances are described
in [Fooling Around with Dummy Load](../current-transformer.md#fooling-around-with-dummy-load).
Positive phase means voltage leads current, corresponding to an inductive
load. These phase values are calculated nominal values from that study,
not phase measurements taken during these runs.

The [CT diagnostic](../../../firmware/src/tests.c) defaults to a 5,000 us RF
burst and requires a five-second cooldown. It now supports two compile-time
modes:

- `TEST_CT_UNATTENUATED=0`: fixed TIM3 CCR1 = 14 and a 14.0-16.0 V
  starting-voltage window, as intended for tests 1-5.
- `TEST_CT_UNATTENUATED=1`: attenuation pin held low as GPIO, no PWM, and no
  starting-voltage window. This is the new default. Tests 6-12 are presumed
  to use this mode because they were reported after that firmware change;
  the actual flashed setting has not been explicitly confirmed.

The displayed pair is the filtered buck-voltage reading immediately before
RF start and immediately before commanded RF stop. After an interlock abort,
the second reading is instead the latest available reading after that shutdown.
The ADC uses its existing round-robin conversions and filtering; these are
endpoint readings, not a waveform capture.

Actual burst durations, input-voltage traces, and abort indicators for tests
1-4 and 7-12 were not supplied. The nominal 5 ms duration is a firmware default,
not a measured duration for these runs. The dummy-load series have different
starting voltages and should be compared within each series before drawing
conclusions from their differences. Tests 11-12 use an actual cold cartridge;
its impedance and phase under these conditions have not been measured.

## Results: series A, fixed attenuation

Configurations below are matched to the reported phases using the existing
dummy-load study. The 146 nH inductor remains in series with the resistance
unless its bypass jumpers are shorted; added capacitors are in parallel with
the inductor.

| Test | Dummy-load configuration | Nominal impedance at 13.56 MHz | Phase | Start | End | Rise, end minus start | Rise relative to test 2 |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| 1 | Inductor alone; bypass open | `50 + j12.44 ohms` | +14.0 degrees | 15.14 V | 19.02 V | +3.88 V | +0.11 V |
| 2 | Inductor bypassed | `50 + j0 ohms` | 0.0 degrees | 15.12 V | 18.89 V | +3.77 V | 0.00 V |
| 3 | 300 pF across inductor; bypass open | `50 + j18.24 ohms` | +20.0 degrees | 15.14 V | 18.73 V | +3.59 V | -0.18 V |
| 4 | 500 pF across inductor; bypass open | `50 + j26.46 ohms` | +27.9 degrees | 15.14 V | 15.78 V | +0.64 V | -3.13 V |
| 5 | 2,000 pF across inductor; bypass open | `50 - j11.11 ohms` | -12.5 degrees | Not reported | Inconclusive | Not calculated | Not calculated |

Test 5 triggers tip-open detection despite the intended connected-load
configuration. It provides no usable voltage pair for assessing polarity.
The final column subtracts test 2's **voltage rise**, rather than its endpoint,
to account for the 0.02 V difference in starting voltage.

## Results: series B, presumed unattenuated

Test 6 repeats the capacitive attempt and again triggers tip-open detection.
Tests 7-10 report the following voltage pairs:

| Test | Dummy-load configuration | Phase | Start | End | Rise, end minus start | Rise relative to test 10 |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| 6 | 2,000 pF across inductor; bypass open | -12.5 degrees | Not reported | Inconclusive: tip-open trip | Not calculated | Not calculated |
| 7 | 500 pF across inductor; bypass open | +27.9 degrees | 18.7 V | 19.87 V | +1.17 V | -4.61 V |
| 8 | 300 pF across inductor; bypass open | +20.0 degrees | 18.7 V | 22.51 V | +3.81 V | -1.97 V |
| 9 | Inductor alone; bypass open | +14.0 degrees | 18.7 V | 23.68 V | +4.98 V | -0.80 V |
| 10 | Inductor bypassed | 0.0 degrees | 18.75 V | 24.53 V | +5.78 V | 0.00 V |

The final column subtracts test 10's voltage rise, accounting for the 0.05 V
difference in starting voltage. Tests 5 and 6 provide two observations of a
capacitive-load tip-open trip, not two measurements of detector polarity.

## Scope follow-up on the dummy loads

The builder subsequently observed the following with the oscilloscope:

- Buck voltage starts rising when RF begins, coincident with a fall in
  `RF-FB-RECTIFIED`. This supports a physical voltage increase rather than
  the increase existing only in the MCU's ADC reading.
- With the resistive load, `RF-FB-RECTIFIED` is approximately 0.8 V at idle.
  During RF it drops sharply, apparently below zero, with substantial
  RF interference obscuring the waveform. When RF stops, it visibly recovers
  from approximately 0 V toward 0.8 V.
- With the +27.9-degree load, the idle value is again approximately 0.8 V.
  After RF stops, the node appears to recover from approximately 0.5-0.6 V
  toward 0.8 V.

The different post-burst starting levels support a higher filtered detector
output for the inductive load than for the resistive load. They are approximate
observations near turn-off, not clean measurements of the RF-on DC averages.
The apparent negative excursion should not be assigned an exact value while
RF pickup obscures the trace. No saved captures or precise peak measurements
were supplied.

## Results: series C, actual cold cartridge

The builder then reported two cold-cartridge conditions. The cartridge and
handpiece identifiers, magnet type and placement, and actual impedances were
not specified. The magnets are recorded as a test condition; this investigation
does not equate their effect with heating the cartridge or reaching Curie.

| Test | Cartridge condition | Start | End | Change, end minus start | Observed RF-FB-RECTIFIED response |
| --- | --- | ---: | ---: | ---: | --- |
| 11 | Cold cartridge without magnets | 18.7 V | 22.9 V | +4.2 V | Dips below 0.2 V |
| 12 | Cold cartridge with magnets | 18.7 V | 14.7 V | -4.0 V | Rises above 1.2 V |

Relative to the previously observed 0.8 V idle bias, the reported detector
excursions are below idle in test 11 and above idle in test 12. Separate idle
readings for these two runs were not supplied. The reported excursions are
not assumed to be settled averages or calibrated peak measurements.

The two runs start at the same buck voltage and end 8.2 V apart. Test 12 is
particularly useful: the buck is reduced below its RF-off starting voltage,
so the observed response is no longer limited to varying amounts of boost.

## Full-firmware operating observations: 2026-10-22

The builder reports running the production firmware with a real cartridge
through actual warm-up. Indicated power is high when cold, tapers as the
cartridge heats, becomes low when hot, and becomes very low in the magnetic
stand. The production-firmware dummy-load comparison also reportedly gives
approximately half as much indicated power at +27.9 degrees as at +14 degrees,
qualitatively agreeing with the earlier MX-5200 comparison. Exact readings
and temperature measurements were not supplied; these are not numbered
additional burst tests.

The production loop advances `pwrlvl_task()` through `pwrmgt_task()`, adjusting
attenuation to enforce the selected power and current ceilings. The burst
diagnostic instead holds attenuation fixed or disabled. Consequently, their
operating points need not match. Firmware limiting could mask excessive power
in principle, so normal heating alone would not settle detector polarity.
The earlier unattenuated phase ordering and detector-node scope observations
provide additional evidence for the intended analog response direction.

The graph uses `adc_get_milliwatts()`, calculated as buck voltage times sensed
buck-output current. It represents the DC power supplied to the RF stage,
not a direct measurement of RF power delivered to the cartridge. Agreement
with the MX-5200 is therefore a useful qualitative comparison, not a
calibration of equal delivered watts.

## What the measurements establish

All eight dummy-load voltage pairs show an increase when RF is enabled. The
largest dummy-load increase is the resistive case in test 10: **18.75 to 24.53 V**, a **5.78 V
rise**. The feedback-neutral distinction below provides a circuit explanation
for an RF-on increase even with the intended detector polarity. The cartridge-with-magnets result
adds an actual reduction of 4.0 V below the starting voltage.

Within series B, both the endpoint and the voltage rise decrease monotonically
as nominal inductive phase increases:

```text
Phase:        0.0       +14.0      +20.0      +27.9 degrees
Buck rise:   +5.78       +4.98      +3.81      +1.17 V
```

The +27.9-degree case rises 4.61 V less than the resistive reference after
correcting for starting voltage. This is stronger evidence of the intended
phase-dependent correction direction than the first series, whose +14-degree
case rose slightly more than its resistive reference. There is still only one
voltage pair per phase per series and no repeatability estimate.

If the unattenuated setting is confirmed for series B, the rise occurs with
the attenuation PWM absent. It therefore cannot require that PWM to be active.
Removing attenuation did not remove the observed RF-on increase. This does
not by itself identify the CT circuit as the sole cause. Subsequent scope
observations support a physical buck rise and the expected association with
falling detector voltage. Matching-dependent loading and converter dynamics
can still contribute, and RF pickup limits precise detector-level readings.

The current [buck-overvoltage setting](../../../firmware/inc/conf.h) is 25 V
and is checked against unfiltered ADC samples. Test 10's filtered endpoint is
only 0.47 V below that nominal threshold; it cannot exclude a higher transient
or an early protection trip. No such trip was reported for test 10, and one
should not be inferred solely from this proximity.

These are buck-voltage measurements, not RF-power measurements. Changing
reactance also changes RF matching, amplifier loading, and input demand.
The nominal phases have not been verified at the operating load connection.

## Zero phase and feedback-neutral are different conditions

Zero load phase means RF voltage and current are in phase. An idealized
quadrature phase detector can have approximately zero average output in
that condition. However, zero detector voltage does not mean zero effect
on this buck converter.

The TPS54560 regulates its FB pin to approximately 0.8 V
([TI datasheet](https://www.ti.com/lit/ds/symlink/tps54560.pdf)). In the
[schematic](../../../electrical/hot-wand.sch), R11 connects the detector node
to FB. Defining positive current as flowing from the detector into FB,

```text
I_CT_into_FB = (V_detector - V_FB) / R_detector
```

The contribution through that branch is zero when `V_detector = V_FB`,
approximately 0.8 V. Below that voltage, the branch draws current out of FB;
the buck raises its output to restore FB to its reference. Above it, the
branch supplies current into FB and the buck lowers its output.

For a DC approximation with attenuation fixed or absent and the other
feedback branches unchanged, Kirchhoff's current law gives

```text
V_out ~= V_baseline + G * (0.8 V - V_detector)
G = R_upper / R_detector > 0
```

Here `V_baseline` is the output with the detector at the FB reference;
`R_upper` is the output-to-FB resistance. R4 is 22 kohms with VR1 in the
upper path. Using R11 alone as the 2.2 kohm detector path gives a gain of
roughly 10 or more, depending on VR1. VR4, if connected through SJ2, changes
the effective detector coupling. A 0.6 V detector drop could therefore
request about a 6 V buck increase for a gain of 10. This illustrates the
observed scale; it is not a fitted prediction of the transient endpoints.
Supply headroom, the clamp, load drops, and active limiting can constrain
that requested output.

The feedback-neutral operating phase, if one exists at the chosen RF level,
therefore satisfies

```text
V_detector(phi_neutral, RF amplitude, circuit conditions) ~= 0.8 V
```

There is no requirement that `phi_neutral = 0 degrees`, nor enough data to
assign it a particular angle. This zero-contribution condition also does not
mean zero heating or thermal equilibrium. The observed 0.8 V RF-off level is
the biased idle condition, not a measurement of the energized detector at
zero load phase.

A change in idle-to-RF operating voltage is thus compatible with intended
polarity. The sign to examine for polarity is how detector voltage changes
with phase while RF is on, and the resulting opposite change in buck voltage.
Reversing the detector's phase-dependent sign would not be equivalent to
simply adjusting the voltage range with the buck potentiometer.

## Interpretation and working conclusion

The [existing detector explanation](../current-transformer.md) predicts that
increasingly inductive load phase raises detector feedback drive and tends
to lower buck voltage. Series B follows that relative direction across all
four usable points. A straightforward polarity inversion is consequently
less plausible than it was before this series, although these measurements
do not isolate the detector from the rest of the RF circuit.

One plausible explanation for the absolute rise is an RF-on offset combined
with a phase-dependent correction of the intended sign. With RF off, the
detector's filtered node is biased through its connection to the buck feedback
network. Its RF-on average need not equal that idle bias even at zero phase.
Enabling RF could therefore raise buck voltage at zero phase while positive
phase increasingly opposes that rise. The scope follow-up now supports this
explanation: the detector recovers toward 0.8 V from approximately zero after
the resistive burst, and from approximately 0.5-0.6 V after the inductive burst.
The mechanism is supported more directly than it was by endpoint readings
alone, although precise RF-on averages remain unmeasured.

Tests 11-12 also follow the feedback-network direction: pulling the detector
below idle accompanies a higher buck output; raising it above idle accompanies
a lower output. Through its connection to the buck feedback network, the
detector can therefore be associated with both increased and reduced output.
This is strong functional evidence against simply reversing the diodes.
However, no phase measurement was made on either cartridge condition, so the
magnet comparison alone does not calibrate the detector's phase response.

The earlier suggestion that endpoints near 19 V represented a ceiling is
unproven. Series B reaches 24.53 V, so 19 V is not a universal ceiling across
the two series. Without input-voltage captures and confirmed identical supply
conditions, neither supply headroom nor attenuation interaction can be assigned
as the reason the first series clustered near 19 V.

**Provisional conclusion: retain the present diode orientation. The phase
ordering, scoped detector recovery levels, and opposite responses in tests
11-12 collectively support the intended feedback direction.** The RF-on
voltage rise is now consistent with a measured change from the detector's
approximately 0.8 V idle bias, rather than constituting evidence by itself
that the diodes are backwards. The builder now reports the intended qualitative cold/hot/stand behavior
with production firmware. Precise transient peaks, operating temperatures,
and regulation curves have not been measured in this investigation.
The magnets have not been established as an equivalent hot-cartridge load.
The repeated capacitive tip-open trip remains unresolved.

## Measurements that would resolve the uncertainty

1. Confirm the flashed attenuation mode for tests 6-12. Repeat the resistive
   and three inductive configurations, interleaving resistive reference runs.
   Record input voltage, both buck readings, abort indication, and actual
   burst duration. Do not compare a shortened run with a completed 5 ms run.
2. Save simultaneous buck-output and filtered-detector captures with scales
   visible for the resistive and +27.9-degree loads. Reduce probe pickup and
   quantify detector levels just before turn-off and their recovery afterward.
   The qualitative scope observations already support a real buck rise; the
   remaining questions are its peak, timing, and relation to detector bias.
3. Repeat the cold-cartridge comparison, recording cartridge/handpiece IDs,
   magnet placement, detector idle level, and supply current. Confirm that the
   buck increase/decrease and opposite detector excursions are repeatable.
   Do not treat a magnet-induced impedance change as a measured temperature
   or phase change without separate evidence.
4. Investigate the repeated tip-open trip with the 2,000 pF configuration.
   Check load connections and the tip-detect signal during RF; retain the
   interlock. Obtain a valid capacitive result before claiming the sign
   response has been tested on both sides of zero phase.
