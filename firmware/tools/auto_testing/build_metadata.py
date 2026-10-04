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

    def __init__(self, elf: Path):
        self.elf = elf.resolve()
        if not self.elf.is_file():
            raise RuntimeError(f"Build the stm32f042_setup_swd environment first: {self.elf}")
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
        for name in ("btn_swd_test_command_ms", "btn_swd_test_state", "btn_swd_test_completed",
                     "vision_test_fiducial_press", "vision_test_oled_sent_generation",
                     "vision_test_oled_sent_framebuffer", "setup_menu_items"):
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


def read_menu_items(target, table_address: int, count: int = 11) -> list[MenuItem]:
    """Read 32-bit pointers and option counts from compiled setup_menu_items."""
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
    if [item.title for item in items[-2:]] != ["SAVE\nAND\nEXIT", "EXIT\nNO\nSAVE"]:
        raise RuntimeError("Compiled menu layout differs from the expected 11-item PWM build")
    return items


def latest_nvm_record(page: bytes) -> bytes | None:
    """Return the newest valid six-byte journal record, or None for defaults."""
    if len(page) != NVM_END - NVM_START:
        raise ValueError("Expected one complete 1 KiB NVM page")
    latest = None
    for offset in range(0, len(page) - 5, 6):
        record = page[offset : offset + 6]
        if record == b"\xff" * 6 or record[0] != 0xA6 or record[2] & 0xC0:
            continue
        # The target stores fletcher16 over its first four packed bytes.
        sum1 = sum2 = 0
        for value in record[:4]:
            sum1 = (sum1 + value) % 255
            sum2 = (sum2 + sum1) % 255
        first, second, third = record[1:4]
        valid_fields = ((first & 3) < 3 and (second & 7) < 7 and
                        ((second >> 3) & 7) < 7 and (third & 15) < 11)
        if valid_fields and int.from_bytes(record[4:6], "little") == (sum2 << 8 | sum1):
            latest = record
    return latest


def settings_indices(record: bytes | None) -> list[int]:
    """Decode option indices in the compiled menu's PWM page order."""
    if record is None:
        return [0, 5, 0, 0, 0, 3, 0, 0, 1]
    if len(record) != 6:
        raise ValueError("NVM record must be six bytes")
    first, second, third = record[1:4]
    return [first & 3, third >> 4, first >> 7, (first >> 2) & 3,
            (first >> 4) & 3, second & 7, (second >> 3) & 7,
            third & 15, (first >> 6) & 1]
