#!/usr/bin/env python3
"""Render an EAGLE schematic as a framed, multi-page Letter-size PDF."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

try:
    from PIL import Image, ImageChops, ImageDraw, ImageFont
except ImportError as exc:  # pragma: no cover - exercised only without Pillow
    raise SystemExit(
        "Pillow is required. Install it with: python -m pip install Pillow"
    ) from exc


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCHEMATIC = REPO_ROOT / "electrical" / "hot-wand.sch"
DEFAULT_EAGLECON_EXE = Path(
    r"C:\ProgramFiles\EAGLE-7.6.0\bin\eaglecon.exe"
)
DEFAULT_DPI = 400
DEFAULT_DIVIDER_THICKNESS_PX = 20
CONTENT_PADDING_PX = 60
FRAME_THICKNESS_PX = 3
PAGE_LABEL_FONT_SIZE_PX = 30
PAGE_LABEL_OFFSET_PX = FRAME_THICKNESS_PX + 5
JPEG_QUALITY = 90
DARK_RED = (139, 0, 0)

POINTS_PER_INCH = 72.0
LETTER_PORTRAIT = (8.5 * POINTS_PER_INCH, 11.0 * POINTS_PER_INCH)
LETTER_LANDSCAPE = (LETTER_PORTRAIT[1], LETTER_PORTRAIT[0])
PDF_MARGIN_POINTS = 0.5 * POINTS_PER_INCH


@dataclass(frozen=True)
class JpegPage:
    path: Path
    width: int
    height: int


def positive_int(value: str) -> int:
    try:
        result = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if result <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Export an EAGLE schematic to a color PNG, split it at sufficiently "
            "thick white horizontal dividers, frame the non-empty chunks, and "
            "write them as JPEG-compressed US Letter pages in a PDF."
        )
    )
    parser.add_argument(
        "schematic",
        nargs="?",
        type=Path,
        default=DEFAULT_SCHEMATIC,
        help="input .sch file (default: electrical/hot-wand.sch)",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="output PDF (default: <schematic path>.sch_preview.pdf)",
    )
    parser.add_argument(
        "--eaglecon",
        type=Path,
        default=Path(os.environ.get("EAGLECON_EXE", DEFAULT_EAGLECON_EXE)),
        help=(
            "path to eaglecon.exe "
            "(default: EAGLECON_EXE or EAGLE 7.6 installation)"
        ),
    )
    parser.add_argument(
        "--dpi",
        type=positive_int,
        default=DEFAULT_DPI,
        help=f"EAGLE PNG resolution (default: {DEFAULT_DPI} DPI)",
    )
    parser.add_argument(
        "--divider-thickness",
        "--minimum-divider-thickness",
        dest="divider_thickness",
        type=positive_int,
        default=DEFAULT_DIVIDER_THICKNESS_PX,
        metavar="PIXELS",
        help=(
            "minimum number of consecutive white rows required for a page "
            f"break (default: {DEFAULT_DIVIDER_THICKNESS_PX})"
        ),
    )
    return parser.parse_args()


def resolve_paths(args: argparse.Namespace) -> tuple[Path, Path, Path]:
    schematic = args.schematic.expanduser().resolve()
    eaglecon = args.eaglecon.expanduser().resolve()
    output = (
        args.output.expanduser().resolve()
        if args.output is not None
        else schematic.with_suffix(".sch_preview.pdf")
    )

    if schematic.suffix.lower() != ".sch":
        raise ValueError(f"Schematic must have a .sch extension: {schematic}")
    if not schematic.is_file():
        raise FileNotFoundError(f"Schematic not found: {schematic}")
    if not eaglecon.is_file():
        raise FileNotFoundError(f"eaglecon.exe not found: {eaglecon}")
    if output.suffix.lower() != ".pdf":
        raise ValueError(f"Output must have a .pdf extension: {output}")

    return schematic, output, eaglecon


def eagle_command_path(path: Path) -> str:
    """Return an EAGLE command-safe absolute path."""
    result = path.resolve().as_posix()
    if "'" in result:
        raise ValueError(f"EAGLE export paths cannot contain an apostrophe: {path}")
    return result


def export_png(
    eaglecon: Path,
    schematic: Path,
    png_path: Path,
    dpi: int,
) -> None:
    # The white palette retains layer colors while making whitespace detection
    # deterministic. Omitting MONOCHROME makes this a color export.
    export_command = (
        "SET PALETTE WHITE; "
        f"EXPORT IMAGE '{eagle_command_path(png_path)}' {dpi}; QUIT;"
    )
    command = [
        str(eaglecon),
        "-N+",
        f"-C{export_command}",
        str(schematic),
    ]
    result = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    if result.returncode != 0:
        details = result.stdout.strip()
        if details:
            details = f"\n{details}"
        raise RuntimeError(
            f"EAGLE PNG export failed with exit code {result.returncode}.{details}"
        )
    if not png_path.is_file() or png_path.stat().st_size == 0:
        details = result.stdout.strip()
        if details:
            details = f"\n{details}"
        raise RuntimeError(f"EAGLE did not create the PNG output.{details}")


def flatten_onto_white(image: Image.Image) -> Image.Image:
    """Make transparency deterministic while preserving color."""
    if image.mode == "RGB":
        return image.copy()

    foreground = image.convert("RGBA")
    background = Image.new("RGBA", foreground.size, "white")
    background.alpha_composite(foreground)
    foreground.close()
    result = background.convert("RGB")
    background.close()
    return result


def nonwhite_mask(image: Image.Image) -> Image.Image:
    white = Image.new("RGB", image.size, "white")
    difference_rgb = ImageChops.difference(image, white)
    white.close()
    difference = difference_rgb.convert("L")
    difference_rgb.close()
    return difference


def find_content_chunks(
    image: Image.Image,
    minimum_divider_thickness: int,
) -> list[tuple[int, int]]:
    """Return Y ranges split only by sufficiently thick all-white bands."""
    difference = nonwhite_mask(image)
    _x_projection, y_projection = difference.getprojection()
    difference.close()

    raw_chunks: list[tuple[int, int]] = []
    start: int | None = None
    for y, has_content in enumerate(y_projection):
        if has_content and start is None:
            start = y
        elif not has_content and start is not None:
            raw_chunks.append((start, y))
            start = None
    if start is not None:
        raw_chunks.append((start, image.height))

    if not raw_chunks:
        return []

    chunks = [raw_chunks[0]]
    for top, bottom in raw_chunks[1:]:
        previous_top, previous_bottom = chunks[-1]
        divider_thickness = top - previous_bottom
        if divider_thickness < minimum_divider_thickness:
            chunks[-1] = (previous_top, bottom)
        else:
            chunks.append((top, bottom))
    return chunks


def minimum_occupied_crop(image: Image.Image) -> Image.Image:
    difference = nonwhite_mask(image)
    bounding_box = difference.getbbox()
    difference.close()
    if bounding_box is None:
        raise RuntimeError("Encountered an empty image chunk")
    return image.crop(bounding_box)


def page_label_font() -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    try:
        return ImageFont.load_default(size=PAGE_LABEL_FONT_SIZE_PX)
    except TypeError as exc:  # Pillow older than 10.1
        raise RuntimeError(
            "Pillow 10.1 or newer is required for the 16-pixel page label font"
        ) from exc


def make_jpeg_pages(
    image: Image.Image,
    chunks: list[tuple[int, int]],
    output_directory: Path,
    dpi: int,
) -> list[JpegPage]:
    font = page_label_font()
    page_count = len(chunks)
    jpeg_pages: list[JpegPage] = []

    for page_number, (top, bottom) in enumerate(chunks, start=1):
        vertical_chunk = image.crop((0, top, image.width, bottom))
        try:
            content = minimum_occupied_crop(vertical_chunk)
        finally:
            vertical_chunk.close()

        framed = Image.new(
            "RGB",
            (
                content.width + 2 * CONTENT_PADDING_PX,
                content.height + 2 * CONTENT_PADDING_PX,
            ),
            "white",
        )
        try:
            framed.paste(content, (CONTENT_PADDING_PX, CONTENT_PADDING_PX))
        finally:
            content.close()

        drawing = ImageDraw.Draw(framed)
        drawing.rectangle(
            (0, 0, framed.width - 1, framed.height - 1),
            outline=DARK_RED,
            width=FRAME_THICKNESS_PX,
        )
        drawing.text(
            (PAGE_LABEL_OFFSET_PX, PAGE_LABEL_OFFSET_PX),
            f"page {page_number} of {page_count}",
            fill=DARK_RED,
            font=font,
        )

        jpeg_path = output_directory / f"page-{page_number:04d}.jpg"
        framed.save(
            jpeg_path,
            format="JPEG",
            quality=JPEG_QUALITY,
            subsampling=0,
            optimize=True,
            dpi=(dpi, dpi),
        )
        jpeg_pages.append(JpegPage(jpeg_path, framed.width, framed.height))
        framed.close()

    return jpeg_pages


def page_geometry(page: JpegPage) -> tuple[float, float, float, float, float, float]:
    page_width, page_height = (
        LETTER_LANDSCAPE if page.width > page.height else LETTER_PORTRAIT
    )
    available_width = page_width - 2 * PDF_MARGIN_POINTS
    available_height = page_height - 2 * PDF_MARGIN_POINTS

    # The PDF transformation scales only the displayed placement. The JPEG's
    # pixel dimensions and compressed data are embedded unchanged.
    placement_scale = min(
        available_width / page.width,
        available_height / page.height,
    )
    image_width = page.width * placement_scale
    image_height = page.height * placement_scale
    image_x = (page_width - image_width) / 2
    image_y = (page_height - image_height) / 2
    return page_width, page_height, image_x, image_y, image_width, image_height


def pdf_object(object_number: int, body: bytes) -> bytes:
    return (
        f"{object_number} 0 obj\n".encode("ascii")
        + body
        + b"\nendobj\n"
    )


def write_pdf(
    path: Path,
    pages: list[JpegPage],
) -> None:
    page_object_numbers = [3 + index * 3 for index in range(len(pages))]
    object_count = 2 + 3 * len(pages)
    offsets = [0] * (object_count + 1)

    with path.open("wb") as pdf:
        pdf.write(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")

        catalog = b"<< /Type /Catalog /Pages 2 0 R >>"
        offsets[1] = pdf.tell()
        pdf.write(pdf_object(1, catalog))

        kids = " ".join(f"{number} 0 R" for number in page_object_numbers)
        page_tree = (
            f"<< /Type /Pages /Count {len(pages)} /Kids [{kids}] >>"
        ).encode("ascii")
        offsets[2] = pdf.tell()
        pdf.write(pdf_object(2, page_tree))

        for index, page in enumerate(pages):
            page_object_number = page_object_numbers[index]
            image_object_number = page_object_number + 1
            content_object_number = page_object_number + 2
            (
                page_width,
                page_height,
                image_x,
                image_y,
                image_width,
                image_height,
            ) = page_geometry(page)

            page_dictionary = (
                "<< /Type /Page /Parent 2 0 R "
                f"/MediaBox [0 0 {page_width:.2f} {page_height:.2f}] "
                "/Resources << /XObject "
                f"<< /Image {image_object_number} 0 R >> >> "
                f"/Contents {content_object_number} 0 R >>"
            ).encode("ascii")
            offsets[page_object_number] = pdf.tell()
            pdf.write(pdf_object(page_object_number, page_dictionary))

            jpeg_size = page.path.stat().st_size
            image_header = (
                "<< /Type /XObject /Subtype /Image "
                f"/Width {page.width} /Height {page.height} "
                "/ColorSpace /DeviceRGB /BitsPerComponent 8 "
                f"/Filter /DCTDecode /Length {jpeg_size} >>\nstream\n"
            ).encode("ascii")
            offsets[image_object_number] = pdf.tell()
            pdf.write(f"{image_object_number} 0 obj\n".encode("ascii"))
            pdf.write(image_header)
            with page.path.open("rb") as jpeg:
                while data := jpeg.read(1024 * 1024):
                    pdf.write(data)
            pdf.write(b"\nendstream\nendobj\n")

            content = (
                "q\n"
                f"{image_width:.6f} 0 0 {image_height:.6f} "
                f"{image_x:.6f} {image_y:.6f} cm\n"
                "/Image Do\n"
                "Q\n"
            ).encode("ascii")
            content_stream = (
                f"<< /Length {len(content)} >>\nstream\n".encode("ascii")
                + content
                + b"endstream"
            )
            offsets[content_object_number] = pdf.tell()
            pdf.write(pdf_object(content_object_number, content_stream))

        cross_reference_offset = pdf.tell()
        pdf.write(f"xref\n0 {object_count + 1}\n".encode("ascii"))
        pdf.write(b"0000000000 65535 f \n")
        for object_number in range(1, object_count + 1):
            pdf.write(f"{offsets[object_number]:010d} 00000 n \n".encode("ascii"))
        pdf.write(
            (
                f"trailer\n<< /Size {object_count + 1} /Root 1 0 R >>\n"
                f"startxref\n{cross_reference_offset}\n%%EOF\n"
            ).encode("ascii")
        )


def write_pdf_atomic(pages: list[JpegPage], output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_path: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            prefix=f".{output.stem}-",
            suffix=".pdf",
            dir=output.parent,
            delete=False,
        ) as temporary_file:
            temporary_path = Path(temporary_file.name)

        write_pdf(temporary_path, pages)
        if temporary_path.stat().st_size == 0:
            raise RuntimeError("Created an empty PDF")
        os.replace(temporary_path, output)
        temporary_path = None
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def main() -> int:
    args = parse_args()
    try:
        schematic, output, eaglecon = resolve_paths(args)
        if schematic == output:
            raise ValueError("The output path must differ from the schematic path")

        with tempfile.TemporaryDirectory(prefix="eagle-schematic-preview-") as temp_dir:
            temp_path = Path(temp_dir)
            png_path = temp_path / "schematic.png"
            export_png(eaglecon, schematic, png_path, args.dpi)

            with Image.open(png_path) as source:
                source.load()
                image = flatten_onto_white(source)

            try:
                chunks = find_content_chunks(image, args.divider_thickness)
                if not chunks:
                    raise RuntimeError("The exported schematic PNG contains no content")
                jpeg_pages = make_jpeg_pages(
                    image,
                    chunks,
                    temp_path,
                    args.dpi,
                )
            finally:
                image.close()

            write_pdf_atomic(jpeg_pages, output)

        print(
            f"Created {output} ({len(jpeg_pages)} US Letter page"
            f"{'s' if len(jpeg_pages) != 1 else ''}, {args.dpi} DPI source)"
        )
        return 0
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
