# Lite continuous-carrier duty experiment

2026-09-27. Investigate changing the gate HIGH fraction within every RF cycle,
independently of the existing burst power setting. No firmware changes made.

## Method and limits

Original nominal PCB capacitors, including C11; 20 V supply; 470 kHz;
continuous carrier. The inherited FDP18N20F model and STP-CN04 cold, warm
(below Curie), and hot (above Curie) impedances are unchanged from the
[overtemperature study](lite-overtemperature-tuning.md). These are not measured
K75C002 impedances, and the model does not predict equilibrium temperature.
Actual fitted MOSFET and any board modifications must be accounted for before
applying the results. Clamp action, avalanche, core losses/saturation and thermal
feedback are not modeled.

[duty_sweep.py](sim-470khz/overtemperature/duty_sweep.py) runs 30 cases at
400 cycles with a 10 ns maximum timestep; `--verify` runs 21 cases at 800
cycles with a 5 ns maximum timestep, including more extreme low duties.
Measurements average the final 40 cycles. Peak drain voltage includes startup.
Duty is the command waveform's midpoint-to-midpoint HIGH fraction; gate rise,
fall, Miller plateau and threshold affect actual MOSFET conduction time.

The sweep holds frequency constant: at 470 kHz the period is 2.128 us;
50% gives approximately 1.064 us HIGH, while 40% gives 0.851 us HIGH.

Class E can be designed for non-50% duty. The matching and shunt network must
still provide appropriate drain voltage at turn-on; changing duty alone in a
fixed network is not equivalent to redesigning it for that duty. See
[Yang et al., Analysis and design of Class-E power amplifiers at any duty ratio](https://metal.shanghaitech.edu.cn/publication/J5.pdf).

## Initial screening

| Gate duty | Cold tip W | Warm tip W | Hot tip W | Warm MOSFET W | Warm drain at gate rising through 3 V |
| --- | ---: | ---: | ---: | ---: | ---: |
| 30% | 15.25 | 14.48 | 5.81 | 16.87 | 65.0 V |
| 40% | 43.84 | 48.88 | 5.78 | 12.25 | 45.4 V |
| 45% | 58.39 | 67.64 | 5.75 | 7.98 | 25.1 V |
| 50% | 65.77 | 77.64 | 5.73 | 6.03 | 4.8 V |
| 52.5% | 66.54 | 78.49 | 5.72 | 5.93 | -0.7 V |
| 55% | 66.44 | 78.33 | 5.71 | 5.94 | -0.8 V |
| 60% | 66.25 | 78.08 | 5.69 | 5.90 | -0.8 V |
| 65% | 66.67 | 77.93 | 5.83 | 5.82 | -0.8 V |

At 65%, the **hot** MOSFET dissipates 2.92 W and has 31.1 V drain voltage at
the 3 V gate crossing. Thus the warm-state column alone cannot qualify a duty.
The crossing is an indicator of turn-on conditions, not an exact measurement
of the instant channel conduction begins.

Reducing duty to 40% substantially reduces useful cold/warm power but hardly
changes hot power. Worse, the switch now discharges the drain capacitance from
substantial voltage each cycle. At 45 V, C1 alone contains about 15.2 uJ;
discharging that energy at 470 kHz corresponds to about 7.1 W. This is an
illustration of hard-switching loss, not an additional loss to add to the
simulated MOSFET dissipation.

## Longer, finer verification and shorter pulses

The repeat at 40%, 50% and 55% agreed with initial tip-power results within
0.2%. Maximum adjacent-window power change was 0.034% in the initial sweep
and 0.013% in verification. These check numerical consistency, not hardware
accuracy. Full data: [initial CSV](sim-470khz/overtemperature/duty.csv) and
[verification CSV](sim-470khz/overtemperature/duty_verify.csv).

| Gate duty | Cold tip W | Warm tip W | Hot tip W | Warm MOSFET W | Hot MOSFET W |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10% | 1.03 | 0.76 | 0.64 | 6.08 | 7.09 |
| 15% | 1.85 | 1.38 | 1.60 | 8.12 | 6.89 |
| 20% | 3.65 | 2.81 | 3.68 | 11.37 | 3.81 |
| 25% | 7.52 | 6.31 | 5.24 | 14.88 | 1.46 |
| 50% reference | 65.78 | 77.66 | 5.73 | 6.04 | 0.72 |

Very short pulses eventually reduce hot power, but at 15% the model puts
6.89 W into the hot-state MOSFET to deliver just 1.60 W to the tip. This is
not an attractive idle-power solution; checking only hot-state loss would
also miss the large heating-state losses at intermediate duties.

## Firmware and bench implications

Carrier duty needs its own parameter, separate from `power`. The current
SAMD21 and RP2040 compare settings use half the period, and the RMT backend
uses half-period HIGH counts for both explicit pulses and the carrier.
All these paths must agree if a duty control is implemented. Burst behavior
needs separate verification after any change; these runs use continuous RF.

A small increase toward 52-55% is potentially an efficiency-tuning experiment,
not a useful hot-tip power reduction in this model. Start at 15 V and 50%,
change only one parameter, and inspect gate/drain timing and MOSFET temperature
in cold, heating and settled-hot conditions. Scope ground goes to board GND.
There is no model-certified safe duty range. Shorter pulses should not be
accepted merely because the tip warms more slowly or the USB meter reads less.

The existing L2 increase and capacitor adjustments remain more promising for
reducing above-Curie electrical power. A firmware-only alternative to study
separately is longer RF bursts with genuine gate-low pauses, so ring-up losses
are a smaller fraction of delivered energy. Startup/shutdown peaks and thermal
ripple must be checked; the present sweep does not establish safe burst timing.
