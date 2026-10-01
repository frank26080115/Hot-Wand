# Lite inductor adjustment experiment

2026-09-26. Initial recommendation: increase **L2**, the nominal 6.2 uH
series inductor wound on two stacked cores. Make small, reversible adjustments
to winding spacing, with electrical measurements before accepting a lower tip
temperature as an improvement.

## Assumptions and evidence

The user reports inductances within 0.1 uH of nominal on an LCR meter and
evenly spaced windings. Radio Thermal's authors suggested adjusting the
inductors under operating conditions.

The numerical screening below assumes the **original capacitors including
C11**, approximately **470 kHz**, **20 V**, and **continuous RF**. Confirm this
matches the hardware before using the numbers. The previous electrical model
uses STP-CN04 load states, not measured K75C002 impedances; these are directions
to investigate, not predictions of tip temperature or safe limits.

The sweep changes one ideal inductance at a time. Winding movement can also
change capacitance, leakage coupling and loss, which this sweep does not model.
The manufacturer explicitly notes that actual powder-core inductance depends
on winding construction and leakage flux:
[Magnetics powder-core explanation](https://www.mag-inc.com/products/powder-cores/learn-more-about-powder-cores).
Low-signal inductance matching alone does not validate the complete loaded RF
network or its switching behavior.

| Single adjustment | Cold tip W | Just below Curie W | Above Curie W |
| --- | ---: | ---: | ---: |
| Original | 65.77 | 77.64 | 5.73 |
| L2 +2%, 6.324 uH | 60.79 | 69.53 | 5.25 |
| L2 +5%, 6.510 uH | 53.20 | 57.97 | 4.63 |
| L2 +10%, 6.820 uH | 42.27 | 43.19 | 3.80 |
| L3 +5%, 17.745 uH | 68.10 | 80.06 | 6.05 |
| L4 +5%, 17.745 uH | 64.71 | 75.00 | 5.64 |
| L5 +5%, 12.075 uH | 68.93 | 81.01 | 6.18 |

L2 +5% reduces hot heating about 19%, but also reduces just-below-Curie heating
about 25%. This is useful attenuation, not free improvement in temperature
regulation. Its modeled cold/warm/hot MOSFET drain losses decrease from
3.79/6.03/0.71 W to 2.62/3.84/0.56 W. The corresponding all-time drain peaks
change from 85.8/93.1/119.7 V to 87.1/93.0/117.0 V. These peaks include startup;
protection clamp, avalanche, core saturation/loss and board parasitics are
omitted. Real drain waveforms take precedence.

Increasing L2 raises the series branch inductive reactance; at 470 kHz, a
0.31 uH increase adds approximately 0.92 ohm. That is significant in this
low-impedance branch. Increasing all inductors together is not equivalent.

## Procedure

1. Record the present capacitor configuration, frequency, supply voltage,
   gate/drain traces, tip temperature and amplifier temperature. Photograph
   the winding spacing so the original geometry can be restored. Use the same
   cartridge, cable, thermometer contact and airflow throughout.
2. With RF off and the supply disconnected/discharged, move a few L2 turns
   slightly closer and establish the direction and size of the inductance
   change on the LCR meter. Isolate a lead if needed to avoid measuring the
   surrounding network. Never attach the LCR meter to an energized board.
   Measure at the same frequency, fixture and temperature for comparisons.
3. Start electrical screening at 15 V, original carrier frequency, continuous
   RF (`power 100`). This avoids the load-dependent burst behavior observed
   in the earlier study. Keep the actual cartridge connected.
4. If making live fine adjustments, use a nonconductive plastic/nylon trimming
   tool and light pressure only. Do not use fingers, metal pliers or metal
   tweezers. These are RF nodes with voltages much higher than the DC input;
   enamel is not a touch-safe barrier. If a movement needs force, switch off
   and adjust it unpowered. Avoid scraping enamel, moving leads, or moving the
   whole core relative to its neighbors.
5. Monitor Q1 gate and drain together. After each tiny movement, withdraw the
   tool and observe switching first. The drain should return near zero before
   turn-on. Reject a change that produces markedly higher turn-on voltage,
   new spikes, unexpected input-current increase or rapid amplifier heating.
   Scope ground clips go to circuit GND, not an RF node. Use a properly rated
   probe and the fitted transistor's development voltage limit.
6. Start with approximately +0.1 to +0.15 uH from the measured L2 baseline.
   Then test roughly +0.2 and +0.3 uH if electrical results remain acceptable.
   For a 6.2 uH starting point this means about 6.3, 6.4 and 6.5 uH. These are
   trial points, not a prescribed optimum. Do not jump straight to +10%.
7. Qualify promising settings at increasing supply voltage toward 20 V,
   checking cold startup, warming through the transition, and hot operation.
   Wait for temperature to stabilize after each adjustment; continuous
   squeezing while watching a delayed thermometer makes overshoot likely.
   Record electrical changes immediately and thermal changes after settling.
8. Restore the original geometry and repeat a measurement, then repeat the
   candidate. A repeatable A/B result is more useful than one temperature dip.
   A jump associated only with tool/hand proximity can be RF interference or
   temporary loading, not a permanent improvement.
9. Once a candidate passes continuous operation, test Eco separately, including
   burst edges. Check heating recovery on the same repeatable soldering load.
   Accept only if idle temperature, recovery and amplifier stress are all
   suitable. More inductance can simply make the entire iron weaker.

Without a gate/drain scope measurement, use unpowered adjustments and reduced
voltage screening. Temperature alone is insufficient to qualify a live 20 V
retune. There is no guaranteed safe inductance interval established here.

Leave L1 alone: it is the supply choke, not one of the hand-wound matching
inductors. Keep L3, L4, L5, carrier frequency and capacitors fixed during the
first L2 series. If L2 does not give a repeatable useful trend within a few
small steps, restore baseline and investigate the actual load/waveforms rather
than continuing to compress turns.

| Trial | Measured L2 | Input V | Tip temperature | Drain at turn-on / peak | Input current | Amplifier temperature | Recovery |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Baseline | | | | | | | |
| Small increase | | | | | | | |
| Restored baseline | | | | | | | |

## Reproduction

Run `python doc/design-study/sim-470khz/overtemperature/inductor_sweep.py`.
The script retains its netlist, case list, log and CSV in the same directory.
The original-capacitor run comprises 51 cases: baseline and -5%, +2%, +5%,
+10% for each of L2-L5, with three fixed load states. Final powers average
40 cycles after a 400-cycle run at a maximum timestep of 10 ns. Adjacent
steady windows differed by less than 0.02% in tip power.

The optional `--without-c11` regenerates the study for C11 removed; that variant
has not been run as part of this original-capacitor screening. See also
[`lite-overtemperature-tuning.md`](lite-overtemperature-tuning.md) and
[`lite-proposed-tuning-test-plan.md`](lite-proposed-tuning-test-plan.md).
