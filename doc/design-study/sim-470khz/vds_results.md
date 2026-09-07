# Radio Thermal simulation: peak drain voltage

Source schematic: `from_discord_sept5_2026.asc`

LTspice 26.0.2 was used to evaluate the originally inactive directive:

```spice
.meas V_DS MAX(V(Vds))
```

The original transient analysis was retained:

```spice
.tran 0 1004.21u 1000u 5n startup
```

Consequently, each result is the maximum drain voltage during the final 4.21 us of the simulation, after 1 ms of startup time.

## Results

| Tip condition | Equivalent tip model | VDS max at VCC = 20 V | VDS max at VCC = 25 V | VDS max at VCC = 30 V |
|---|---:|---:|---:|---:|
| Cold tip (room temperature) | 11.6 ohm + 5.5 uH | 82.86 V | 103.58 V | 124.29 V |
| Warm tip (just below Curie) | 14.0 ohm + 7.2 uH | 93.27 V | 116.54 V | 139.80 V |
| Hot tip (above Curie) | 1.55 ohm + 1.70 uH | 72.65 V | 90.83 V | 109.01 V |
| No tip (tip removed) | 100 Mohm + 0 H | 101.01 V | 126.11 V | 151.19 V |

The highest simulated value is **151.19 V**, for no tip at 30 V input.

## Reproduction notes

- The source `.asc` file was not modified. The voltage sweep, enabled measurement, and tip-condition sweep were run from a temporary netlist derived from it.
- The visible schematic comment specifies 1.55 ohm for the hot tip, while the active `R_tip` parameter table in the source uses 1.54 ohm. These results follow the visible four-condition specification and use 1.55 ohm.
- The source represents the removed tip as 100 Mohm rather than mathematical infinity.
- Results reflect the supplied component models and nominal component values. They do not include additional PCB parasitics or component tolerances beyond anything already present in those models.
