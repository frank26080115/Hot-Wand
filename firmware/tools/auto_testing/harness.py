"""Boot controls for the one stm32f042_test image, using ordinary SWD writes.

After reset the firmware waits at the ADC FIFO gate, feeding its watchdog with
RF off and forced minimum output. Set the optional button hold/camera gate
before requesting ADC start. A plain ADC producer can instead enqueue its own
sequence and start directly; default controls enter normal supervision.
"""

from __future__ import annotations

import struct
import time

try:
    from .adc_sim import AdcSimProducer, Frame, FRAME_SIZE
except ImportError:
    from adc_sim import AdcSimProducer, Frame, FRAME_SIZE


# STM32F042 factory calibration words: 12-bit readings at VDDA=3.3 V, MCU=30 C.
MCU_CALIBRATION = 0x1FFFF7B8
VREF_CALIBRATION = 0x1FFFF7BA


def start_test_boot(target, symbols, *, setup: bool = False, fiducial: bool = False):
    """Release a fresh boot gate with a held-last baseline; return its encoding.

    Used after *every* reset by the setup runner, including save/discard resets.
    The baseline is only a menu stimulus (about 24 V, zero current, cool NTCs),
    not a fault-test sequence. Long-running fault tests supply their own frames.
    """
    target.resume()
    # Reset does not clear SRAM; startup clears BSS progressively. Let it run
    # before polling, rather than interpreting the previous boot's nonzero ABI
    # word as readiness while its other header fields are being cleared.
    time.sleep(0.05)
    deadline = time.monotonic() + 5
    header = None
    while time.monotonic() < deadline:
        # Read a coherent complete header under one halt. The firmware publishes
        # ABI version 1 last, after capacity/frame-size/channel-count are ready.
        header = target.inspect(lambda: struct.unpack("<4I", target.read_memory(symbols["adc_sim"].address, 16)))
        version, capacity, frame_size, count = header
        if (version == 1 and frame_size == FRAME_SIZE and count == 7 and
                2 <= capacity <= 65536 and capacity & (capacity - 1) == 0):
            break
        time.sleep(0.02)
    else:
        raise RuntimeError(f"Shared test build did not reach its ADC boot gate; observed header {header}")
    producer = AdcSimProducer(target, symbols)

    def configure():
        status = producer.status()
        if status["state"] != 0 or status["queued"] != 0:
            raise RuntimeError("Test boot configuration requires a fresh, empty ADC boot gate")
        temp = int.from_bytes(target.read_memory(MCU_CALIBRATION, 2), "little")
        vref = int.from_bytes(target.read_memory(VREF_CALIBRATION, 2), "little")
        if not 100 <= temp <= 4095 or not 100 <= vref <= 4095:
            raise RuntimeError("Invalid STM32 factory calibration; cannot supply menu baseline")
        # Match the firmware's 10-bit sampling; encoding reports the actual
        # logarithmic values, which may differ slightly from these requests.
        frame = Frame((676, 676, 0, 870, 870, min(1023, (temp + 2) // 4), min(1023, (vref + 2) // 4)))
        target.write_u32(symbols["btn_swd_test_boot_hold"].address, int(setup))
        target.write_u32(symbols["vision_test_fiducial_enabled"].address, int(fiducial))
        report = producer.enqueue([frame])
        producer.start()  # Commit the start mailbox after options and payload.
        return report

    report = target.inspect(configure)
    # A completed sent frame also proves btn_init() ran. Its state word is
    # BOOT_HELD even before initialization, so that word alone is insufficient.
    generation_address = symbols["vision_test_oled_sent_generation"].address
    deadline = time.monotonic() + 10
    while True:
        generation = target.read_u32(generation_address)
        if generation != 0 and generation % 2 == 0:
            return report
        if time.monotonic() >= deadline:
            raise RuntimeError("Shared test build did not send its first OLED frame")
        time.sleep(0.02)
