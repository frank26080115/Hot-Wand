"""Versioned fault-test ABI and state/ADC control over the shared GDB session.

Only exported ABI symbols and documented STM32 register addresses are used.
No GDB target function calls or writes to optimized implementation variables.
"""
from __future__ import annotations

from dataclasses import dataclass
import struct
import time

try:
    from .adc_sim import AdcSimProducer, Frame
    from .harness import MCU_CALIBRATION, VREF_CALIBRATION
except ImportError:
    from adc_sim import AdcSimProducer, Frame
    from harness import MCU_CALIBRATION, VREF_CALIBRATION

FAULT_ABI_VERSION = 2  # Requires the shared build with physical RF/buck inhibition.
BOOT, ADC, OLED, SETUP, RUNTIME, FAULT, BATTERY, RESUMING = range(1, 9)
COMMANDS = {"hang": 1, "hardfault": 2, "clock": 3, "tip_storm": 4, "mark": 5,
            "oscillator": 10, "clock_switch": 11, "clock_ready": 12,
            "adc_init": 20, "adc_calibration": 21, "adc_channel": 22, "adc_vref": 23, "adc_start": 24,
            "oled_init": 30, "graphics": 31, "tip_base": 32, "tip_pulse": 33, "nvm_save": 40}
HEAD = ("abi", "command", "ack", "stage", "reset_flags", "message_address", "tick", "trip_ms", "trip_round",
        "trip_generation", "rf_attempts", "rf_at_fault", "qualify_ms", "setup")
BYTE_FIELDS = ("cells", "mode", "power", "reasons", "pwm", "flags", "tip_burst_remaining", "reserved")
# STM32F042 memory map; GPIO offsets MODER/ODR and TIM offsets CR1/CCER/BDTR.
REGISTERS = {"gpioa_mode": 0x48000000, "gpioa_odr": 0x48000014, "gpioa_afrh": 0x48000024,
             "gpiob_mode": 0x48000400, "gpiob_odr": 0x48000414,
             "tim1_cr1": 0x40012C00, "tim1_ccer": 0x40012C20, "tim1_bdtr": 0x40012C44,
             "tim3_ccr1": 0x40000434, "icsr": 0xE000ED04}


@dataclass(frozen=True)
class BootSettings:
    mode: int = 0
    cells: int = 0  # Zero runs the actual battery guess.
    power: int = 0
    calibration: int = 0
    auto_sleep: int = 0
    auto_dim: int = 0  # Reserved/ignored by OLED_DIMMING_ENABLED=0 firmware.
    tip_absent: bool = False
    idle_profile: int | None = None  # None keeps the saved/factory profile.

    def pack(self):
        for name, maximum in (("mode", 6), ("cells", 255), ("power", 2), ("calibration", 10),
                              ("auto_sleep", 3), ("auto_dim", 3)):
            value = getattr(self, name)
            if type(value) is not int or not 0 <= value <= maximum:
                raise ValueError(f"Invalid {name}: {value}")
        if self.idle_profile is not None and (type(self.idle_profile) is not int or not 0 <= self.idle_profile <= 7):
            raise ValueError(f"Invalid idle_profile: {self.idle_profile}")
        idle_override = 0 if self.idle_profile is None else (1 << 25) | (self.idle_profile << 22)
        if self.mode == 0 and self.cells != 0:
            raise ValueError("Disabled battery mode requires zero cells")
        return (0x80000000 | self.cells | self.mode << 8 | self.power << 11 | self.calibration << 13 |
                self.auto_sleep << 17 | self.auto_dim << 19 | int(self.tip_absent) << 21 | idle_override)


class AbiNotReady(RuntimeError):
    pass


class FaultTarget:
    def __init__(self, session, metadata):
        self.session = session
        self.symbols = metadata.symbols
        self.address = metadata.address("fault_test")
        for name in ("adc_sim", "adc_sim_decode_table", "vision_test_oled_sent_generation",
                     "vision_test_oled_sent_framebuffer", "btn_swd_test_boot_hold", "tip_swd_test_command"):
            metadata.address(name)
        self.dimming_enabled = bool(session.read_u32(metadata.address("fault_test_dimming_enabled")))
        self.power_rise_enabled = bool(session.read_u32(metadata.address("fault_test_power_rise_enabled")))
        self.buck_voltage_spike_adc_counts = session.read_u32(metadata.address("fault_test_buck_voltage_spike_adc_counts"))
        self.producer = None
        self.boot_number = 0
        self.last_state = None

    def status(self, *, registers=False):
        def read():
            blob = self.session.read_memory(self.address, 128)
            result = dict(zip(HEAD, struct.unpack_from("<14I", blob)))
            result.update(zip(BYTE_FIELDS, blob[108:116]))
            if result["abi"] == 0:
                raise AbiNotReady("Fault ABI is not initialized during reset")
            if result["abi"] != FAULT_ABI_VERSION:
                raise RuntimeError("Fault ABI is incompatible")
            if result["flags"] & 128:
                return None
            result["filtered"] = struct.unpack_from("<6H", blob, 56)
            result["history"] = struct.unpack_from("<7H", blob, 68)
            result["raw_trigger_frame"] = struct.unpack_from("<7H", blob, 82)
            result["energy_uj"] = struct.unpack_from("<I", blob, 96)[0]
            result.update(zip(("dc_mv", "current_ma", "temperature_c", "reference_mv"),
                              struct.unpack_from("<4H", blob, 100)))
            result.update(zip(("event_ms", "event_round", "event_raw"), struct.unpack_from("<3I", blob, 116)))
            result["message"] = (self.session.read_c_string(result["message_address"], 80)
                                 if result["message_address"] else "")
            result["boot_number"] = self.boot_number
            result["generation"] = self.session.read_u32(self.symbols["vision_test_oled_sent_generation"].address)
            result["inactive_ms"] = self.session.read_u32(self.symbols["fault_test_inactive_ms"].address)
            if registers:
                result["registers"] = {name: self.session.read_u32(address) for name, address in REGISTERS.items()}
                result["inside_isr"] = bool(result["registers"]["icsr"] & 0x1FF)
            return result
        for _ in range(30):
            result = self.session.inspect(read)
            if result is not None:
                self.last_state = result
                return result
            time.sleep(0.01)
        raise RuntimeError("Fault snapshot remained busy")

    def wait(self, predicate, *, timeout=10, description="expected state", interval=0.12):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                value = self.status()
            except AbiNotReady:
                time.sleep(interval)
                continue
            if predicate(value):
                return value
            time.sleep(interval)
        raise TimeoutError(f"Timed out waiting for {description}; last state: {self.last_state}")

    def wait_boot_gate(self, *, timeout=10):
        # Reset/flash can move the ABI object to RAM that still contains the
        # old image's data. Do not parse a full snapshot (or its pointers)
        # until startup clears BSS and both interfaces publish readiness.
        # Incompatibility remains a hard error in ordinary status() reads.
        self.session.resume()
        time.sleep(0.05)
        deadline = time.monotonic() + timeout
        last_header = None
        while time.monotonic() < deadline:
            def read():
                head = self.session.read_memory(self.address, 16)
                abi, command, ack, stage = struct.unpack("<4I", head)
                adc_abi = self.session.read_u32(self.symbols["adc_sim"].address)
                return abi, stage, adc_abi
            last_header = self.session.inspect(read)
            if last_header == (FAULT_ABI_VERSION, BOOT, 1):
                return
            time.sleep(0.03)
        raise TimeoutError("Firmware did not initialize the shared test boot gate; "
                           f"expected fault ABI/stage/ADC ABI {(FAULT_ABI_VERSION, BOOT, 1)}, "
                           f"observed {last_header}")

    def command(self, name):
        value = COMMANDS[name]
        def commit():
            if self.session.read_u32(self.address + 4):
                raise RuntimeError("Unconsumed fault-test command")
            self.session.write_u32(self.address + 8, 0)
            self.session.write_u32(self.address + 4, value)
        self.session.inspect(commit)
        return value

    def boot(self, frame, *, settings=BootSettings(), setup=False, fiducial=False, injection=None, reset=True):
        if reset:
            self.session.reset()
        self.boot_number += 1
        self.wait_boot_gate()
        self.producer = AdcSimProducer(self.session, self.symbols)
        state = self.producer.status()
        if state["state"] != 0 or state["queued"]:
            raise RuntimeError("Fresh boot did not empty the ADC FIFO")
        def commit():
            self.session.write_u32(self.address + 52, settings.pack() if settings is not None else 0)
            self.session.write_u32(self.symbols["btn_swd_test_boot_hold"].address, int(setup))
            self.session.write_u32(self.symbols["vision_test_fiducial_enabled"].address, int(fiducial))
            if injection:
                self.command(injection)
            encoding = self.producer.enqueue([frame])
            self.producer.start()
            return encoding
        return self.session.inspect(commit)

    def frame(self, after_generation=0, *, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            def read():
                address = self.symbols["vision_test_oled_sent_generation"].address
                before = self.session.read_u32(address)
                if not before or before & 1 or before == after_generation:
                    return None
                data = self.session.read_memory(self.symbols["vision_test_oled_sent_framebuffer"].address, 512)
                after = self.session.read_u32(address)
                return (after, data) if before == after else None
            result = self.session.inspect(read)
            if result:
                return result
            time.sleep(0.03)
        raise TimeoutError("NO_FRAME: no new coherent sent framebuffer in this boot")

    def expect_reset(self, *, timeout=9, watchdog=False):
        def ready(state):
            if state["stage"] != BOOT:
                return False
            adc = self.producer.status()
            return adc["abi_version"] == 1 and adc["state"] == 0 and adc["queued"] == 0
        state = self.wait(ready, timeout=timeout, description="reset and empty boot gate")
        if watchdog and not state["reset_flags"] & (1 << 29):
            raise AssertionError("Reset occurred without the IWDG reset-cause flag")
        self.boot_number += 1
        state["boot_number"] = self.boot_number
        return state

    def calibration(self):
        def read():
            temp = int.from_bytes(self.session.read_memory(MCU_CALIBRATION, 2), "little")
            vref = int.from_bytes(self.session.read_memory(VREF_CALIBRATION, 2), "little")
            identity = self.session.read_memory(0x1FFFF7AC, 12).hex()
            if not 100 <= temp <= 4095 or not 100 <= vref <= 4095:
                raise RuntimeError("Invalid factory ADC calibration")
            return {"mcu_cal": temp, "vref_cal": vref, "device_uid": identity}
        return self.session.inspect(read)
