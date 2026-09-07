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
