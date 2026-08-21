#!/usr/bin/env python3
"""Check EAGLE package orientations against EasyEDA/JLCPCB orientations.

The schematic may identify a JLCPCB part with JLCPCBPART, LCSC, or LCSC_PART.
JLCPARTNUM is also accepted because it is used by this project's JLCPCB exporter.
For each selected schematic part, the corresponding EAGLE package is read from
the board (when available) or from the schematic's embedded library.

This is deliberately a conservative heuristic.  Two-contact packages are
compared by their pad axis and, when EasyEDA supplies FD/RD polarity, by their
polarity direction.  Packages with more contacts are compared by the position
of pin 1.  Anything that cannot be inferred safely is reported as a warning.

EasyEDA naming reference:
https://docs.easyeda.com/en/PCBLib/PCBLib-Naming-Rule/index.html
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import sys
import time
import xml.etree.ElementTree as ET
from collections.abc import Callable, Mapping
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, TextIO
from urllib.error import HTTPError
from urllib.request import OpenerDirector, Request, build_opener


DEFAULT_SCHEMATIC = Path(__file__).resolve().parents[1] / "hot-wand.sch"
DEFAULT_CACHE = Path(__file__).resolve().parent / "jlc_rotation_cache.csv"
CACHE_FIELDS = ("part_number", "package_title")
API_URL = "https://easyeda.com/api/products/{part_number}/components?version=6.4.19.5"
USER_AGENT = "Hot-Wand-JLC-Rotation-Checker/1.0"
MAX_RESPONSE_BYTES = 8 * 1024 * 1024
TRANSIENT_HTTP_STATUS = {403, 408, 425, 429, 500, 502, 503, 504}
PART_NUMBER_ATTRIBUTES = ("JLCPCBPART", "LCSC", "LCSC_PART", "JLCPARTNUM")
LCSC_PART_PATTERN = re.compile(r"C[1-9][0-9]*", re.IGNORECASE)
ORIENTATION_TOKEN_PATTERN = re.compile(
    r"(?:^|[-_])(TL|TR|BL|BR|L|R|T|B)(?=$|[-_])",
    re.IGNORECASE,
)
POLARITY_TOKEN_PATTERN = re.compile(
    r"(?:^|[-_])(FD|RD|BI)(?=$|[-_])",
    re.IGNORECASE,
)
STANDARD_TWO_CONTACT_PATTERN = re.compile(
    r"^(?:R|C|L|F|D|LED)[0-9]{4}(?:$|[-_])",
    re.IGNORECASE,
)
EXPLICIT_LENGTH_PATTERN = re.compile(
    r"(?:^|[-_])L[0-9]+(?:\.[0-9]+)?(?=$|[-_])",
    re.IGNORECASE,
)
ANGLE_BY_TOKEN = {
    "R": 0,
    "TR": 45,
    "T": 90,
    "TL": 135,
    "L": 180,
    "BL": 225,
    "B": 270,
    "BR": 315,
}


class RotationCheckerError(RuntimeError):
    """A user-facing schematic, board, cache, or lookup error."""


class RetryableLookupError(RotationCheckerError):
    """A transient EasyEDA error which may succeed on a later attempt."""

    def __init__(self, message: str, retry_after_seconds: float | None = None):
        super().__init__(message)
        self.retry_after_seconds = retry_after_seconds


@dataclass(frozen=True)
class Contact:
    name: str
    x: float
    y: float


@dataclass
class PartRecord:
    reference: str
    part_numbers: tuple[str, ...]
    package_library: str = ""
    package_name: str = ""
    package: ET.Element | None = None
    pin_by_pad: dict[str, str] = field(default_factory=dict)
    current_rotation: int | None = None
    current_rotation_text: str = "0"
    warnings: list[str] = field(default_factory=list)


@dataclass(frozen=True)
class OrientationExpectation:
    part_number: str
    title: str
    allowed_rotations: frozenset[int]
    explanation: str


@dataclass(frozen=True)
class ReportRow:
    status: str
    reference: str
    part_numbers: str
    package: str
    easyeda_package: str
    current_rotation: str
    suggested_rotation: str
    details: str


def positive_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number") from exc
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("must be a finite number greater than zero")
    return parsed


def nonnegative_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number") from exc
    if not math.isfinite(parsed) or parsed < 0:
        raise argparse.ArgumentTypeError("must be a finite non-negative number")
    return parsed


def nonnegative_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if parsed < 0:
        raise argparse.ArgumentTypeError("must not be negative")
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "schematic",
        nargs="?",
        type=Path,
        default=DEFAULT_SCHEMATIC,
        help=f"EAGLE schematic (default: {DEFAULT_SCHEMATIC})",
    )
    parser.add_argument(
        "--board",
        type=Path,
        help="corresponding EAGLE board (default: schematic path with .brd suffix)",
    )
    parser.add_argument(
        "--cache",
        type=Path,
        default=DEFAULT_CACHE,
        help=f"CSV cache for successful EasyEDA package titles (default: {DEFAULT_CACHE})",
    )
    parser.add_argument(
        "--timeout",
        type=positive_float,
        default=15.0,
        metavar="SECONDS",
        help="timeout for each EasyEDA request (default: 15)",
    )
    parser.add_argument(
        "--retries",
        type=nonnegative_int,
        default=2,
        metavar="COUNT",
        help="retries after transient request failures (default: 2)",
    )
    parser.add_argument(
        "--delay",
        type=nonnegative_float,
        default=1.0,
        metavar="SECONDS",
        help="minimum delay between uncached EasyEDA requests (default: 1)",
    )
    return parser.parse_args()


def read_eagle_xml(path: Path, description: str) -> ET.Element:
    if not path.is_file():
        raise FileNotFoundError(f"{description} not found: {path}")
    try:
        return ET.parse(path).getroot()
    except ET.ParseError as exc:
        raise RotationCheckerError(
            f"invalid XML in {description} {path}: {exc}"
        ) from exc


def attributes(element: ET.Element) -> dict[str, str]:
    result: dict[str, str] = {}
    for attribute in element.findall("attribute"):
        name = attribute.get("name")
        if name:
            result[name.upper()] = attribute.get("value", "")
    return result


def selected_part_number(
    part_attributes: Mapping[str, str],
) -> tuple[str | None, list[str]]:
    values = [
        (name, part_attributes[name].strip())
        for name in PART_NUMBER_ATTRIBUTES
        if name in part_attributes
    ]
    if not values:
        return None, []

    warnings: list[str] = []
    nonempty_values = [(name, value) for name, value in values if value]
    if not nonempty_values:
        return "", ["JLCPCB/LCSC part-number attribute is empty"]

    selected_name, selected_value = nonempty_values[0]
    for name, value in nonempty_values[1:]:
        if value.upper() != selected_value.upper():
            warnings.append(
                f"conflicting part-number attributes: {selected_name}={selected_value!r}, "
                f"{name}={value!r}"
            )
    return selected_value, warnings


def parse_part_numbers(value: str) -> tuple[tuple[str, ...], list[str]]:
    if not value:
        return (), []

    raw_numbers = value.split(";")
    warnings: list[str] = []
    if any(not number.strip() for number in raw_numbers):
        warnings.append(f"empty alternative in part-number value {value!r}")

    part_numbers: list[str] = []
    for raw_number in raw_numbers:
        part_number = raw_number.strip().upper()
        if not part_number:
            continue
        if LCSC_PART_PATTERN.fullmatch(part_number) is None:
            warnings.append(f"invalid LCSC part number {part_number!r}")
            continue
        if part_number not in part_numbers:
            part_numbers.append(part_number)
    return tuple(part_numbers), warnings


def parse_rotation(value: str, source: str) -> tuple[int | None, str | None]:
    text = value.strip()
    if not text:
        return None, f"empty JLC_ROTATION on {source}"
    try:
        numeric = float(text)
    except ValueError:
        return None, f"invalid JLC_ROTATION {value!r} on {source}"
    if not math.isfinite(numeric) or not numeric.is_integer():
        return None, f"JLC_ROTATION must be an integer on {source}, got {value!r}"
    return int(numeric) % 360, None


def library_map(root: ET.Element, design_kind: str) -> dict[str, ET.Element]:
    libraries = root.find(f"./drawing/{design_kind}/libraries")
    if libraries is None:
        return {}
    return {
        library.get("name", ""): library
        for library in libraries.findall("library")
        if library.get("name") is not None
    }


def package_map(
    libraries: Mapping[str, ET.Element],
) -> dict[tuple[str, str], ET.Element]:
    result: dict[tuple[str, str], ET.Element] = {}
    for library_name, library in libraries.items():
        packages = library.find("packages")
        if packages is None:
            continue
        for package in packages.findall("package"):
            package_name = package.get("name")
            if package_name is not None:
                result[(library_name, package_name)] = package
    return result


def find_schematic_device(
    part: ET.Element,
    schematic_libraries: Mapping[str, ET.Element],
) -> ET.Element | None:
    library = schematic_libraries.get(part.get("library", ""))
    if library is None:
        return None
    deviceset_name = part.get("deviceset", "")
    device_name = part.get("device", "")
    for deviceset in library.findall("./devicesets/deviceset"):
        if deviceset.get("name") != deviceset_name:
            continue
        for device in deviceset.findall("./devices/device"):
            if device.get("name", "") == device_name:
                return device
    return None


def pin_map_for_device(device: ET.Element | None) -> dict[str, str]:
    result: dict[str, str] = {}
    if device is None:
        return result
    for connect in device.findall("./connects/connect"):
        pin = connect.get("pin", "").strip()
        for pad in connect.get("pad", "").split():
            if pad and pin:
                result[pad.upper()] = pin
    return result


def board_elements(root: ET.Element | None) -> dict[str, ET.Element]:
    if root is None:
        return {}
    elements = root.find("./drawing/board/elements")
    if elements is None:
        return {}
    return {
        element.get("name", ""): element
        for element in elements.findall("element")
        if element.get("name") is not None
    }


def load_parts(
    schematic_root: ET.Element,
    board_root: ET.Element | None,
) -> list[PartRecord]:
    schematic_libraries = library_map(schematic_root, "schematic")
    schematic_packages = package_map(schematic_libraries)
    board_libraries = library_map(board_root, "board") if board_root is not None else {}
    board_packages = package_map(board_libraries)
    elements = board_elements(board_root)

    parts_parent = schematic_root.find("./drawing/schematic/parts")
    if parts_parent is None:
        raise RotationCheckerError("schematic has no parts section")

    records: list[PartRecord] = []
    for part in parts_parent.findall("part"):
        part_attributes = attributes(part)
        part_number_value, part_warnings = selected_part_number(part_attributes)
        if part_number_value is None:
            continue

        part_numbers, number_warnings = parse_part_numbers(part_number_value)
        reference = part.get("name", "<unnamed>")
        record = PartRecord(reference=reference, part_numbers=part_numbers)
        record.warnings.extend(part_warnings)
        record.warnings.extend(number_warnings)

        schematic_rotation_text = part_attributes.get("JLC_ROTATION", "0")
        schematic_rotation, rotation_warning = parse_rotation(
            schematic_rotation_text,
            f"schematic part {reference}",
        )
        if rotation_warning:
            record.warnings.append(rotation_warning)

        device = find_schematic_device(part, schematic_libraries)
        record.pin_by_pad = pin_map_for_device(device)

        element = elements.get(reference)
        if board_root is not None and element is None:
            record.warnings.append("part has no corresponding board element")

        if element is not None:
            record.package_library = element.get("library", "")
            record.package_name = element.get("package", "")
            record.package = board_packages.get(
                (record.package_library, record.package_name)
            )
            if record.package is None:
                record.warnings.append(
                    f"board package {record.package_library}/{record.package_name} "
                    "was not found"
                )

            element_attributes = attributes(element)
            board_part_number, _ = selected_part_number(element_attributes)
            if board_part_number is None:
                record.warnings.append(
                    "board element has no JLCPCB/LCSC part-number attribute"
                )
            elif board_part_number.strip().upper() != part_number_value.strip().upper():
                record.warnings.append(
                    "schematic and board part-number attributes differ "
                    f"({part_number_value!r} versus {board_part_number!r})"
                )

            board_rotation_text = element_attributes.get("JLC_ROTATION", "0")
            board_rotation, board_rotation_warning = parse_rotation(
                board_rotation_text,
                f"board element {reference}",
            )
            if board_rotation_warning:
                record.warnings.append(board_rotation_warning)
            if (
                schematic_rotation is not None
                and board_rotation is not None
                and schematic_rotation != board_rotation
            ):
                record.warnings.append(
                    "schematic and board JLC_ROTATION values differ "
                    f"({schematic_rotation_text!r} versus {board_rotation_text!r})"
                )
            record.current_rotation = board_rotation
            record.current_rotation_text = board_rotation_text
        else:
            record.current_rotation = schematic_rotation
            record.current_rotation_text = schematic_rotation_text

        if record.package is None:
            schematic_library = part.get("library", "")
            schematic_package = device.get("package", "") if device is not None else ""
            record.package_library = record.package_library or schematic_library
            record.package_name = record.package_name or schematic_package
            record.package = schematic_packages.get(
                (schematic_library, schematic_package)
            )
            if record.package is None:
                record.warnings.append(
                    "could not resolve the EAGLE package from the board or schematic"
                )

        if not record.part_numbers and not number_warnings:
            record.warnings.append("no usable LCSC part number")
        records.append(record)

    return records


def load_title_cache(path: Path) -> dict[str, str]:
    if not path.exists() or path.stat().st_size == 0:
        return {}
    try:
        with path.open(encoding="utf-8-sig", newline="") as stream:
            reader = csv.DictReader(stream)
            if tuple(reader.fieldnames or ()) != CACHE_FIELDS:
                expected = ",".join(CACHE_FIELDS)
                raise RotationCheckerError(
                    f"cache {path} must have the CSV header {expected!r}"
                )

            result: dict[str, str] = {}
            for line_number, row in enumerate(reader, start=2):
                part_number = (row.get("part_number") or "").strip().upper()
                title = (row.get("package_title") or "").strip()
                if LCSC_PART_PATTERN.fullmatch(part_number) is None or not title:
                    raise RotationCheckerError(
                        f"cache {path} has an invalid row at line {line_number}"
                    )
                previous = result.get(part_number)
                if previous is not None and previous != title:
                    raise RotationCheckerError(
                        f"cache {path} has conflicting rows for {part_number}"
                    )
                result[part_number] = title
    except RotationCheckerError:
        raise
    except (OSError, UnicodeDecodeError, csv.Error) as exc:
        raise RotationCheckerError(f"could not read cache {path}: {exc}") from exc
    return result


class TitleCache:
    """Append successful lookups to a flushed CSV file as they arrive."""

    def __init__(self, path: Path):
        self.path = path
        self.titles = load_title_cache(path)
        self.stream: TextIO | None = None
        self.writer: csv.DictWriter | None = None

    def __enter__(self) -> TitleCache:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        needs_header = not self.path.exists() or self.path.stat().st_size == 0
        try:
            self.stream = self.path.open("a", encoding="utf-8", newline="")
            self.writer = csv.DictWriter(
                self.stream,
                fieldnames=CACHE_FIELDS,
                lineterminator="\n",
            )
            if needs_header:
                self.writer.writeheader()
                self.stream.flush()
        except OSError as exc:
            raise RotationCheckerError(
                f"could not open cache {self.path} for writing: {exc}"
            ) from exc
        return self

    def __exit__(self, *_: object) -> None:
        if self.stream is not None:
            self.stream.close()

    def add(self, part_number: str, title: str) -> None:
        previous = self.titles.get(part_number)
        if previous is not None:
            if previous != title:
                raise RotationCheckerError(
                    f"cache has conflicting titles for {part_number}"
                )
            return
        if self.stream is None or self.writer is None:
            raise RotationCheckerError("cache is not open for writing")
        try:
            self.writer.writerow({"part_number": part_number, "package_title": title})
            self.stream.flush()
        except (OSError, csv.Error) as exc:
            raise RotationCheckerError(
                f"could not append {part_number} to cache {self.path}: {exc}"
            ) from exc
        self.titles[part_number] = title


@dataclass
class ProgressReporter:
    total: int
    complete: int
    stream: TextIO = sys.stderr

    def display(self) -> None:
        print(
            f"\r{self.complete} out of {self.total} complete",
            end="",
            file=self.stream,
            flush=True,
        )

    def advance(self, _part_number: str) -> None:
        self.complete += 1
        self.display()

    def finish(self) -> None:
        print(file=self.stream, flush=True)


def decode_json_response(response: Any, part_number: str) -> Mapping[str, Any]:
    body = response.read(MAX_RESPONSE_BYTES + 1)
    if len(body) > MAX_RESPONSE_BYTES:
        raise RotationCheckerError(
            f"EasyEDA response was unexpectedly large for {part_number}"
        )
    try:
        payload = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RotationCheckerError(
            f"EasyEDA returned invalid JSON for {part_number}"
        ) from exc
    if not isinstance(payload, Mapping):
        raise RotationCheckerError(
            f"EasyEDA returned an unexpected JSON value for {part_number}"
        )
    return payload


def response_message(payload: Mapping[str, Any]) -> str:
    for key in ("message", "msg"):
        value = payload.get(key)
        if isinstance(value, str) and value.strip():
            return value.strip()
    return "no error message"


def extract_package_title(payload: Mapping[str, Any], part_number: str) -> str:
    if payload.get("success") is not True:
        code = payload.get("code")
        raise RotationCheckerError(
            f"EasyEDA API error {code!r} for {part_number}: {response_message(payload)}"
        )
    result = payload.get("result")
    if not isinstance(result, Mapping):
        raise RotationCheckerError(f"EasyEDA returned no result for {part_number}")
    package_detail = result.get("packageDetail")
    if not isinstance(package_detail, Mapping):
        raise RotationCheckerError(
            f"EasyEDA result has no packageDetail object for {part_number}"
        )
    title = package_detail.get("title")
    if not isinstance(title, str) or not title.strip():
        raise RotationCheckerError(
            f"EasyEDA packageDetail.title is empty for {part_number}"
        )
    return title.strip()


def parse_retry_after(error: HTTPError) -> float | None:
    value = error.headers.get("Retry-After")
    if value is None:
        return None
    try:
        seconds = float(value)
    except ValueError:
        return None
    if not math.isfinite(seconds) or seconds < 0:
        return None
    return min(seconds, 30.0)


class EasyEdaClient:
    def __init__(
        self,
        timeout: float,
        retries: int,
        delay: float,
        cached_titles: Mapping[str, str] | None = None,
        on_title: Callable[[str, str], None] | None = None,
        on_lookup_complete: Callable[[str], None] | None = None,
    ):
        self.timeout = timeout
        self.retries = retries
        self.delay = delay
        self.opener: OpenerDirector = build_opener()
        self.titles = dict(cached_titles or {})
        self.errors: dict[str, str] = {}
        self.last_request_started: float | None = None
        self.on_title = on_title
        self.on_lookup_complete = on_lookup_complete

    def throttle(self) -> None:
        if self.last_request_started is None:
            return
        remaining = self.delay - (time.monotonic() - self.last_request_started)
        if remaining > 0:
            time.sleep(remaining)

    def request_once(self, part_number: str) -> str:
        self.throttle()
        self.last_request_started = time.monotonic()
        request = Request(
            API_URL.format(part_number=part_number),
            method="GET",
            headers={"Accept": "application/json", "User-Agent": USER_AGENT},
        )
        try:
            with self.opener.open(request, timeout=self.timeout) as response:
                payload = decode_json_response(response, part_number)
        except HTTPError as exc:
            message = f"EasyEDA returned HTTP {exc.code} for {part_number}"
            if exc.code in TRANSIENT_HTTP_STATUS:
                retry_after = parse_retry_after(exc)
                if retry_after is None and exc.code == 403:
                    # EasyEDA uses 403, rather than only 429, for rate limiting.
                    retry_after = 10.0
                raise RetryableLookupError(
                    message,
                    retry_after_seconds=retry_after,
                ) from exc
            raise RotationCheckerError(message) from exc
        except OSError as exc:
            raise RetryableLookupError(
                f"EasyEDA request failed for {part_number}: {exc}"
            ) from exc
        return extract_package_title(payload, part_number)

    def get_package_title(self, part_number: str) -> str:
        if part_number in self.titles:
            return self.titles[part_number]
        if part_number in self.errors:
            raise RotationCheckerError(self.errors[part_number])

        lookup_finished = False
        try:
            for attempt in range(self.retries + 1):
                try:
                    title = self.request_once(part_number)
                    if self.on_title is not None:
                        self.on_title(part_number, title)
                    self.titles[part_number] = title
                    lookup_finished = True
                    return title
                except RetryableLookupError as exc:
                    if attempt >= self.retries:
                        raise RotationCheckerError(
                            f"{exc} (failed after {attempt + 1} attempts)"
                        ) from exc
                    delay = exc.retry_after_seconds
                    if delay is None:
                        delay = min(1.0 * (2**attempt), 8.0)
                    time.sleep(delay)
        except RotationCheckerError as exc:
            self.errors[part_number] = str(exc)
            lookup_finished = True
            raise
        finally:
            if lookup_finished and self.on_lookup_complete is not None:
                self.on_lookup_complete(part_number)
        raise AssertionError("unreachable retry state")


def package_contacts(package: ET.Element) -> tuple[Contact, ...]:
    positions: dict[str, list[tuple[float, float]]] = {}
    display_names: dict[str, str] = {}
    for tag in ("smd", "pad"):
        for node in package.findall(tag):
            name = node.get("name", "").strip()
            if not name:
                continue
            try:
                x = float(node.attrib["x"])
                y = float(node.attrib["y"])
            except (KeyError, ValueError) as exc:
                raise RotationCheckerError(
                    f"contact {name!r} has invalid coordinates"
                ) from exc
            key = name.upper()
            positions.setdefault(key, []).append((x, y))
            display_names.setdefault(key, name)

    contacts: list[Contact] = []
    for key, points in positions.items():
        contacts.append(
            Contact(
                name=display_names[key],
                x=sum(point[0] for point in points) / len(points),
                y=sum(point[1] for point in points) / len(points),
            )
        )
    return tuple(contacts)


def axis_angle(dx: float, dy: float) -> int | None:
    scale = max(abs(dx), abs(dy))
    if scale <= 1e-9:
        return None
    tolerance = scale * 0.05
    if abs(dy) <= tolerance:
        return 0
    if abs(dx) <= tolerance:
        return 90
    return None


def direction_angle(dx: float, dy: float) -> int | None:
    scale = max(abs(dx), abs(dy))
    if scale <= 1e-9:
        return None
    tolerance = scale * 0.05
    if abs(dy) <= tolerance:
        return 0 if dx > 0 else 180
    if abs(dx) <= tolerance:
        return 90 if dy > 0 else 270
    if abs(abs(dx) - abs(dy)) > scale * 0.35:
        return None
    if dx > 0 and dy > 0:
        return 45
    if dx < 0 and dy > 0:
        return 135
    if dx < 0 and dy < 0:
        return 225
    return 315


def pin_position_angle(x: float, y: float) -> int | None:
    """Classify pin position as an EasyEDA side or quadrant token angle."""
    scale = max(abs(x), abs(y))
    if scale <= 1e-9:
        return None
    tolerance = scale * 0.05
    if abs(y) <= tolerance:
        return 0 if x > 0 else 180
    if abs(x) <= tolerance:
        return 90 if y > 0 else 270
    if x > 0 and y > 0:
        return 45
    if x < 0 and y > 0:
        return 135
    if x < 0 and y < 0:
        return 225
    return 315


def normalized_pin_name(value: str) -> str:
    return re.sub(r"[^A-Z0-9+-]", "", value.upper())


def contact_role(contact: Contact, pin_by_pad: Mapping[str, str]) -> str | None:
    values = [contact.name, pin_by_pad.get(contact.name.upper(), "")]
    normalized = {normalized_pin_name(value) for value in values if value}
    if normalized & {"A", "ANODE", "+", "PLUS", "POS", "POSITIVE"}:
        return "positive"
    if normalized & {"C", "K", "CATHODE", "-", "MINUS", "NEG", "NEGATIVE"}:
        return "negative"
    return None


def pin_one_candidates(
    contacts: tuple[Contact, ...],
    pin_by_pad: Mapping[str, str],
) -> list[Contact]:
    direct: list[Contact] = []
    mapped: list[Contact] = []
    for contact in contacts:
        contact_name = normalized_pin_name(contact.name)
        if re.fullmatch(r"(?:PAD|PIN|P)?0*1", contact_name):
            direct.append(contact)
        mapped_pin = normalized_pin_name(pin_by_pad.get(contact.name.upper(), ""))
        if re.fullmatch(r"(?:PIN|P)?0*1", mapped_pin):
            mapped.append(contact)
    return direct or mapped


def extract_single_token(pattern: re.Pattern[str], title: str, kind: str) -> str | None:
    tokens = [match.group(1).upper() for match in pattern.finditer(title)]
    unique_tokens = list(dict.fromkeys(tokens))
    if not unique_tokens:
        return None
    if len(unique_tokens) != 1:
        raise RotationCheckerError(
            f"EasyEDA title {title!r} has multiple {kind} tokens: "
            + ", ".join(unique_tokens)
        )
    return unique_tokens[0]


def title_has_horizontal_length(title: str) -> bool:
    return bool(
        EXPLICIT_LENGTH_PATTERN.search(title)
        or STANDARD_TWO_CONTACT_PATTERN.match(title)
    )


def infer_two_contact_orientation(
    part_number: str,
    title: str,
    contacts: tuple[Contact, ...],
    pin_by_pad: Mapping[str, str],
) -> OrientationExpectation:
    if not title_has_horizontal_length(title):
        raise RotationCheckerError(
            f"EasyEDA title {title!r} does not expose a 0-degree length axis"
        )

    polarity = extract_single_token(
        POLARITY_TOKEN_PATTERN,
        title,
        "polarity",
    )
    first, second = contacts
    local_axis = axis_angle(second.x - first.x, second.y - first.y)
    if local_axis is None:
        raise RotationCheckerError(
            "the two local contacts are coincident or not on a horizontal/vertical axis"
        )

    if polarity in {"FD", "RD"}:
        positive = [
            contact
            for contact in contacts
            if contact_role(contact, pin_by_pad) == "positive"
        ]
        negative = [
            contact
            for contact in contacts
            if contact_role(contact, pin_by_pad) == "negative"
        ]
        if len(positive) != 1 or len(negative) != 1:
            raise RotationCheckerError(
                f"EasyEDA title is polarized ({polarity}), but the local package/device "
                "does not provide one A/+ and one C/- contact"
            )
        local_direction = direction_angle(
            negative[0].x - positive[0].x,
            negative[0].y - positive[0].y,
        )
        if local_direction is None or local_direction % 90 != 0:
            raise RotationCheckerError(
                "the local polarity direction is not horizontal or vertical"
            )
        remote_direction = 0 if polarity == "FD" else 180
        suggested = (remote_direction - local_direction) % 360
        return OrientationExpectation(
            part_number=part_number,
            title=title,
            allowed_rotations=frozenset({suggested}),
            explanation=(
                f"local A/+ to C/- direction is {angle_name(local_direction)}; "
                f"EasyEDA {polarity} is {angle_name(remote_direction)}"
            ),
        )

    suggested_axis = local_axis % 180
    return OrientationExpectation(
        part_number=part_number,
        title=title,
        allowed_rotations=frozenset({suggested_axis, (suggested_axis + 180) % 360}),
        explanation=(
            f"local two-contact axis is {angle_name(local_axis)}; "
            "EasyEDA's 0-degree length axis is horizontal"
        ),
    )


def infer_multi_contact_orientation(
    part_number: str,
    title: str,
    contacts: tuple[Contact, ...],
    pin_by_pad: Mapping[str, str],
) -> OrientationExpectation:
    remote_token = extract_single_token(
        ORIENTATION_TOKEN_PATTERN,
        title,
        "pin-1 orientation",
    )
    if remote_token is None:
        raise RotationCheckerError(
            f"EasyEDA title {title!r} has no pin-1 orientation token"
        )

    candidates = pin_one_candidates(contacts, pin_by_pad)
    if len(candidates) != 1:
        if not candidates:
            raise RotationCheckerError("could not identify pin 1 in the local package")
        names = ", ".join(contact.name for contact in candidates)
        raise RotationCheckerError(
            f"local package has multiple pin-1 candidates: {names}"
        )

    pin_one = candidates[0]
    local_angle = pin_position_angle(pin_one.x, pin_one.y)
    if local_angle is None:
        raise RotationCheckerError(
            f"local pin 1 ({pin_one.name}) is at the origin or has an ambiguous angle"
        )
    remote_angle = ANGLE_BY_TOKEN[remote_token]
    # CPL/JLC_ROTATION angles are CCW-positive and are added to the local
    # footprint rotation, so solve remote = local + correction.
    suggested = (remote_angle - local_angle) % 360
    local_token = next(
        token for token, angle in ANGLE_BY_TOKEN.items() if angle == local_angle
    )
    return OrientationExpectation(
        part_number=part_number,
        title=title,
        allowed_rotations=frozenset({suggested}),
        explanation=(f"local pin 1 is {local_token}; EasyEDA pin 1 is {remote_token}"),
    )


def infer_orientation(
    part_number: str,
    title: str,
    package: ET.Element,
    pin_by_pad: Mapping[str, str],
) -> OrientationExpectation:
    contacts = package_contacts(package)
    if len(contacts) < 2:
        raise RotationCheckerError(
            f"local package has {len(contacts)} electrical contact(s); expected at least 2"
        )
    if len(contacts) == 2:
        return infer_two_contact_orientation(
            part_number,
            title,
            contacts,
            pin_by_pad,
        )
    return infer_multi_contact_orientation(
        part_number,
        title,
        contacts,
        pin_by_pad,
    )


def angle_name(angle: int) -> str:
    normalized = angle % 360
    names = {0: "right", 90: "up", 180: "left", 270: "down"}
    return names.get(normalized, f"{normalized} degrees")


def format_rotation(rotation: int | None, raw_text: str = "") -> str:
    if rotation is None:
        return raw_text or "?"
    return str(rotation)


def format_allowed_rotations(rotations: frozenset[int]) -> str:
    return "/".join(str(rotation) for rotation in sorted(rotations))


def report_for_part(part: PartRecord, client: EasyEdaClient) -> ReportRow | None:
    titles: list[str] = []
    expectations: list[OrientationExpectation] = []
    warnings = list(part.warnings)

    if part.package is not None:
        for part_number in part.part_numbers:
            try:
                title = client.get_package_title(part_number)
                titles.append(title)
                expectations.append(
                    infer_orientation(
                        part_number,
                        title,
                        part.package,
                        part.pin_by_pad,
                    )
                )
            except RotationCheckerError as exc:
                warnings.append(f"{part_number}: {exc}")

    allowed: frozenset[int] | None = None
    if expectations:
        allowed = expectations[0].allowed_rotations
        for expectation in expectations[1:]:
            allowed = allowed & expectation.allowed_rotations
        if not allowed:
            descriptions = "; ".join(
                f"{expectation.part_number} expects "
                f"{format_allowed_rotations(expectation.allowed_rotations)}"
                for expectation in expectations
            )
            warnings.append(f"alternative LCSC parts disagree: {descriptions}")

    package_label = "/".join(
        value for value in (part.package_library, part.package_name) if value
    )
    easyeda_label = "; ".join(dict.fromkeys(titles)) or "?"
    current_label = format_rotation(part.current_rotation, part.current_rotation_text)

    if warnings:
        suggestion = format_allowed_rotations(allowed) if allowed else "?"
        details = "; ".join(dict.fromkeys(warnings))
        if expectations:
            details += "; " + "; ".join(
                f"{expectation.part_number}: {expectation.explanation}"
                for expectation in expectations
            )
        return ReportRow(
            status="WARNING",
            reference=part.reference,
            part_numbers=";".join(part.part_numbers) or "?",
            package=package_label or "?",
            easyeda_package=easyeda_label,
            current_rotation=current_label,
            suggested_rotation=suggestion,
            details=details,
        )

    if allowed is None or part.current_rotation is None:
        return ReportRow(
            status="WARNING",
            reference=part.reference,
            part_numbers=";".join(part.part_numbers) or "?",
            package=package_label or "?",
            easyeda_package=easyeda_label,
            current_rotation=current_label,
            suggested_rotation="?",
            details="orientation could not be evaluated",
        )

    if part.current_rotation in allowed:
        return None

    details = "; ".join(
        f"{expectation.part_number}: {expectation.explanation}"
        for expectation in expectations
    )
    return ReportRow(
        status="MISMATCH",
        reference=part.reference,
        part_numbers=";".join(part.part_numbers),
        package=package_label,
        easyeda_package=easyeda_label,
        current_rotation=current_label,
        suggested_rotation=format_allowed_rotations(allowed),
        details=details,
    )


def table_cell(value: str) -> str:
    return value.replace("\r", r"\r").replace("\n", r"\n").replace("\t", r"\t")


def output_table(stream: TextIO, rows: list[ReportRow]) -> None:
    headers = [
        "Status",
        "Part",
        "LCSC",
        "EAGLE package",
        "EasyEDA package",
        "JLC_ROTATION",
        "Suggested",
        "Details",
    ]
    values = [
        [
            row.status,
            row.reference,
            row.part_numbers,
            row.package,
            row.easyeda_package,
            row.current_rotation,
            row.suggested_rotation,
            row.details,
        ]
        for row in rows
    ]
    escaped = [[table_cell(value) for value in row] for row in values]
    widths = [
        max(len(headers[index]), *(len(row[index]) for row in escaped))
        for index in range(len(headers))
    ]

    def format_row(row: list[str]) -> str:
        return " | ".join(value.ljust(width) for value, width in zip(row, widths))

    print(format_row(headers), file=stream)
    print("-+-".join("-" * width for width in widths), file=stream)
    for row in escaped:
        print(format_row(row), file=stream)


def main() -> int:
    args = parse_args()
    schematic_path = args.schematic.resolve()
    board_path = (args.board or schematic_path.with_suffix(".brd")).resolve()
    cache_path = args.cache.resolve()

    schematic_root = read_eagle_xml(schematic_path, "schematic")
    if args.board is not None and not board_path.is_file():
        raise FileNotFoundError(f"board not found: {board_path}")
    board_root = read_eagle_xml(board_path, "board") if board_path.is_file() else None
    parts = load_parts(schematic_root, board_root)
    if not parts:
        names = ", ".join(PART_NUMBER_ATTRIBUTES)
        raise RotationCheckerError(
            f"schematic has no parts with a supported part-number attribute ({names})"
        )

    lookup_part_numbers = tuple(
        dict.fromkeys(
            part_number
            for part in parts
            if part.package is not None
            for part_number in part.part_numbers
        )
    )
    with TitleCache(cache_path) as title_cache:
        progress = ProgressReporter(
            total=len(lookup_part_numbers),
            complete=sum(
                part_number in title_cache.titles for part_number in lookup_part_numbers
            ),
        )
        progress.display()
        client = EasyEdaClient(
            timeout=args.timeout,
            retries=args.retries,
            delay=args.delay,
            cached_titles=title_cache.titles,
            on_title=title_cache.add,
            on_lookup_complete=progress.advance,
        )
        try:
            rows = [
                row
                for part in parts
                if (row := report_for_part(part, client)) is not None
            ]
        finally:
            progress.finish()

    if rows:
        output_table(sys.stdout, rows)
        mismatches = sum(row.status == "MISMATCH" for row in rows)
        warnings = sum(row.status == "WARNING" for row in rows)
        print(
            f"\nChecked {len(parts)} schematic part(s): "
            f"{mismatches} suspected mismatch(es), {warnings} warning(s)."
        )
        return 1

    print(
        f"OK: checked {len(parts)} schematic part(s); "
        "no suspected rotation mismatches or warnings."
    )
    return 0


def cli() -> int:
    try:
        return main()
    except BrokenPipeError:
        return 0
    except KeyboardInterrupt:
        print("\nCanceled.", file=sys.stderr)
        return 130
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(cli())
