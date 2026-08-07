#!/usr/bin/env python3
"""Add current JLCPCB stock counts to the populated rows of a BOM CSV.

The stock lookup uses the same undocumented endpoint as JLCPCB's public parts
search page.  It currently accepts anonymous JSON requests, but its URL and
response format may change without notice.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
import time
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any, TextIO
from urllib.error import HTTPError, URLError
from urllib.request import OpenerDirector, Request, build_opener


API_URL = (
    "https://jlcpcb.com/api/overseas-pcb-order/v1/"
    "shoppingCart/smtGood/selectSmtComponentList/v2"
)
LCSC_COLUMN = "LCSC Part #"
STOCK_COLUMN = "JLCPCB Stock"
USER_AGENT = "Hot-Wand-JLC-Stock-Checker/1.0"
MAX_RESPONSE_BYTES = 4 * 1024 * 1024
TRANSIENT_HTTP_STATUS = {429, 500, 502, 503, 504}
LCSC_PART_PATTERN = re.compile(r"C[1-9][0-9]*", re.IGNORECASE)


class StockCheckerError(RuntimeError):
    """A user-facing BOM or stock-lookup error."""


class RetryableStockError(StockCheckerError):
    """A transient stock-lookup error that may succeed on another attempt."""

    def __init__(self, message: str, retry_after_seconds: float | None = None):
        super().__init__(message)
        self.retry_after_seconds = retry_after_seconds


@dataclass(frozen=True)
class BomEntry:
    row: dict[str, str]
    part_numbers: tuple[str, ...]


def positive_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
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
    parser.add_argument("bom", type=Path, help="input BOM CSV file")
    parser.add_argument(
        "--format",
        choices=("auto", "csv", "table"),
        default="auto",
        dest="output_format",
        help=(
            "output format: auto uses a table in a terminal and CSV when "
            "redirected (default: auto)"
        ),
    )
    parser.add_argument(
        "--timeout",
        type=positive_float,
        default=15.0,
        metavar="SECONDS",
        help="timeout for each JLCPCB request (default: 15)",
    )
    parser.add_argument(
        "--retries",
        type=nonnegative_int,
        default=2,
        metavar="COUNT",
        help="retries after transient request failures (default: 2)",
    )
    return parser.parse_args()


def parse_part_numbers(value: str, line_number: int) -> tuple[str, ...]:
    chunks = value.split(";")
    if any(not chunk.strip() for chunk in chunks):
        raise StockCheckerError(
            f"empty LCSC part number in CSV line {line_number}: {value!r}"
        )

    part_numbers = tuple(chunk.strip().upper() for chunk in chunks)
    for part_number in part_numbers:
        if LCSC_PART_PATTERN.fullmatch(part_number) is None:
            raise StockCheckerError(
                f"invalid LCSC part number in CSV line {line_number}: {part_number!r}"
            )
    return part_numbers


def read_bom(path: Path) -> tuple[list[str], list[BomEntry]]:
    if not path.is_file():
        raise FileNotFoundError(f"BOM CSV not found: {path}")

    with path.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.reader(source)
        try:
            fieldnames = next(reader)
        except StopIteration as exc:
            raise StockCheckerError(f"BOM CSV is empty: {path}") from exc

        if not fieldnames or any(not fieldname for fieldname in fieldnames):
            raise StockCheckerError("BOM CSV contains an empty column name")
        if len(fieldnames) != len(set(fieldnames)):
            raise StockCheckerError("BOM CSV contains duplicate column names")
        if LCSC_COLUMN not in fieldnames:
            raise StockCheckerError(
                f"BOM CSV is missing required column {LCSC_COLUMN!r}"
            )
        if STOCK_COLUMN in fieldnames:
            raise StockCheckerError(
                f"BOM CSV already contains output column {STOCK_COLUMN!r}"
            )

        lcsc_index = fieldnames.index(LCSC_COLUMN)
        entries: list[BomEntry] = []
        for line_number, values in enumerate(reader, start=2):
            # Match DictReader's useful behavior of ignoring completely blank
            # lines while still rejecting partially populated, ragged rows.
            if not values:
                continue
            if len(values) != len(fieldnames):
                raise StockCheckerError(
                    f"CSV line {line_number} has {len(values)} fields; "
                    f"expected {len(fieldnames)}"
                )

            part_number_text = values[lcsc_index].strip()
            if not part_number_text:
                continue

            entries.append(
                BomEntry(
                    row=dict(zip(fieldnames, values)),
                    part_numbers=parse_part_numbers(
                        part_number_text,
                        line_number,
                    ),
                )
            )

    return fieldnames, entries


def build_search_payload(part_number: str) -> dict[str, Any]:
    # Mirror the request made by the public JLCPCB component-search page.  Most
    # fields are search filters left at their neutral values, but retaining the
    # full shape makes this request easier to compare with browser traffic.
    return {
        "currentPage": 1,
        "pageSize": 25,
        "presaleType": "stock",
        "searchType": 2,
        "keyword": part_number,
        "componentLibraryType": None,
        "stockFlag": None,
        "stockSort": None,
        "firstSortName": None,
        "secondSortName": None,
        "componentBrandList": [],
        "searchSource": "search",
        "componentSpecificationList": [],
        "componentAttributeList": [],
        "paramList": [],
        "startStockNumber": None,
        "sortMode": "",
        "sortASC": "",
    }


def decode_json_response(response: Any, part_number: str) -> Mapping[str, Any]:
    body = response.read(MAX_RESPONSE_BYTES + 1)
    if len(body) > MAX_RESPONSE_BYTES:
        raise StockCheckerError(
            f"JLCPCB response was unexpectedly large for {part_number}"
        )

    try:
        payload = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise StockCheckerError(
            f"JLCPCB returned invalid JSON for {part_number}"
        ) from exc
    if not isinstance(payload, Mapping):
        raise StockCheckerError(
            f"JLCPCB returned an unexpected JSON value for {part_number}"
        )
    return payload


def response_error_message(payload: Mapping[str, Any]) -> str:
    message = payload.get("message")
    if isinstance(message, str) and message.strip():
        return message.strip()
    return "no error message"


def extract_stock_count(
    payload: Mapping[str, Any],
    part_number: str,
) -> int | None:
    code = payload.get("code")
    if code != 200:
        message = response_error_message(payload)
        error_text = f"JLCPCB API error {code!r} for {part_number}: {message}"
        if isinstance(code, int) and code >= 500:
            raise RetryableStockError(error_text)
        raise StockCheckerError(error_text)

    data = payload.get("data")
    if not isinstance(data, Mapping):
        raise StockCheckerError(f"JLCPCB response has no data object for {part_number}")
    page_info = data.get("componentPageInfo")
    if not isinstance(page_info, Mapping):
        raise StockCheckerError(
            f"JLCPCB response has no component page for {part_number}"
        )
    components = page_info.get("list")
    if not isinstance(components, list):
        raise StockCheckerError(
            f"JLCPCB response has no component list for {part_number}"
        )

    exact_matches: list[Mapping[str, Any]] = []
    for component in components:
        if not isinstance(component, Mapping):
            raise StockCheckerError(
                f"JLCPCB returned a malformed component for {part_number}"
            )
        component_code = component.get("componentCode")
        if (
            isinstance(component_code, str)
            and component_code.strip().upper() == part_number
        ):
            exact_matches.append(component)

    # The endpoint performs fuzzy searches, so list[0] is not necessarily the
    # requested part.  An empty exact-match set means "not found", not zero.
    if not exact_matches:
        return None
    if len(exact_matches) != 1:
        raise StockCheckerError(
            f"JLCPCB returned duplicate exact matches for {part_number}"
        )

    stock_count = exact_matches[0].get("stockCount")
    if (
        isinstance(stock_count, bool)
        or not isinstance(stock_count, int)
        or stock_count < 0
    ):
        raise StockCheckerError(
            f"JLCPCB returned an invalid stock count for {part_number}: {stock_count!r}"
        )
    return stock_count


def parse_retry_after(error: HTTPError) -> float | None:
    value = error.headers.get("Retry-After")
    if value is None:
        return None
    try:
        seconds = float(value)
    except ValueError:
        return None
    if seconds < 0:
        return None
    return min(seconds, 10.0)


class JlcStockClient:
    def __init__(self, timeout: float, retries: int):
        self.timeout = timeout
        self.retries = retries
        self.opener: OpenerDirector = build_opener()

    def request_once(self, part_number: str) -> Mapping[str, Any]:
        request_body = json.dumps(
            build_search_payload(part_number),
            separators=(",", ":"),
        ).encode("utf-8")
        request = Request(
            API_URL,
            data=request_body,
            method="POST",
            headers={
                "Accept": "application/json",
                "Content-Type": "application/json",
                "User-Agent": USER_AGENT,
            },
        )

        try:
            with self.opener.open(request, timeout=self.timeout) as response:
                return decode_json_response(response, part_number)
        except HTTPError as exc:
            message = f"JLCPCB returned HTTP {exc.code} for {part_number}"
            if exc.code in TRANSIENT_HTTP_STATUS:
                raise RetryableStockError(
                    message,
                    retry_after_seconds=parse_retry_after(exc),
                ) from exc
            raise StockCheckerError(message) from exc
        except (TimeoutError, URLError) as exc:
            raise RetryableStockError(
                f"JLCPCB request failed for {part_number}: {exc}"
            ) from exc

    def get_stock_count(self, part_number: str) -> int | None:
        for attempt in range(self.retries + 1):
            try:
                payload = self.request_once(part_number)
                return extract_stock_count(payload, part_number)
            except RetryableStockError as exc:
                if attempt >= self.retries:
                    raise StockCheckerError(
                        f"{exc} (failed after {attempt + 1} attempts)"
                    ) from exc
                delay = exc.retry_after_seconds
                if delay is None:
                    delay = min(0.5 * (2**attempt), 4.0)
                time.sleep(delay)

        raise AssertionError("unreachable retry state")


def collect_stock_values(
    entries: list[BomEntry],
    client: JlcStockClient,
) -> tuple[list[str], dict[str, str]]:
    cache: dict[str, int | None] = {}
    lookup_errors: dict[str, str] = {}
    values: list[str] = []

    for entry in entries:
        for part_number in entry.part_numbers:
            if part_number in cache or part_number in lookup_errors:
                continue
            try:
                cache[part_number] = client.get_stock_count(part_number)
            except StockCheckerError as exc:
                # A failed part is cached just like a successful lookup.  This
                # prevents duplicate BOM entries from repeating a failed
                # request while allowing every other unique part to be tried.
                lookup_errors[part_number] = str(exc)

        # Multiple semicolon-separated alternatives stay in their original
        # order; their semicolon-separated counts occupy the same output cell.
        values.append(
            ";".join(
                (
                    "ERROR"
                    if part_number in lookup_errors
                    else (
                        "NOT FOUND"
                        if cache[part_number] is None
                        else str(cache[part_number])
                    )
                )
                for part_number in entry.part_numbers
            )
        )

    return values, lookup_errors


def output_csv(
    stream: TextIO,
    fieldnames: list[str],
    entries: list[BomEntry],
    stock_values: list[str],
) -> None:
    writer = csv.writer(stream, lineterminator="\n")
    writer.writerow([*fieldnames, STOCK_COLUMN])
    for entry, stock_value in zip(entries, stock_values):
        writer.writerow(
            [*(entry.row[fieldname] for fieldname in fieldnames), stock_value]
        )


def table_cell(value: str) -> str:
    return value.replace("\r", r"\r").replace("\n", r"\n").replace("\t", r"\t")


def output_table(
    stream: TextIO,
    fieldnames: list[str],
    entries: list[BomEntry],
    stock_values: list[str],
) -> None:
    rows = [
        [*(table_cell(entry.row[fieldname]) for fieldname in fieldnames), stock]
        for entry, stock in zip(entries, stock_values)
    ]
    headers = [*fieldnames, STOCK_COLUMN]
    widths = [
        max(len(headers[index]), *(len(row[index]) for row in rows))
        if rows
        else len(headers[index])
        for index in range(len(headers))
    ]

    def format_row(row: list[str]) -> str:
        cells = [
            value.rjust(width) if index == len(row) - 1 else value.ljust(width)
            for index, (value, width) in enumerate(zip(row, widths))
        ]
        return " | ".join(cells)

    print(format_row(headers), file=stream)
    print("-+-".join("-" * width for width in widths), file=stream)
    for row in rows:
        print(format_row(row), file=stream)


def main() -> int:
    args = parse_args()
    input_path = args.bom.resolve()
    fieldnames, entries = read_bom(input_path)

    client = JlcStockClient(timeout=args.timeout, retries=args.retries)
    stock_values, lookup_errors = collect_stock_values(entries, client)

    output_format = args.output_format
    if output_format == "auto":
        output_format = "table" if sys.stdout.isatty() else "csv"

    if output_format == "table":
        output_table(sys.stdout, fieldnames, entries, stock_values)
    else:
        output_csv(sys.stdout, fieldnames, entries, stock_values)

    if lookup_errors:
        # Flush the complete data output before reporting diagnostics so an
        # interactive run never appears to contain only the failure message.
        sys.stdout.flush()
        for part_number, message in lookup_errors.items():
            print(f"WARNING: {part_number}: {message}", file=sys.stderr)
        print(
            f"ERROR: {len(lookup_errors)} stock lookup(s) failed; "
            f"affected rows are marked ERROR.",
            file=sys.stderr,
        )
        return 1

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
