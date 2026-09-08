# 13.56 MHz RF amplifier: unclamped VDS results

Simulated on 2026-09-07 with LTspice 26.0.2 for Windows. Runnable schematic: [rf_amp.asc](rf_amp.asc). Full main-run measurement log: [rf_amp.log](rf_amp.log).

**Provisional model results:** Q1 is represented by a datasheet-based STP19NF20 approximation, with an assumed gate waveform. ST's official SPICE-model download timed out on repeated attempts. These numbers are useful for comparing the modeled conditions; they are not validated hardware voltage limits or a completed TVS selection.

The **D4/TVS1 drain-clamp branch is omitted**, as requested, to expose the unclamped voltage demand.

## Maximum VDS over the full transient

Each cell is the result of:

```spice
.meas TRAN V_DS MAX V(Vds)
```

The measurement covers **0 to 1 ms**, including LTspice's `startup` ramp of the independent sources over the first 20 us. The maximum timestep is 200 ps and waveform compression is disabled. VCC is the DC supply **at the RF stage / L3 input**, replacing the buck converter's output.

| Tip condition | Impedance at 13.56 MHz | VCC = 15 V | VCC = 20 V | VCC = 25 V | VCC = 30 V |
|---|---|---:|---:|---:|---:|
| Cold | 42.3 + j13 ohm | 70.2 V | 120.4 V | 169.7 V | 216.5 V |
| Warm (below Curie) | 55 - j16 ohm | 65.9 V | 84.8 V | 102.9 V | 121.0 V |
| Hot (above Curie) | 12 + j24 ohm | 149.4 V | 216.1 V | 287.0 V | 361.1 V |
| Open output | 1 Tohm (open approximation) | 418.3 V | 543.3 V | 682.5 V | 803.7 V |

The largest main-run result is **803.7 V with an open output at 30 V VCC**. All open-output results exceed the physical MOSFET's 200 V rating. The model has no avalanche or failure behavior: these are hypothetical unclamped voltages, not predictions that a real STP19NF20 survives or actually reaches them.

## Final-window comparison

For comparison with measurements that exclude startup, `V_DS_LAST` measures the final 20 us (980 us to 1 ms):

| Tip condition | Impedance at 13.56 MHz | VCC = 15 V | VCC = 20 V | VCC = 25 V | VCC = 30 V |
|---|---|---:|---:|---:|---:|
| Cold | 42.3 + j13 ohm | 69.6 V | 119.5 V | 168.4 V | 214.9 V |
| Warm (below Curie) | 55 - j16 ohm | 65.9 V | 84.8 V | 102.9 V | 121.0 V |
| Hot (above Curie) | 12 + j24 ohm | 145.8 V | 210.9 V | 280.1 V | 352.6 V |

The three loaded cases have closely matching maxima in the last two 20 us windows. **The open output does not settle to a repeatable envelope within this run.** Its table entries above are maxima observed over a finite 1 ms simulation, not established steady-state bounds.

## Circuit and load models

The output network follows [electrical/hot-wand.sch](../../../electrical/hot-wand.sch):

- Q1 is M1 in LTspice, with its source grounded and drain labeled `Vds`.
- L3 = 9 uH; L4 = 180 nH; L5 = 400 nH; L6 = 540 nH.
- C9-C12 are combined as 400 nF; C13-C18 as 600 pF; C20-C26 as 382 pF; C27-C31 as 235 pF. C19 and C32 are DNP.
- The first MOSFET/gate amplifier and its resonant network are replaced by an ideal gate source: **0-12 V, 13.56 MHz, 50% duty measured between half-amplitude crossings, 2 ns rise/fall times**. This is an assumption, not a reconstruction of the real resonant gate waveform.
- The current transformer is replaced by a wire. The tip detector, feedback control, gate damping/clamps, buck dynamics, and cable transmission-line effects are omitted.
- Assumed series losses are 0.1 ohm for L3, 0.03 ohm for each filter inductor, and 0.01 ohm for each combined capacitor bank. These are explicit adjustable assumptions, not measured component losses.

Tip impedances come from [current-transformer.md, Iron Tip Model](../current-transformer.md#iron-tip-model), for the STTC-147. The complex impedance values are used directly, rather than their rounded equivalent components:

| Tip condition | Series equivalent used |
|---|---|
| Cold | 42.3 ohm + 152.582 nH |
| Warm | 55 ohm + 733.568 pF |
| Hot | 12 ohm + 281.690 nH |
| Open | 1 Tohm at the PCB output |

Inductance is calculated as `X/(2*pi*Freq)`; capacitance as `1/(2*pi*Freq*abs(X))`. To switch between RL and RC models without singular zero-valued components, the unused inductor is 1 pH with 1 micro-ohm series resistance, and the unused capacitor is bypassed by 1 micro-ohm. The active warm-tip capacitor has 1 Tohm leakage resistance.

Each tip condition is fixed throughout its simulation. In particular, the open case is **open from startup**, not a tip disconnected partway through an energized run. The lumped load matches the specified impedance at 13.56 MHz; its harmonic impedance is an assumption inherent in the series RL/RC model.

## MOSFET approximation and numerical checks

The inline `STP19NF20_EST` VDMOS model targets nominal values from the [local ST datasheet](../../3rd-party/datasheets/stb19nf20.pdf), also available from [STMicroelectronics](https://www.st.com/resource/en/datasheet/stb19nf20.pdf). Separate checks gave approximately 0.1105 ohm at VGS = 10 V / ID = 7.5 A, and Ciss = 803 pF, Coss = 168 pF, Crss = 29.5 pF at VDS = 25 V / VGS = 0 V / 1 MHz.

This checks a few nominal operating points only. Gate resistance, transconductance and body-diode behavior are estimates, and the nonlinear capacitances have not been validated across the full voltage swing. There is no package/PCB inductance, self-heating, tolerance sweep, or avalanche model. The real gate waveform and nonlinear output capacitance can materially change VDS, so the approximation should be replaced or checked against the real circuit before using the absolute peaks to select a TVS.

The four 30 V cases were rerun with a 100 ps maximum timestep:

| Tip condition at 30 V | Main run, 200 ps | Check, 100 ps |
|---|---:|---:|
| Cold | 216.5 V | 217.2 V |
| Warm | 121.0 V | 121.0 V |
| Hot | 361.1 V | 361.2 V |
| Open | 803.7 V | 785.6 V |

Loaded-case peak differences were at most 0.31%. A further 50 ps open-output run gave 787.3 V. This supports reporting the open-output result only as an approximate, finite-window unclamped result; a timestep check does not validate the device or circuit assumptions.

## Run again

Open `rf_amp.asc` in LTspice and select Run, or run this PowerShell command:

```powershell
Start-Process -FilePath 'C:\Users\frank\AppData\Local\Programs\ADI\LTspice\LTspice.exe' -ArgumentList '-b "D:\GithubRepos\Hot-Wand\doc\design-study\sim-13mhz\rf_amp.asc"' -WindowStyle Hidden -Wait
```

The active nested sweeps automatically run all 16 combinations:

```spice
.step param VCC list 15 20 25 30
.step param TipState list 1 2 3 4
```

Measurements are written to `rf_amp.log`. `V_DS_LAST` and `V_DS_PREV` compare the final two 20 us windows; `V_DS` is the full-run maximum used in the main table.


## With ES1J and SM15T150CA drain clamp

Simulated on 2026-09-07 with LTspice 26.0.2. Runnable schematic: [rf_amp_with_tvs_clamp.asc](rf_amp_with_tvs_clamp.asc). Full 16-case measurement log: [rf_amp_with_tvs_clamp.log](rf_amp_with_tvs_clamp.log).

This is a copy of the unclamped circuit above, with the drain-clamp branch restored according to [electrical/hot-wand.sch](../../../electrical/hot-wand.sch): **D4 (ES1J) anode at Vds, cathode at Clamp; TVS1 (bidirectional SM15T150CA) between Clamp and ground.** The MOSFET, gate drive, filter, load equivalents, losses, and simulation settings are unchanged. The added diode models are explicit datasheet-based approximations, not manufacturer SPICE models.

### Clamped maximum VDS

The same `.meas TRAN V_DS MAX V(Vds)` measures the full **0–1 ms** run, including startup, with a **200 ps** maximum timestep:

| Tip condition | VCC = 15 V | VCC = 20 V | VCC = 25 V | VCC = 30 V |
|---|---:|---:|---:|---:|
| Cold | 62.5 V | 107.5 V | 154.7 V | 163.0 V |
| Warm (below Curie) | 64.3 V | 83.4 V | 103.1 V | 123.9 V |
| Hot (above Curie) | 138.0 V | 162.6 V | 169.6 V | 178.4 V |
| Open output | 164.0 V | 184.4 V | 193.6 V | 191.5 V |

For comparison, the final 20 us maxima (`V_DS_LAST`) are:

| Tip condition | VCC = 15 V | VCC = 20 V | VCC = 25 V | VCC = 30 V |
|---|---:|---:|---:|---:|
| Cold | 62.3 V | 107.5 V | 154.4 V | 162.9 V |
| Warm (below Curie) | 64.3 V | 83.4 V | 103.1 V | 123.9 V |
| Hot (above Curie) | 136.4 V | 162.2 V | 169.0 V | 177.6 V |
| Open output | 160.5 V | 182.2 V | 192.7 V | 184.7 V |

The main sweep's largest peak is **193.6 V at 25 V with an open output**, but the finer-timestep 30 V open-output check below reaches **197.1 V**. Open-output full-run maxima are not monotonic with VCC in this finite-window simulation; do not infer that 30 V is safer than 25 V. The loaded cases have closely matching maxima in their last two measurement windows; the open cases retain envelope variation.

### TVS and rectifier loading

`P_TVS` is average power in the TVS's conducting branch over **980 us–1 ms**. Its separate ideal capacitance's reactive current is excluded:

| Tip condition | VCC = 15 V | VCC = 20 V | VCC = 25 V | VCC = 30 V |
|---|---:|---:|---:|---:|
| Cold | <0.001 W | <0.001 W | <0.001 W | 0.930 W |
| Warm (below Curie) | <0.001 W | <0.001 W | <0.001 W | <0.001 W |
| Hot (above Curie) | <0.001 W | 1.663 W | 9.140 W | 23.717 W |
| Open output | 0.085 W | 3.402 W | 9.886 W | 21.019 W |

The sub-milliwatt entries are modeled leakage, not significant avalanche clamping. Even then, the clamp branch can change VDS through its capacitance and rectification. Real parts near the lower breakdown limit may conduct in cases shown here as leakage-only; this upper-knee fit is not a worst-case heating calculation.

Additional measurements at **30 V**, from the main 200 ps sweep:

| Tip condition | Peak TVS conduction current, full run | Peak ES1J forward current, full run | ES1J average terminal power, final 20 us |
|---|---:|---:|---:|
| Cold | 0.572 A | 1.845 A | 0.132 W |
| Warm (below Curie) | <0.001 A | 0.246 A | 0.002 W |
| Hot (above Curie) | 2.738 A | 5.500 A | 0.574 W |
| Open output | 4.594 A | 5.538 A | 0.869 W |

ES1J also carries charging current for the TVS capacitance, so its peak current differs from the TVS conduction peak. The ES1J power measurement is average terminal power, an estimate of dissipation rather than a thermal model. `E_TVS` records full-run absorbed energy: **23.31 mJ for hot / 30 V** and **20.63 mJ for open / 30 V** over this 1 ms run.

The modeled clamp substantially reduces the excessive unclamped voltage, but **hot and open operation at 30 V impose about 24 W and 21 W of TVS dissipation**, respectively. These are substantial repetitive loads, not evidence of continuous safe operation. SM15T's 1500 W specification is a surge rating for a specified waveform, not a continuous dissipation rating. See the [ST SM15T datasheet](https://www.st.com/resource/en/datasheet/sm15t39ca.pdf).

### Clamp-model assumptions

- **TVS I–V fit:** the SM15T150CA datasheet specifies 128 V stand-off and 143–158 V breakdown at 1 mA (150 V typical). This model deliberately uses a **158 V knee**, plus **6.81 ohm** dynamic resistance, approximating the upper-voltage 10/1000 us curve: `V_TVS ≈ 158 + 6.81 × I_TVS`. A separate static check gave **207.04 V at 7.2 A**. This is not a validated RF model or a guaranteed worst-case RF clamp bound. Source: [ST SM15T datasheet](https://www.st.com/resource/en/datasheet/sm15t39ca.pdf).
- **TVS capacitance:** `Ctvs=200p` is an assumed constant lumped capacitance, not a measured or guaranteed value. Breakdown is modeled symmetrically; leakage is approximated by 1 Gohm. There is no temperature dependence, package inductance, or self-heating.
- **ES1J:** the junction model approximates 1.3 V at 1 A and 20 pF at 4 V; its static check gave **1.308 V at 1 A**. `TtES=15n` is an estimated SPICE transit time, **not** the datasheet's 35 ns reverse-recovery time. Recovery under the actual RF waveform has not been validated. Source: [Diodes Incorporated ES1J datasheet](https://www.diodes.com/datasheet/download/ES1J.pdf).

Both diode approximations and the original MOSFET model remain fixed at **25 C**, with no thermal failure or MOSFET avalanche. Real gate shape, parasitics, device tolerances, temperature rise, and diode recovery can change the result. In particular, **197 V is too close to the MOSFET's 200 V rating for these simulations to establish a safe margin**. The open case is open from startup; it does not test a mid-run tip disconnection or a firmware shutdown delay.

### Clamped timestep checks

The four 30 V cases were rerun at 100 ps; the open case was additionally rerun at 50 ps:

| Tip condition at 30 V | Main run, 200 ps | Check, 100 ps | Check, 50 ps |
|---|---:|---:|---:|
| Cold | 163.0 V | 163.1 V | Not run |
| Warm (below Curie) | 123.9 V | 123.8 V | Not run |
| Hot (above Curie) | 178.4 V | 178.4 V | Not run |
| Open output | 191.5 V | 196.4 V | 197.1 V |

The loaded-case peak differences between 200 ps and 100 ps are below 0.1%. The open-output peak is more sensitive: 191.5 → 196.4 → 197.1 V. At 50 ps its final-window maximum is 184.2 V, TVS average power is **21.08 W**, and ES1J average terminal power is **0.874 W**. These numerical checks support the broad comparison, not device-model accuracy or a guaranteed peak-voltage bound.

### Run the clamped version again

Open `rf_amp_with_tvs_clamp.asc` in LTspice and select Run, or use:

```powershell
Start-Process -FilePath 'C:\Users\frank\AppData\Local\Programs\ADI\LTspice\LTspice.exe' -ArgumentList '-b "D:\GithubRepos\Hot-Wand\doc\design-study\sim-13mhz\rf_amp_with_tvs_clamp.asc"' -WindowStyle Hidden -Wait
```

The active VCC and TipState sweeps run all 16 combinations. Measurements are written to `rf_amp_with_tvs_clamp.log`; `V_DS` remains the full-run maximum. The extra current, power, and energy directives are active in the schematic.
