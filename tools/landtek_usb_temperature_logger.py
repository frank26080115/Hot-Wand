#!/usr/bin/env python3
"""Poll a LANDTEK THE-373 four-channel thermometer; print/log CSV and plot live.

Install: python -m pip install pyserial matplotlib
Examples:
  python tools/landtek_usb_temperature_logger.py
  python tools/landtek_usb_temperature_logger.py --port COM20 --log-file temperatures.csv
  python tools/landtek_usb_temperature_logger.py --no-plot --interval 2 --duration 600
  python tools/landtek_usb_temperature_logger.py --device-units F --plot-samples 600

IMPORTANT: the response contains no units flag. Set the front panel to Celsius
(default assumption), or specify --device-units F to convert Fahrenheit to C.
Do not change front-panel units during a session. Open probes are blank CSV
fields and NaN plot gaps. Failed polls are reported to stderr, not fabricated
as measurements. Three consecutive failures after startup terminate the run;
a failed first poll is fatal. Only the first matching CH340 is tried.

CSV is local date/time, followed by four Celsius temperatures at one decimal.
Each CSV line is also printed to stdout. Every file write is flushed and fsynced.
Existing log files are refused, not overwritten. Omit --log-file to save nothing.
Closing the plot or pressing Ctrl-C stops acquisition and closes all resources.
The resizable Matplotlib window has its standard Save image toolbar button.
Acquisition runs separately from the GUI, so using its Save dialog does not pause
CSV logging. Both the displayed history and pending GUI updates are bounded.

Protocol source: user-supplied reverse engineering notes (polling only).
Request AA 55 01 03 03; response 55 AA 01 0B, four signed LE int16 values,
checksum=sum(first 12 bytes)&255. Raw 28000 means open. The example in those
notes ends in 8C, but its calculated checksum is 5C; enforce the stated rule.
No configuration commands or internal-log download commands are sent.
"""
from __future__ import annotations

import argparse
from collections import deque
from contextlib import ExitStack
from datetime import datetime
import math
import os
from pathlib import Path
import queue
import struct
import sys
import threading
import time

REQUEST = bytes.fromhex("AA 55 01 03 03")
HEADER = bytes.fromhex("55 AA 01 0B")
CSV_HEADER = "date,time,ch1,ch2,ch3,ch4"
VID, PID = 0x1A86, 0x7523
OPEN_CHANNEL = 28000
REPLY_TIMEOUT = 0.5
MAX_CONSECUTIVE_FAILURES = 3


class ProtocolError(Exception):
    """A complete valid measurement was not received."""


def positive_seconds(value: str) -> float:
    try:
        result = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"expected seconds as a number, got {value!r}") from exc
    if not math.isfinite(result) or result <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero seconds")
    return result


def positive_integer(value: str) -> int:
    try:
        result = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"expected a whole number of samples, got {value!r}") from exc
    if result <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero samples")
    return result


def decode_reply(frame: bytes, units: str = "C") -> tuple[float | None, ...]:
    if len(frame) != 13:
        raise ProtocolError(f"expected 13 bytes, received {len(frame)}")
    if frame[:4] != HEADER:
        raise ProtocolError(f"invalid header: {frame[:4].hex(' ')}")
    expected = sum(frame[:12]) & 0xFF
    if frame[12] != expected:
        raise ProtocolError(f"checksum {frame[12]:02X}, expected {expected:02X}")
    if units not in ("C", "F"):
        raise ValueError("device units must be C or F")
    readings = []
    for raw in struct.unpack("<4h", frame[4:12]):
        if raw == OPEN_CHANNEL:
            readings.append(None)
        else:
            value = raw / 10
            readings.append((value - 32) * 5 / 9 if units == "F" else value)
    return tuple(readings)


def poll(port, units: str = "C", timeout: float = REPLY_TIMEOUT):
    """Discard stale input, send one request, and resync within a bounded read.

    Scan for the header after noise, partial packets or bad checksums. A failed
    candidate advances one byte, allowing an overlapping valid frame to survive.
    A timeout's leftovers are discarded before the next request. Serial reads
    block for at most 50 ms at a time, keeping Ctrl-C responsive.
    """
    port.reset_input_buffer()
    if port.write(REQUEST) != len(REQUEST):
        raise ProtocolError("short serial write of polling request")
    deadline = time.monotonic() + timeout
    buffer = bytearray()
    last_error = "no complete reply"
    while time.monotonic() < deadline:
        port.timeout = min(0.05, max(0, deadline - time.monotonic()))
        chunk = port.read(max(1, min(port.in_waiting, 256)))
        buffer.extend(chunk)
        while True:
            position = buffer.find(HEADER)
            if position < 0:
                # Keep a possible split header, discarding unframed noise.
                if len(buffer) > 3:
                    last_error = "received bytes without the expected header"
                    del buffer[:-3]
                break
            del buffer[:position]
            if len(buffer) < 13:
                last_error = f"partial reply ({len(buffer)}/13 bytes)"
                break
            try:
                return decode_reply(bytes(buffer[:13]), units)
            except ProtocolError as exc:
                last_error = str(exc)
                del buffer[0]
    raise ProtocolError(f"reply timeout after {timeout:.3f}s: {last_error}; "
                        f"remaining bytes: {buffer.hex(' ') or '(none)'}")


def select_port(explicit: str | None, ports=None) -> str:
    if explicit:
        return explicit
    if ports is None:
        from serial.tools import list_ports
        ports = list_ports.comports()
    for candidate in ports:
        if candidate.vid == VID and candidate.pid == PID:
            return candidate.device
    raise RuntimeError("no CH340 serial port with VID=1A86 PID=7523 found; "
                       "connect the thermometer or specify --port")


def csv_row(timestamp: datetime, readings) -> str:
    fields = [timestamp.strftime("%m/%d/%Y"), timestamp.strftime("%H:%M:%S")]
    fields.extend("" if value is None else f"{value:.1f}" for value in readings)
    return ",".join(fields)


def emit(line: str, logfile=None):
    if logfile is not None:
        logfile.write(line + "\n")
        logfile.flush()
        os.fsync(logfile.fileno())
    print(line, flush=True)


class LivePlot:
    def __init__(self, samples: int, interval: float, *, show: bool = True):
        import matplotlib.pyplot as plt
        self.plt = plt
        self.interval = interval
        self.times = deque(maxlen=samples)
        self.channels = [deque(maxlen=samples) for _ in range(4)]
        self.closed = False
        # Standard GUI managers provide resize, pan/zoom and Save image.
        with plt.rc_context({"toolbar": "toolbar2"}):
            self.figure, self.axes = plt.subplots(figsize=(10, 5), dpi=100)
        if show and self.figure.canvas.required_interactive_framework is None:
            plt.close(self.figure)
            raise RuntimeError("Matplotlib has no interactive GUI backend; install Tk/Qt "
                               "or run with --no-plot")
        self.figure.canvas.manager.set_window_title("LANDTEK thermocouple logger")
        self.figure.canvas.mpl_connect("close_event", self._on_close)
        self.lines = [self.axes.plot([], [], color=color, label=f"CH{i + 1}",
                                     marker=".", markersize=3)[0]
                      for i, color in enumerate(("tab:blue", "tab:orange", "tab:green", "tab:red"))]
        self.axes.set(xlabel="Elapsed time (seconds)", ylabel="Temperature (°C)",
                      title=f"Latest {samples} samples — open probes are gaps",
                      ylim=(0, 100), xlim=(0, max(interval, 1)))
        self.axes.grid(True, alpha=0.3)
        self.axes.legend(loc="upper left")
        self.figure.tight_layout()
        if show:
            plt.show(block=False)
            plt.pause(0.001)

    def _on_close(self, event):
        self.closed = True

    def add(self, elapsed: float, readings):
        self.times.append(elapsed)
        for channel, value in zip(self.channels, readings):
            channel.append(math.nan if value is None else value)
        for line, channel in zip(self.lines, self.channels):
            line.set_data(list(self.times), list(channel))
        left, right = self.times[0], self.times[-1]
        self.axes.set_xlim(left, right if right > left else left + max(self.interval, 1))
        values = [v for v in readings if v is not None]
        if values:
            low, high = self.axes.get_ylim()
            smallest, largest = min(values), max(values)
            padding = max(1, (max(high, largest) - min(low, smallest)) * 0.05)
            if smallest < low:
                low = smallest - padding
            if largest > high:
                high = largest + padding
            self.axes.set_ylim(low, high)
        self.figure.canvas.draw_idle()

    def wait(self, seconds: float):
        if not self.closed:
            self.plt.pause(max(seconds, 0.001))

    def close(self):
        self.closed = True
        self.plt.close(self.figure)


def collect(port, *, interval: float, duration: float | None, units: str,
            logfile=None, plot=None):
    emit(CSV_HEADER, logfile)
    start = time.monotonic()
    stop = math.inf if duration is None else start + duration
    next_poll = start
    samples, failures = 0, 0
    while time.monotonic() < stop and not (plot and plot.closed):
        now = time.monotonic()
        if now < next_poll:
            wait = min(0.05, next_poll - now, stop - now)
            plot.wait(wait) if plot else time.sleep(wait)
            continue
        try:
            readings = poll(port, units, min(REPLY_TIMEOUT, stop - now))
        except ProtocolError as exc:
            failures += 1
            print(f"Warning: poll failed: {exc}", file=sys.stderr, flush=True)
            if samples == 0 or failures >= MAX_CONSECUTIVE_FAILURES:
                raise RuntimeError(f"{port.port}: no valid startup reply or "
                                   f"{MAX_CONSECUTIVE_FAILURES} consecutive failed polls") from exc
        else:
            failures = 0
            emit(csv_row(datetime.now(), readings), logfile)
            samples += 1
            if plot and not plot.closed:
                plot.add(time.monotonic() - start, readings)
        # Monotonic start-to-start schedule; skip missed slots, never catch up
        # with a burst of rapid duplicate requests after a timeout/GUI pause.
        next_poll += interval
        now = time.monotonic()
        if next_poll < now:
            next_poll += (math.floor((now - next_poll) / interval) + 1) * interval
        if plot:
            plot.wait(0.001)
    return samples


def collect_live(port, plot, *, interval, duration, units, logfile=None):
    """Keep serial/file I/O off the GUI thread, including during modal Save dialogs."""
    stop_event = threading.Event()
    updates = queue.Queue(maxsize=plot.times.maxlen)
    result = []
    errors = []

    class PlotMailbox:
        @property
        def closed(self):
            return stop_event.is_set()

        def wait(self, seconds):
            stop_event.wait(seconds)

        def add(self, elapsed, readings):
            # A modal GUI dialog can prevent redraws for arbitrarily long periods.
            # Drop only obsolete display updates, never the logged measurements.
            if updates.full():
                try:
                    updates.get_nowait()
                except queue.Empty:
                    pass
            updates.put_nowait((elapsed, readings))

    def acquire():
        try:
            result.append(collect(port, interval=interval, duration=duration,
                                  units=units, logfile=logfile, plot=PlotMailbox()))
        except Exception as exc:
            errors.append(exc)

    worker = threading.Thread(target=acquire, name="landtek-acquisition")
    worker.start()
    try:
        while not plot.closed:
            while True:
                try:
                    elapsed, readings = updates.get_nowait()
                except queue.Empty:
                    break
                plot.add(elapsed, readings)
            if not worker.is_alive() and updates.empty():
                break
            plot.wait(0.05)
    finally:
        stop_event.set()
        # The serial read/write timeouts are bounded. Join before closing the
        # serial port or logfile; never tear them down under the acquisition thread.
        worker.join()
    if errors:
        raise errors[0]
    return result[0] if result else 0


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="e.g. COM20; default first USB VID=1A86 PID=7523")
    parser.add_argument("--log-file", "--log", type=Path, help="new CSV file; default no file")
    parser.add_argument("--interval", "--sample-interval", type=positive_seconds, default=1.0,
                        help="poll interval in seconds (default 1)")
    parser.add_argument("--duration", "--session-duration", type=positive_seconds,
                        help="session duration in seconds (default unlimited)")
    parser.add_argument("--no-plot", action="store_true", help="console/log only; no Matplotlib needed")
    parser.add_argument("--plot-samples", type=positive_integer, default=300,
                        help="maximum plotted samples, not seconds (default 300)")
    parser.add_argument("--device-units", type=str.upper, choices=("C", "F"), default="C",
                        help="must match front panel; output is always Celsius (default C)")
    args = parser.parse_args(argv)
    if args.interval < 0.5:
        print("Warning: polling faster than 2 Hz can return repeated readings.", file=sys.stderr)
    try:
        import serial
        with ExitStack() as stack:
            port_name = select_port(args.port)
            # Configure before opening: do not deliberately assert modem lines.
            port = serial.Serial(port=None, baudrate=9600, bytesize=serial.EIGHTBITS,
                                 parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE,
                                 timeout=0.05, write_timeout=0.5,
                                 xonxoff=False, rtscts=False, dsrdtr=False)
            stack.callback(port.close)
            port.dtr = False
            port.rts = False
            port.port = port_name
            port.open()
            logfile = None
            if args.log_file:
                logfile = stack.enter_context(args.log_file.expanduser().open("x", encoding="utf-8", newline=""))
            plot = None if args.no_plot else LivePlot(args.plot_samples, args.interval)
            if plot:
                stack.callback(plot.close)
            print(f"Connected to {port_name} at 9600 8N1. Front panel MUST be {args.device_units}; "
                  "CSV/plot are Celsius. Ctrl-C or close plot to stop.", file=sys.stderr, flush=True)
            settings = dict(interval=args.interval, duration=args.duration,
                            units=args.device_units, logfile=logfile)
            samples = collect_live(port, plot, **settings) if plot else collect(port, **settings)
            print(f"Stopped: {samples} samples.", file=sys.stderr)
        return 0
    except KeyboardInterrupt:
        print("\nStopped by Ctrl-C.", file=sys.stderr)
        return 0
    except ImportError as exc:
        print(f"error: {exc}. Install dependencies: python -m pip install pyserial matplotlib", file=sys.stderr)
        return 1
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
