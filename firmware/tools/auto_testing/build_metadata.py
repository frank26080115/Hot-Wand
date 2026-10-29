"""Read the symbols and load ranges of a PlatformIO ARM ELF artifact."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
import subprocess


# Full installation paths are kept here so the test does not depend on PATH.
TOOLCHAIN_BIN = Path(r"C:\Users\frank\.platformio\packages\toolchain-gccarmnoneeabi\bin")
NM_EXE = TOOLCHAIN_BIN / "arm-none-eabi-nm.exe"
OBJDUMP_EXE = TOOLCHAIN_BIN / "arm-none-eabi-objdump.exe"
GDB_EXE = TOOLCHAIN_BIN / "arm-none-eabi-gdb.exe"
OPENOCD_EXE = Path(r"C:\Users\frank\.platformio\packages\tool-openocd\bin\openocd.exe")
OPENOCD_SCRIPTS = Path(r"C:\Users\frank\.platformio\packages\tool-openocd\openocd\scripts")
GPHOTO2_EXE = Path(r"C:\msys64\ucrt64\bin\gphoto2.exe")
# All automated test runners consume the same full-application artifact.
TEST_ENVIRONMENT = "stm32f042_test"
TEST_ELF = Path(__file__).resolve().parents[2] / ".pio" / "build" / TEST_ENVIRONMENT / "firmware.elf"
FLASH_START = 0x08000000
NVM_START = 0x08007C00
NVM_END = 0x08008000


@dataclass(frozen=True)
class Symbol:
    address: int
    kind: str
    name: str


@dataclass(frozen=True)
class MenuItem:
    title: str
    options: tuple[str, ...]


class BuildMetadata:
    """Resolve target addresses and reject an ELF that could erase NVM."""

    def __init__(self, elf: Path, *, required_symbols=None):
        self.required_symbols = required_symbols
        self.elf = elf.resolve()
        if not self.elf.is_file():
            raise RuntimeError(f"Build the requested PlatformIO environment first: {self.elf}")
        self.sha256 = hashlib.sha256(self.elf.read_bytes()).hexdigest()
        self.symbols = self._read_symbols()
        self.load_ranges = self._read_load_ranges()
        self._validate()

    def _tool(self, path: Path, *arguments: str) -> str:
        if not path.is_file():
            raise RuntimeError(f"Required executable is missing: {path}")
        result = subprocess.run([str(path), *arguments, str(self.elf)], capture_output=True, text=True, check=False)
        if result.returncode:
            raise RuntimeError(f"{path.name} failed: {result.stderr.strip()}")
        return result.stdout

    def _read_symbols(self) -> dict[str, Symbol]:
        symbols = {}
        for line in self._tool(NM_EXE, "-an").splitlines():
            match = re.fullmatch(r"([0-9a-fA-F]+)\s+([A-Za-z])\s+(\S+)", line.strip())
            if match:
                symbol = Symbol(int(match[1], 16), match[2], match[3])
                symbols[symbol.name] = symbol
        return symbols

    def _read_load_ranges(self) -> list[tuple[str, int, int]]:
        lines = self._tool(OBJDUMP_EXE, "-h").splitlines()
        ranges = []
        for index, line in enumerate(lines[:-1]):
            match = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)", line)
            if match and "LOAD" in lines[index + 1].split(", "):
                size, address = int(match[2], 16), int(match[3], 16)
                if size:
                    ranges.append((match[1], address, address + size))
        return ranges

    def _validate(self) -> None:
        required = self.required_symbols
        if required is None:
            required = ("btn_swd_test_command_ms", "btn_swd_test_state", "btn_swd_test_completed",
                        "vision_test_fiducial_press", "vision_test_oled_sent_generation",
                        "vision_test_oled_sent_framebuffer", "setup_menu_items", "setup_menu_item_count",
                        "btn_swd_test_boot_hold", "vision_test_fiducial_enabled",
                        "adc_sim", "adc_sim_decode_table", "tip_swd_test_command", "tip_swd_test_state",
                        "fault_test_dimming_enabled", "fault_test_inactive_ms", "fault_test_power_rise_enabled")
        for name in required:
            self.address(name)
        if not self.load_ranges or not any(FLASH_START <= start < NVM_START for _, start, _ in self.load_ranges):
            raise RuntimeError("ELF has no application flash load sections")
        for section, start, end in self.load_ranges:
            if FLASH_START <= start < NVM_END and end > NVM_START:
                raise RuntimeError(f"ELF section {section} reaches the reserved NVM page")

    def address(self, name: str) -> int:
        if name not in self.symbols:
            raise RuntimeError(f"Required symbol {name!r} is absent from {self.elf}")
        return self.symbols[name].address


def read_menu_items(target, table_address: int, count: int | None = None) -> list[MenuItem]:
    """Discover the compiled menu; test builds omit the ordinary splash page."""
    if count is None:
        count = target.read_u32(target.symbols["setup_menu_item_count"].address)
    if not 2 <= count <= 32:
        raise RuntimeError("Compiled menu item count is invalid")
    table = target.read_memory(table_address, count * 12)
    items = []
    for index in range(count):
        entry = table[index * 12 : (index + 1) * 12]
        title_ptr = int.from_bytes(entry[:4], "little")
        options_ptr = int.from_bytes(entry[4:8], "little")
        option_count = entry[8]
        title = target.read_c_string(title_ptr)
        choices = target.read_c_string(options_ptr).split("|") if option_count else []
        if len(choices) != option_count:
            raise RuntimeError(f"Compiled menu item {index} has inconsistent option count")
        items.append(MenuItem(title, tuple(choices)))
    if ([item.title for item in items[-3:-1]] != ["SAVE\nAND\nEXIT", "EXIT\nNO\nSAVE"]
            or items[-1].options):
        raise RuntimeError("Compiled menu action pages differ from the expected layout")
    return items


def latest_nvm_record(page: bytes, *, dimming_enabled=False) -> bytes | None:
    """Decode this build's journal; foreign layouts invalidate the whole page."""
    if len(page) != NVM_END - NVM_START:
        raise ValueError("Expected one complete 1 KiB NVM page")
    expected = 0xA8 if dimming_enabled else 0xA7
    latest = None
    foreign = False
    erased_slot_seen = False
    for offset in range(0, len(page) - 5, 6):
        record = page[offset:offset + 6]
        if record == b"\xff" * 6:
            erased_slot_seen = True
            continue
        a = b = 0
        for value in record[:4]:
            a = (a + value) % 255
            b = (b + a) % 255
        if int.from_bytes(record[4:6], "little") != (b << 8 | a):
            continue
        if record[0] in (0xA6, 0xA7, 0xA8) and record[0] != expected:
            foreign = True
        first, second, third = record[1:4]
        if (not erased_slot_seen and record[0] == expected and not second & 0xC0 and (first & 3) < 3 and
                ((second >> 3) & 7) < 7 and (third & 15) < 11 and
                (dimming_enabled or not first & 0xC0)):
            latest = record
    return None if foreign else latest


def settings_values(record: bytes | None, *, dimming_enabled=False) -> dict[str, int]:
    """Named page values avoid depending on optional pages' positions."""
    if record is not None and (len(record) != 6 or record[0] != (0xA8 if dimming_enabled else 0xA7)):
        raise ValueError("Settings record does not match the compiled dimming layout")
    first, second, third = record[1:4] if record is not None else (0, 1, 0x50)
    result = {"START\nPOWER\nLEVEL": first & 3, "FAN\nMODE": third >> 4,
              "FAN\nSIGNL\nPOLAR": (first >> (7 if dimming_enabled else 5)) & 1,
              "AUTO\nSLEEP": (first >> 2) & 3}
    if dimming_enabled:
        result["AUTO\nDIM"] = (first >> 4) & 3
    result.update({"IDLE\nD;ABS": second & 7, "BATT\nMODE": (second >> 3) & 7,
                   "INPUT\nVOLT\nCALIB": third & 15,
                   "SHOW\nSPLASH": (first >> (6 if dimming_enabled else 4)) & 1 if record is not None else 1})
    return result


def settings_indices(record: bytes | None, *, dimming_enabled=False) -> list[int]:
    return list(settings_values(record, dimming_enabled=dimming_enabled).values())


def menu_settings_indices(page: bytes, items) -> list[int]:
    enabled = any(item.title == "AUTO\nDIM" for item in items)
    values = settings_values(latest_nvm_record(page, dimming_enabled=enabled), dimming_enabled=enabled)
    result = []
    for item in items:
        if not item.options:
            continue
        value = values[item.title]
        if item.title == "FAN\nMODE" and len(item.options) == 4:
            value = {0: 0, 1: 1, 5: 2, 6: 3}.get(value, 0)
        result.append(value)
    return result
