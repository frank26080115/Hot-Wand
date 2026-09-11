"""Render actual LTspice binary transient data. Requires numpy and matplotlib.

Run LTspice on startup_rf.cir and startup_supply_ramp.cir first, then run this file.
"""
from pathlib import Path
import json
import re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

HERE = Path(__file__).resolve().parent
PERIOD = 2.13e-6


def read_raw(path):
    blob = path.read_bytes()
    marker = "Binary:\n".encode("utf-16-le")
    header, payload = blob.split(marker, 1)
    header = header.decode("utf-16-le")
    names = re.findall(r"^\s*\d+\s+(\S+)\s+\S+\s*$", header, re.M)
    count = int(re.search(r"No. Points:\s*(\d+)", header)[1])
    flags = re.search(r"Flags: (.*)", header)[1]
    if "real" not in flags or "FastAccess" in flags or "double" in flags:
        raise ValueError("Expected ordinary real, mixed-precision LTspice binary RAW")
    dtype = np.dtype([("time", "<f8"), ("values", "<f4", (len(names)-1,))])
    if len(payload) != count * dtype.itemsize:
        raise ValueError("RAW record size does not match its header")
    data = np.frombuffer(payload, dtype=dtype, count=count)
    return {names[0].lower(): data["time"], **{
        n.lower(): data["values"][:, i].astype(float)
        for i, n in enumerate(names[1:])}}


def cycles(t, current, drain):
    rows = []
    for k in range(int(t[-1] / PERIOD)):
        a, b = k * PERIOD, (k+1) * PERIOD
        inside = (t > a) & (t < b)
        tt = np.r_[a, t[inside], b]
        ii = np.interp(tt, t, current)
        vv = np.interp(tt, t, drain)
        rows.append((k+1, (a+b)/2, np.trapezoid(ii, tt)/PERIOD, vv.max()))
    return np.array(rows)


def render(stem, ramp=False):
    d = read_raw(HERE / f"{stem}.raw")
    t, gate, drain, current = d["time"], d["v(vgs)"], d["v(vds)"], -d["i(vcc)"]
    stats = cycles(t, current, drain)
    reference = stats[-10:, 2:].mean(axis=0)
    errors = np.abs(stats[:, 2:] / reference - 1)
    acceptable = (errors <= .01).all(axis=1)
    stays = np.logical_and.accumulate(acceptable[::-1])[::-1]
    settled = int(stats[np.flatnonzero(stays)[0], 0]) if stays.any() else None
    result = dict(steady_current_A=reference[0], steady_vds_peak_V=reference[1],
                  first_cycle_current_A=stats[0, 2], cycle_30_current_A=stats[29, 2],
                  settled_cycle_1pct=settled, settled_start_us=(settled-1)*2.13 if settled else None,
                  startup_vds_max_V=float(drain[t <= 30*PERIOD].max()))
    np.savetxt(HERE / f"{stem}_cycles.csv", stats * [1, 1e6, 1, 1], delimiter=",",
               header="cycle,midpoint_us,mean_supply_current_A,peak_VDS_V", comments="",
               fmt=["%d", "%.6f", "%.9g", "%.9g"])
    plt.rcParams.update({"font.size": 11, "font.family": "DejaVu Sans"})
    fig, ax = plt.subplots(figsize=(14, 5.8), layout="constrained")
    fig.patch.set_facecolor("#0b1018")
    ax.set_facecolor("#0b1018")
    right = ax.twinx()
    mask = t <= 30*PERIOD
    ax.plot(t[mask]*1e6, drain[mask], color="#a298ff", lw=1.15, label=r"Drain $V_{DS}$")
    ax.plot(t[mask]*1e6, gate[mask], color="#55e56b", lw=1.1, label=r"Gate $V_{GS}$")
    if ramp:
        ax.plot(t[mask]*1e6, d["v(vdc)"][mask], color="#ed7777", lw=1, label="Supply voltage")
    right.plot(t[mask]*1e6, current[mask], color="#ffb52e", lw=1.3, label=r"Supply draw $-I(Vcc)$")
    right.plot(stats[:30, 1]*1e6, stats[:30, 2], color="#ffe3a6", ls="--", lw=1.4,
               label="Current: average per RF cycle")
    right.axhline(reference[0], color="#ffe3a6", lw=.7, alpha=.4)
    ax.set(xlim=(0, 63.9), ylim=(-5, 105), xlabel="Time from RF turn-on (µs)", ylabel="Voltage (V)")
    right.set(ylabel="Supply current (A)", ylim=(-.15, max(current[mask].max()*1.15, 3)))
    ax.set_xticks(np.arange(0, 64, 5))
    ax.grid(color="#ffffff", alpha=.12)
    for axis in (ax, right):
        axis.tick_params(colors="#dbe4f1")
        axis.xaxis.label.set_color("#dbe4f1")
        axis.yaxis.label.set_color("#dbe4f1")
        for spine in axis.spines.values():
            spine.set_color("#586477")
    top = ax.secondary_xaxis("top", functions=(lambda us: us/2.13, lambda n: n*2.13))
    top.set_xlabel("RF cycles", color="#dbe4f1")
    top.set_xticks(np.arange(0, 31, 5))
    top.tick_params(colors="#dbe4f1")
    lines = ax.get_lines() + right.get_lines()[:2]
    legend = ax.legend(lines, [line.get_label() for line in lines], loc="upper right",
                       facecolor="#101a29", edgecolor="#586477", fontsize=10, ncol=2)
    for label in legend.get_texts():
        label.set_color("#eef4ff")
    kind = "20 µs supply ramp (LTspice startup)" if ramp else "RF enabled with 21 V supply established"
    fig.suptitle(f"470 kHz RF startup — {kind}\nCold tip: 11.6 Ω + 5.5 µH  |  30 cycles = 63.9 µs", color="#eef4ff", fontsize=15)
    fig.savefig(HERE / f"{stem}.png", dpi=160)
    plt.close(fig)
    return result


if __name__ == "__main__":
    results = {stem: render(stem, ramp) for stem, ramp in
               [("startup_rf", False), ("startup_supply_ramp", True)]}
    (HERE / "startup_metrics.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results, indent=2))
