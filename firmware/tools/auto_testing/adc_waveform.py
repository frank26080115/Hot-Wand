"""Plot programmed ADC FIFO playback without expanding repeat counts.

The FIFO is clocked by conversions, not a fixed wall-clock timer. The graph's
seconds therefore use measured cadence, while the sidecars preserve repeat
counts and inferred round offsets. These are decoded injection values, not filtered ADC getter samples.
Rendering happens after case cleanup so plotting cannot disturb the stimulus.
"""
from __future__ import annotations

import csv
import importlib.util
import json
import math
from pathlib import Path


MASK = 0xFFFFFFFF
HALF_COUNTER = 0x80000000
CHANNEL_LABELS = ("DC input", "Buck voltage", "Current", "NTC 1", "NTC 2", "MCU temperature", "VREFINT")
CHANNEL_KEYS = ("dc", "buck", "current", "ntc1", "ntc2", "mcu", "vrefint")


def save_adc_waveform(result, directory, halt_spans=()):
    """Save one case overview and a zoom of every committed FIFO batch.

    A complete case overview retains real gaps between enqueue operations and
    the final held sample through the last observation. Batch views make short
    trends readable even when the case also spends seconds on a fault screen.
    Queued data beyond the last observation is explicitly shaded as unobserved.
    """
    if not result.get("stimulus"):
        return None
    timeline = build_timeline(result, halt_spans)
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    stem = directory / "adc_waveform"
    _save_data(stem, timeline)
    _render(stem.with_suffix(".png"), result["id"], timeline)
    batches = []
    for batch in timeline["batches"]:
        # Include the previous frame just before activation to expose even a
        # single-frame spike/drop. The overview shows its complete held tail.
        span = batch["end_round"] - batch["start_round"]
        padding = max(1, span * 0.04)
        left = max(0, batch["start_round"] - padding)
        right = batch["end_round"] + padding
        name = f"adc_sequence_{batch['number']:02d}.png"
        _render(directory / name, result["id"], timeline, window=(left, right), batch=batch)
        batches.append({"image": f"{directory.name}/{name}", "number": batch["number"]})
    return {"image": f"{directory.name}/{stem.name}.png",
            "data": f"{directory.name}/{stem.name}.json",
            "csv": f"{directory.name}/{stem.name}.csv", "sequences": batches,
            "time_basis": timeline["time_basis"], "estimated_seconds_per_round": timeline["seconds_per_round"]}


def build_timeline(result, halt_spans=()):
    """Reconstruct FIFO order using published counters and remaining repeats.

    Counter arithmetic stays modulo 2**32. Each case is much shorter than half
    a counter wrap; observations before the first enqueue or after reset are
    excluded. An ordinary enqueue's activation is inferred to the next round
    boundary. The marked sustained-loss waveform has an exact firmware anchor.
    """
    entries = result["stimulus"]
    origin = entries[0]["before"]["rounds_completed"]
    boot_number = result.get("boot", {}).get("boot_number")
    observations = [o for o in result.get("observations", []) if "adc_progress" in o and
                    (boot_number is None or o.get("state", {}).get("boot_number") == boot_number)]
    cadence = result.get("cadence", {})
    if cadence.get("rounds", 0) > 0 and cadence.get("target_ms", 0) > 0:
        period = cadence["target_ms"] / (1000 * cadence["rounds"])
        basis = "Estimated seconds from this case's measured ADC rounds / target milliseconds"
    else:
        # Setup/fault screens do not publish the foreground target tick. ADC
        # progress still runs, so use host observations with debugger halts
        # removed. No nominal conversion-rate constant is substituted.
        period = _observed_period(entries, observations, halt_spans)
        basis = "Estimated seconds from observed ADC progress / host time, excluding debugger halts"
    if not math.isfinite(period) or period <= 0:
        raise ValueError("No valid measured ADC cadence is available for a time-axis plot")

    points = [{"round": 0, "actual": list(entries[0]["before"]["decoded"])}]
    known_frames, batches = {}, []
    for number, entry in enumerate(entries, 1):
        before = entry["before"]
        relative = (before["rounds_completed"] - origin) & MASK
        if relative >= HALF_COUNTER:
            raise ValueError("ADC sequence crosses an unsupported reset/half-counter interval")
        # consumer_count already excludes the current decoded frame. Unread
        # slots ahead of this batch must finish before its first slot is used.
        pending = sum(known_frames[(before["consumer_count"] + i) & MASK]["repeat_rounds"]
                      for i in range(before["queued"]))
        start = relative + max(1, before["rounds_remaining"]) + pending
        anchor = "inferred next boundary (within one ADC round)"
        if entry.get("mark"):
            for observation in observations:
                state = observation.get("state", {})
                if state.get("event_raw", 0) >> 16 == 3:
                    start = (state["qualify_ms"] - origin) & MASK
                    anchor = "firmware FT_MARK activation round"
                    break
        if batches and start < batches[-1]["end_round"]:
            # An inferred activation can differ by one boundary. Preserve the
            # FIFO's known order rather than drawing overlapping frame ranges.
            start = batches[-1]["end_round"]
        cursor = start
        for index, frame in enumerate(entry["frames"]):
            repeats = frame["repeat_rounds"]
            if repeats <= 0:
                raise ValueError("ADC graph frames must have positive repeat counts")
            actual = list(frame["actual"])
            if len(actual) != 7 or any(not 0 <= value <= 1023 for value in actual):
                raise ValueError("ADC graph requires seven decoded 10-bit values")
            points.append({"round": cursor, "actual": actual})
            cursor += repeats
            known_frames[(before["producer_count"] + index) & MASK] = frame
        # Do not expand repeats: uint32 durations and long holds require only
        # one point per frame, plus the frame's ending boundary.
        points.append({"round": cursor, "actual": list(entry["frames"][-1]["actual"])})
        batches.append({"number": number, "enqueue_round": relative,
                        "start_round": start, "end_round": cursor, "activation": anchor})

    observed = [(o["adc_progress"]["rounds_completed"] - origin) & MASK for o in observations]
    observed += [(e["before"]["rounds_completed"] - origin) & MASK for e in entries]
    observed_end = max((r for r in observed if r < HALF_COUNTER), default=0)
    end = max(points[-1]["round"], observed_end)
    if end > points[-1]["round"]:
        points.append({"round": end, "actual": list(points[-1]["actual"])})
    for point in points:
        point["time_seconds"] = point["round"] * period
    return {"schema_version": 1, "channels": list(CHANNEL_KEYS), "seconds_per_round": period,
            "time_basis": basis, "origin_round_counter": origin, "boot_number": boot_number,
            "observed_end_round": observed_end, "end_round": end, "batches": batches, "points": points,
            "note": "Programmed LUT-decoded counts before filtering; VREFINT retains its sparse sampling cadence. "
                    "Holds are inferred from FIFO playback and observed progress, not a sampled ADC trace."}


def check_plotting_available():
    """Fail preflight before connecting to hardware if a plot dependency is missing."""
    if importlib.util.find_spec("matplotlib") is None:
        raise RuntimeError("ADC waveform plots require matplotlib: py -m pip install matplotlib")


def _observed_period(entries, observations, halt_spans):
    samples = [(e["adc_host_time"], e["before"]["rounds_completed"])
               for e in entries if "adc_host_time" in e]
    samples += [(o["adc_host_time"], o["adc_progress"]["rounds_completed"])
                for o in observations if "adc_host_time" in o]
    samples.sort()
    elapsed = rounds = 0
    for (left, first), (right, last) in zip(samples, samples[1:]):
        delta = (last - first) & MASK
        if not 0 < delta < HALF_COUNTER:
            continue
        # Clip and merge halt intervals so nested/overlapping records cannot
        # subtract a pause twice. The transport normally logs outer halts only.
        intervals = sorted((max(left, h["host_start"]), min(right, h["host_start"] + h["seconds"]))
                           for h in halt_spans if h["host_start"] < right and
                           h["host_start"] + h["seconds"] > left)
        paused, previous = 0, left
        for start, end in intervals:
            paused += max(0, end - max(start, previous))
            previous = max(previous, end)
        active = right - left - paused
        if active > 0:
            elapsed += active
            rounds += delta
    if rounds == 0:
        raise ValueError("Cannot plot ADC time: no cadence or same-boot progress interval was captured")
    return elapsed / rounds


def _save_data(stem, timeline):
    stem.with_suffix(".json").write_text(json.dumps(timeline, indent=2) + "\n",
                                         encoding="utf-8", newline="\r\n")
    with stem.with_suffix(".csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("time_seconds_estimated", "round_offset", *CHANNEL_KEYS))
        for point in timeline["points"]:
            writer.writerow((point["time_seconds"], point["round"], *point["actual"]))


def _render(path, case_id, timeline, *, window=None, batch=None):
    # Direct Agg figures open no GUI and do not select/import camera tooling.
    # Import lazily after cleanup, outside all timing-sensitive case execution.
    from matplotlib.backends.backend_agg import FigureCanvasAgg
    from matplotlib.figure import Figure

    figure = Figure(figsize=(12, 12), dpi=120)
    FigureCanvasAgg(figure)
    axes = figure.subplots(7, 1, sharex=True)
    period = timeline["seconds_per_round"]
    left, right = window or (0, timeline["end_round"])
    display_points = list(timeline["points"])
    if display_points[-1]["round"] < right:
        # Held-last continues beyond the last planned repeat. In a batch zoom
        # its unobserved continuation remains inside the gray future region.
        display_points.append({"round": right, "time_seconds": right * period,
                               "actual": display_points[-1]["actual"]})
    x = [point["time_seconds"] for point in display_points]
    label = f"FIFO sequence {batch['number']} (zoom)" if batch else "case overview, including held-last periods"
    figure.suptitle(f"{case_id} — simulated ADC values\n{label}", fontsize=14)
    for channel, axis in enumerate(axes):
        values = [point["actual"][channel] for point in display_points]
        axis.step(x, values, where="post", linewidth=1.5, color=f"C{channel}")
        axis.set_ylabel(f"{CHANNEL_LABELS[channel]}\nADC counts", fontsize=9)
        axis.grid(True, alpha=0.25)
        visible = [values[i] for i, p in enumerate(display_points)
                   if left <= p["round"] <= right or
                   (p["round"] < left and (i == len(values) - 1 or display_points[i + 1]["round"] >= left))]
        if not visible:
            visible = values
        margin = max(4, (max(visible) - min(visible)) * 0.12)
        axis.set_ylim(max(0, min(visible) - margin), min(1023, max(visible) + margin))
        axis.ticklabel_format(axis="y", style="plain", useOffset=False)
        axis.set_xlim(left * period, right * period)
        for entry in timeline["batches"]:
            axis.axvline(entry["start_round"] * period, color="#888888", linewidth=0.7, linestyle=":")
        unobserved = max(left, timeline["observed_end_round"])
        if unobserved < right:
            axis.axvspan(unobserved * period, right * period, color="#777777", alpha=0.12)
    axes[-1].set_xlabel("Time from ADC round at first enqueue (s, estimated)")
    figure.text(0.5, 0.032, timeline["time_basis"], ha="center", fontsize=8)
    figure.text(0.5, 0.017, "Steps: decoded FIFO input, before filtering. Dotted: batch start. "
                "Gray: queued/unobserved future. Initial activation may differ by one round.", ha="center", fontsize=8)
    figure.subplots_adjust(left=0.14, right=0.98, top=0.92, bottom=0.085, hspace=0.25)
    figure.savefig(path)
    figure.clear()
