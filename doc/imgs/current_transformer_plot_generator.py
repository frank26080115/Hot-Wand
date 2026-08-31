"""Generate explanatory waveforms for the current-transformer phase detector.

This is deliberately a logic-level model, not a circuit or SPICE simulation.  It
implements the simplified behavior described in design-study-current-transformer.md:

* D16 and D17 conduct while primary current flows right-to-left.
* During that interval, C47's quadrature current selects D21 or D22 and drives
  R40 to either polarity.
* The two diode drops clamp the R40 drive to approximately +/-2 V.
* R40 and C75 are represented by a first-order 100 ohm / 1 uF low-pass filter.

Run this file directly.  It writes 0.png, 15.png, ... 345.png beside the
script, in the current_transformer_plots directory.
"""

from __future__ import annotations

from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
from PIL import Image


RF_FREQUENCY_HZ = 13.56e6
VOLTAGE_PEAK_V = 50.0
CURRENT_PEAK_A = 1.0
NUMBER_OF_CYCLES = 4
SAMPLES_PER_CYCLE = 1_500

DIODE_FORWARD_VOLTAGE_V = 1.0
R40_OHMS = 100.0
C75_FARADS = 1.0e-6

PHASE_STEP_DEGREES = 15
OUTPUT_DIRECTORY = Path(__file__).resolve().parent.parent / "design-study" / "imgs" / "current_transformer_plots"
OUTPUT_DPI = 150
ANIMATION_FRAME_DURATION_MS = 150
ANIMATION_FILENAME = "animation.apng"

VOLTAGE_COLOR = "#1769aa"
CURRENT_COLOR = "#c62828"
FILTER_COLOR = "#6a1b9a"


def steady_state_low_pass(
    input_signal: np.ndarray,
    samples_per_cycle: int,
    sample_interval_s: float,
    time_constant_s: float,
) -> np.ndarray:
    """Apply a discrete exact RC step response with a periodic steady-state start.

    Solving the one-cycle recurrence for its fixed point avoids an artificial
    charge-up transient at the left side of every plot.
    """

    decay = np.exp(-sample_interval_s / time_constant_s)
    one_minus_decay = 1.0 - decay

    # Starting from zero, find the contribution made by one complete cycle.
    cycle_contribution = 0.0
    for sample in input_signal[:samples_per_cycle]:
        cycle_contribution = (
            decay * cycle_contribution + one_minus_decay * sample
        )

    cycle_decay = decay**samples_per_cycle
    initial_value = cycle_contribution / (1.0 - cycle_decay)

    output_signal = np.empty_like(input_signal, dtype=float)
    output_signal[0] = initial_value
    for index in range(1, input_signal.size):
        output_signal[index] = (
            decay * output_signal[index - 1]
            + one_minus_decay * input_signal[index - 1]
        )

    return output_signal


def build_waveforms(phase_degrees: int) -> dict[str, np.ndarray]:
    """Return the analog and idealized diode waveforms for one phase angle."""

    total_samples = NUMBER_OF_CYCLES * SAMPLES_PER_CYCLE
    phase_radians = np.deg2rad(phase_degrees)
    electrical_angle = np.linspace(
        0.0,
        NUMBER_OF_CYCLES * 2.0 * np.pi,
        total_samples + 1,
    )
    time_s = electrical_angle / (2.0 * np.pi * RF_FREQUENCY_HZ)

    voltage_v = VOLTAGE_PEAK_V * np.sin(electrical_angle)
    current_a = CURRENT_PEAK_A * np.sin(electrical_angle + phase_radians)

    # A capacitor's current is proportional to dV/dt, so it leads voltage by
    # 90 degrees.  Its plotted amplitude is normalized to the voltage amplitude
    # because this plot is intended to explain timing rather than C47 magnitude.
    c47_quadrature_v = VOLTAGE_PEAK_V * np.cos(electrical_angle)

    primary_reverse = current_a < 0.0
    c47_current_down = c47_quadrature_v >= 0.0

    # Idealized ring states.  D21/D22 carry the other transformer half-cycle;
    # while D16/D17 conduct, one of D21/D22 also conducts to clamp C47's
    # injected current.  That three-diode overlap selects the R40 polarity.
    d16_on = primary_reverse
    d17_on = primary_reverse
    d21_on = (~primary_reverse) | (primary_reverse & c47_current_down)
    d22_on = (~primary_reverse) | (primary_reverse & ~c47_current_down)

    clamp_voltage_v = 2.0 * DIODE_FORWARD_VOLTAGE_V
    r40_current_a = np.zeros_like(time_s)
    r40_current_a[primary_reverse & c47_current_down] = (
        clamp_voltage_v / R40_OHMS
    )
    r40_current_a[primary_reverse & ~c47_current_down] = (
        -clamp_voltage_v / R40_OHMS
    )

    sample_interval_s = time_s[1] - time_s[0]
    filtered_current_a = steady_state_low_pass(
        r40_current_a,
        SAMPLES_PER_CYCLE,
        sample_interval_s,
        R40_OHMS * C75_FARADS,
    )

    return {
        "time_s": time_s,
        "voltage_v": voltage_v,
        "current_a": current_a,
        "c47_quadrature_v": c47_quadrature_v,
        "d16_on": d16_on,
        "d17_on": d17_on,
        "d21_on": d21_on,
        "d22_on": d22_on,
        "r40_current_a": r40_current_a,
        "filtered_current_a": filtered_current_a,
    }


def draw_phase_plot(phase_degrees: int, output_path: Path) -> None:
    """Draw and save the three aligned panels for one current phase angle."""

    waveforms = build_waveforms(phase_degrees)
    time_ns = waveforms["time_s"] * 1.0e9
    period_ns = 1.0e9 / RF_FREQUENCY_HZ

    figure, (analog_axis, diode_axis, output_axis) = plt.subplots(
        3,
        1,
        figsize=(12.0, 8.0),
        sharex=True,
        gridspec_kw={"height_ratios": (2.2, 1.55, 1.55)},
    )
    figure.subplots_adjust(
        left=0.09,
        right=0.90,
        bottom=0.085,
        top=0.91,
        hspace=0.13,
    )
    figure.suptitle(
        "Current-transformer phase detector — "
        f"phase shift +{phase_degrees}° ",
        fontsize=14,
        y=0.975,
    )

    voltage_line = analog_axis.plot(
        time_ns,
        waveforms["voltage_v"],
        color=VOLTAGE_COLOR,
        alpha=0.90,
        linewidth=2.0,
        label="RF voltage",
    )[0]
    c47_line = analog_axis.plot(
        time_ns,
        waveforms["c47_quadrature_v"],
        color=VOLTAGE_COLOR,
        alpha=0.36,
        linestyle=":",
        linewidth=2.3,
        label="C47 quadrature (+90°, normalized)",
    )[0]
    analog_axis.set_ylabel("Voltage (V)", color=VOLTAGE_COLOR)
    analog_axis.tick_params(axis="y", colors=VOLTAGE_COLOR)
    analog_axis.set_ylim(-56.0, 56.0)
    analog_axis.set_yticks((-50, -25, 0, 25, 50))
    analog_axis.axhline(0.0, color="#666666", linewidth=0.7)
    analog_axis.set_title("RF voltage, primary current, and C47 quadrature")

    current_axis = analog_axis.twinx()
    current_line = current_axis.plot(
        time_ns,
        waveforms["current_a"],
        color=CURRENT_COLOR,
        alpha=0.90,
        linewidth=1.8,
        label="Primary current (left-to-right positive)",
    )[0]
    current_axis.set_ylabel("Primary current (A)", color=CURRENT_COLOR)
    current_axis.tick_params(axis="y", colors=CURRENT_COLOR)
    # Preserve the true +/-1 A values while rendering current at 95% of the
    # voltage trace's visual height, so in-phase traces remain distinguishable.
    current_visual_scale = 0.95
    voltage_visual_limit_ratio = VOLTAGE_PEAK_V / 56.0
    current_axis_limit = CURRENT_PEAK_A / (
        voltage_visual_limit_ratio * current_visual_scale
    )
    current_axis.set_ylim(-current_axis_limit, current_axis_limit)
    current_axis.set_yticks((-1.0, -0.5, 0.0, 0.5, 1.0))
    analog_axis.legend(
        handles=(voltage_line, current_line, c47_line),
        loc="upper right",
        ncol=3,
        fontsize=9,
        framealpha=0.92,
    )

    diode_states = (
        ("D22", waveforms["d22_on"], "#7b1fa2"),
        ("D21", waveforms["d21_on"], "#00897b"),
        ("D17", waveforms["d17_on"], "#ef6c00"),
        ("D16", waveforms["d16_on"], "#3949ab"),
    )
    row_height = 0.72
    for row, (_, state, color) in enumerate(diode_states):
        trace = row + row_height * state.astype(float)
        diode_axis.step(time_ns, trace, where="post", color=color, linewidth=1.8)
        diode_axis.fill_between(
            time_ns,
            row,
            trace,
            step="post",
            color=color,
            alpha=0.14,
        )

    diode_axis.set_ylim(-0.15, len(diode_states) - 1 + row_height + 0.18)
    diode_axis.set_yticks(
        [row + row_height / 2.0 for row in range(len(diode_states))],
        labels=[name for name, _, _ in diode_states],
    )
    diode_axis.set_ylabel("Diode")
    diode_axis.set_title("Idealized diode conduction states (high = ON)")
    diode_axis.axhline(0.0, color="#888888", linewidth=0.5)

    r40_current_ma = waveforms["r40_current_a"] * 1.0e3
    filtered_current_ma = waveforms["filtered_current_a"] * 1.0e3
    output_axis.step(
        time_ns,
        r40_current_ma,
        where="post",
        color="#37474f",
        linewidth=1.6,
        label="R40 current (logic model)",
    )
    output_axis.plot(
        time_ns,
        filtered_current_ma,
        color=FILTER_COLOR,
        linewidth=2.4,
        label="100 Ω / 1 µF LPF (steady state)",
    )
    output_axis.axhline(0.0, color="#666666", linewidth=0.7)
    output_axis.set_ylim(-22.5, 22.5)
    output_axis.set_yticks((-20, -10, 0, 10, 20))
    output_axis.set_ylabel("Current equivalent (mA)")
    output_axis.set_xlabel("Time (ns)")
    output_axis.set_title("R40 square-wave current and C75 low-pass result")
    output_axis.legend(loc="upper right", fontsize=9, framealpha=0.92)

    cycle_boundaries_ns = np.arange(NUMBER_OF_CYCLES + 1) * period_ns
    for axis in (analog_axis, diode_axis, output_axis):
        axis.set_xlim(0.0, NUMBER_OF_CYCLES * period_ns)
        axis.grid(axis="y", color="#b0bec5", alpha=0.38, linewidth=0.7)
        for boundary_ns in cycle_boundaries_ns:
            axis.axvline(
                boundary_ns,
                color="#90a4ae",
                alpha=0.38,
                linestyle="--",
                linewidth=0.8,
            )

    half_cycle_ticks_ns = np.arange(2 * NUMBER_OF_CYCLES + 1) * period_ns / 2.0
    output_axis.set_xticks(half_cycle_ticks_ns)
    output_axis.set_xticklabels(
        [f"{tick:.1f}" for tick in half_cycle_ticks_ns]
    )

    figure.savefig(output_path, dpi=OUTPUT_DPI, facecolor="white")
    plt.close(figure)


def write_apng(frame_paths: list[Path], output_path: Path) -> None:
    """Combine the phase plots into an infinitely looping APNG."""

    frames: list[Image.Image] = []
    for frame_path in frame_paths:
        with Image.open(frame_path) as frame:
            frames.append(frame.convert("RGB"))

    first_frame, *remaining_frames = frames
    first_frame.save(
        output_path,
        format="PNG",
        save_all=True,
        append_images=remaining_frames,
        duration=ANIMATION_FRAME_DURATION_MS,
        loop=0,
        disposal=0,
        blend=0,
        optimize=True,
    )

    for frame in frames:
        frame.close()


def main() -> None:
    """Generate every phase plot and combine them into a looping APNG."""

    OUTPUT_DIRECTORY.mkdir(parents=True, exist_ok=True)
    frame_paths: list[Path] = []
    for phase_degrees in range(0, 360, PHASE_STEP_DEGREES):
        output_path = OUTPUT_DIRECTORY / f"{phase_degrees}.png"
        draw_phase_plot(phase_degrees, output_path)
        frame_paths.append(output_path)
        print(f"Wrote {output_path}")

    animation_path = OUTPUT_DIRECTORY / ANIMATION_FILENAME
    write_apng(frame_paths, animation_path)
    print(f"Wrote {animation_path}")


if __name__ == "__main__":
    main()
