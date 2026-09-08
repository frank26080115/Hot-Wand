#!/usr/bin/env python3
"""Convert an SVG or Pillow-supported raster image to a Brother P-touch LBX.

Dependencies: python -m pip install Pillow defusedxml
SVG rendering prefers Inkscape when installed (better text/layout fidelity).
Fallback: python -m pip install CairoSVG (also needs native Cairo on Windows).
Raster inputs need only Pillow. https://cairosvg.org/documentation/

Examples:
  python tools/img_to_brother_label.py mechanical/lite/input-sticker.svg
  python tools/img_to_brother_label.py photo.png -o label.lbx --tape-length 4.62
  python tools/img_to_brother_label.py art.svg --horizontal-margin 2mm --keep-temp

Bare CLI dimensions are inches; suffixes mm, in, inch, inches, or " are accepted.
Defaults reproduce the inspected PT-D610BT label margins: horizontal 5.6 pt per
end; vertical 8.4 pt per edge. Tape width defaults to exactly 24 mm.

Realistic-scale SVG: either page dimension matches tape width within 0.5 mm.
Rotate 90 degrees counterclockwise if the matching dimension is SVG width.
Render at 360 DPI; preserve physical scale and center-crop/pad to the requested
tape rectangle, then crop away margins. Automatic length is the SVG's other
dimension, INCLUDING margins. Fixed length never rescales realistic artwork.

Other SVG: render at 800 DPI. Raster: keep native pixels (first frame/page,
honoring EXIF orientation). For both, fit physical placement to printable height,
or contain within both printable dimensions for fixed length. This changes XML
placement, not bitmap resolution; raster DPI metadata is intentionally ignored.
Automatic length is calculated explicitly and saved with autoLength=false so
P-touch does not independently change it.

Artwork is composited on white and thresholded at 128, without dithering.
The embedded 32-bit BMP contains ONLY opaque black/white pixels, matching the BMP
container format of the inspected sample. No installed P-touch/template required.
Printer ID is based on that sample, not a universal printer driver API. The 24 mm
media preset is retained only at 24 mm; other widths use custom paper dimensions.
"""
from __future__ import annotations

import argparse
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from datetime import datetime, timezone
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

NAMESPACES = {
    name: "http://schemas.brother.info/ptouch/2007/lbx/" + suffix
    for name, suffix in (
        ("pt", "main"), ("style", "style"), ("image", "image"),
        ("meta", "meta"),
    )
}
for prefix, uri in NAMESPACES.items():
    ET.register_namespace(prefix, uri)
ET.register_namespace("dc", "http://purl.org/dc/elements/1.1/")
ET.register_namespace("dcterms", "http://purl.org/dc/terms/")

# Schema and opaque printer/media settings copied from P-touch Editor 6.12.1.0.
LABEL_TEMPLATE = r'''<?xml version="1.0" encoding="UTF-8"?>
<pt:document xmlns:pt="http://schemas.brother.info/ptouch/2007/lbx/main" xmlns:style="http://schemas.brother.info/ptouch/2007/lbx/style" xmlns:text="http://schemas.brother.info/ptouch/2007/lbx/text" xmlns:draw="http://schemas.brother.info/ptouch/2007/lbx/draw" xmlns:image="http://schemas.brother.info/ptouch/2007/lbx/image" xmlns:barcode="http://schemas.brother.info/ptouch/2007/lbx/barcode" xmlns:database="http://schemas.brother.info/ptouch/2007/lbx/database" xmlns:table="http://schemas.brother.info/ptouch/2007/lbx/table" xmlns:cable="http://schemas.brother.info/ptouch/2007/lbx/cable" version="1.10" generator="P-touch Editor 6.12.1.0 Windows"><pt:body currentSheet="Sheet 1" direction="LTR"><style:sheet name="Sheet 1"><style:paper media="0" width="68pt" height="332.4pt" marginLeft="8.4pt" marginTop="5.6pt" marginRight="8.4pt" marginBottom="5.6pt" orientation="landscape" autoLength="false" monochromeDisplay="true" printColorDisplay="false" printColorsID="0" paperColor="#FFFFFF" paperInk="#000000" split="1" format="261" backgroundTheme="0" printerID="31792" printerName="Brother PT-D610BT"></style:paper><style:cutLine regularCut="0pt" freeCut=""></style:cutLine><style:backGround x="5.6pt" y="8.4pt" width="321.2pt" height="51.2pt" brushStyle="NULL" brushId="0" userPattern="NONE" userPatternId="0" color="#000000" printColorNumber="1" backColor="#FFFFFF" backPrintColorNumber="0"></style:backGround><pt:objects><image:image><pt:objectStyle x="5.6pt" y="8.4pt" width="76.8pt" height="51.2pt" backColor="#FFFFFF" backPrintColorNumber="0" ropMode="COPYPEN" angle="0" anchor="TOPLEFT" flip="NONE"><pt:pen style="NULL" widthX="0.5pt" widthY="0.5pt" color="#000000" printColorNumber="1"></pt:pen><pt:brush style="NULL" color="#000000" printColorNumber="1" id="0"></pt:brush><pt:expanded objectName="Image1" ID="0" lock="2" templateMergeTarget="LABELLIST" templateMergeType="NONE" templateMergeID="0" allowOutOfBoundsTransfer="false" linkStatus="NONE" linkID="0"></pt:expanded></pt:objectStyle><image:imageStyle originalName="luggage_tag.fw.png" alignInText="NONE" firstMerge="true" IpName="" fileName="Object0.bmp"><image:transparent flag="false" color="#FFFFFF"></image:transparent><image:trimming flag="false" shape="RECTANGLE" trimOrgX="0pt" trimOrgY="0pt" trimOrgWidth="60pt" trimOrgHeight="40pt"></image:trimming><image:orgPos x="5.6pt" y="8.4pt" width="76.8pt" height="51.2pt"></image:orgPos><image:effect effect="NONE" brightness="50" contrast="50" photoIndex="4"></image:effect><image:mono operationKind="BINARY" reverse="0" ditherKind="MESH" threshold="128" gamma="100" ditherEdge="0" rgbconvProportionRed="30" rgbconvProportionGreen="59" rgbconvProportionBlue="11" rgbconvProportionReversed="0"></image:mono></image:imageStyle></image:image></pt:objects></style:sheet></pt:body></pt:document>'''

NUMBER = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"
DEFAULT_HORIZONTAL = 5.6 / 72
DEFAULT_VERTICAL = 8.4 / 72
MAX_RENDER_PIXELS = 80_000_000


def dimension(value: str, name: str, *, automatic: bool = False,
              allow_zero: bool = False) -> float | None:
    """Parse CLI inches/mm, returning inches or None for automatic length."""
    raw = value.strip().lower()
    if automatic and raw in ("", "auto", "automatic"):
        return None
    match = re.fullmatch(rf"({NUMBER})\s*(mm|in|inch|inches|\")?", raw)
    if not match:
        raise ValueError(
            f"{name}: cannot parse {value!r}; expected a finite number in inches "
            "(e.g. 2.5 or 2.5in) or millimeters (e.g. 63mm)"
        )
    number = float(match[1])
    if not math.isfinite(number):
        raise ValueError(f"{name}: {value!r} is not a finite dimension")
    if number < 0:
        raise ValueError(f"{name}: must not be negative (got {value!r})")
    if number == 0:
        if automatic:
            return None
        if not allow_zero:
            raise ValueError(f"{name}: must be greater than zero")
    return number / 25.4 if match[2] == "mm" else number


def argument_dimension(name: str, **options):
    def parse(value):
        try:
            return dimension(value, name, **options)
        except ValueError as exc:
            raise argparse.ArgumentTypeError(str(exc)) from exc
    return parse


def svg_dimensions(path: Path):
    """Read intrinsic dimensions in inches; SVG unitless lengths are CSS px."""
    try:
        from defusedxml import ElementTree as safe_et
    except ImportError as exc:
        raise ValueError("SVG input requires defusedxml: python -m pip install defusedxml") from exc
    root = safe_et.parse(path).getroot()
    if root.tag != "{http://www.w3.org/2000/svg}svg":
        raise ValueError("SVG input: root must be an SVG element in the SVG namespace")
    factors = {"": 1 / 96, "px": 1 / 96, "in": 1, "mm": 1 / 25.4,
               "cm": 1 / 2.54, "q": 1 / 101.6, "pt": 1 / 72, "pc": 1 / 6}

    def read_length(name):
        raw = root.get(name)
        if raw is None:
            return None
        match = re.fullmatch(rf"({NUMBER})\s*(px|in|mm|cm|q|pt|pc)?", raw.strip(), re.I)
        if not match:
            raise ValueError(f"SVG {name}: cannot resolve {raw!r}; use an absolute "
                             "dimension (mm, in, px, etc.), not percentages/em/auto")
        value = float(match[1]) * factors[(match[2] or "").lower()]
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f"SVG {name}: must be finite and greater than zero")
        return value

    width, height = read_length("width"), read_length("height")
    if width is None or height is None:
        try:
            viewbox = [float(x) for x in re.split(r"[\s,]+", root.get("viewBox", "").strip())]
            if len(viewbox) != 4 or not all(math.isfinite(x) for x in viewbox):
                raise ValueError
            if viewbox[2] <= 0 or viewbox[3] <= 0:
                raise ValueError
        except ValueError as exc:
            raise ValueError("SVG dimensions: missing width/height requires a valid viewBox") from exc
        if width is None and height is None:
            width, height = viewbox[2] / 96, viewbox[3] / 96
        elif width is None:
            width = height * viewbox[2] / viewbox[3]
        else:
            height = width * viewbox[3] / viewbox[2]
    return width, height


def pixel_size(width: float, height: float, dpi: int) -> tuple[int, int]:
    if not all(math.isfinite(x) and x > 0 for x in (width, height)):
        raise ValueError("image dimensions must be finite and greater than zero")
    if width * dpi * height * dpi > MAX_RENDER_PIXELS:
        raise ValueError(f"render/canvas exceeds {MAX_RENDER_PIXELS:,} pixels; reduce dimensions")
    size = round(width * dpi), round(height * dpi)
    if min(size) < 1:
        raise ValueError(f"image dimensions are smaller than one pixel at {dpi} DPI")
    return size


def monochrome(image):
    from PIL import Image
    rgba = image.convert("RGBA")
    white = Image.new("RGBA", rgba.size, "white")
    gray = Image.alpha_composite(white, rgba).convert("L")
    # Mode 1 is genuinely binary. RGBA conversion below only supplies the sample's
    # 32-bit BMP encoding; it does not introduce gray pixels or dithering.
    return gray.point([0] * 128 + [255] * 128, mode="1")


def find_inkscape() -> str | None:
    executable = shutil.which("inkscape")
    if executable:
        return executable
    if sys.platform == "win32":
        for variable, suffix in (("ProgramFiles", "Inkscape/bin/inkscape.exe"),
                                 ("ProgramFiles(x86)", "Inkscape/bin/inkscape.exe"),
                                 ("LOCALAPPDATA", "Programs/Inkscape/bin/inkscape.exe")):
            base = os.environ.get(variable)
            if base and (Path(base) / suffix).is_file():
                return str(Path(base) / suffix)
    return None


def render_svg(source: Path, rendered: Path, width: float, height: float,
               dpi: int, renderer: str):
    pixels = pixel_size(width, height, dpi)
    inkscape = find_inkscape() if renderer != "cairosvg" else None
    if renderer == "inkscape" and not inkscape:
        raise ValueError("--svg-renderer: Inkscape was requested but not found; install it or add it to PATH")
    if inkscape:
        command = [inkscape, str(source), "--export-area-page", "--export-type=png",
                   f"--export-filename={rendered}", f"--export-dpi={dpi}",
                   f"--export-width={pixels[0]}", f"--export-height={pixels[1]}",
                   "--export-background=white", "--export-background-opacity=1"]
        completed = subprocess.run(command, capture_output=True, text=True,
                                   encoding="utf-8", errors="replace", timeout=120,
                                   creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
        if completed.returncode != 0 or not rendered.is_file():
            raise ValueError(f"Inkscape rendering failed: {completed.stderr.strip() or completed.stdout.strip()}")
        return "Inkscape"
    try:
        import cairosvg
    except (ImportError, OSError) as exc:
        raise ValueError("SVG renderer unavailable; install Inkscape or CairoSVG and native Cairo. "
                         f"Details: {exc}") from exc
    print("Note: CairoSVG fallback may differ from Inkscape for text, fonts, or filters; "
          "check the label preview.", file=sys.stderr)
    # Keep CSS px at 96/in; output dimensions apply the requested raster DPI.
    # Parent dimensions resolve viewBox-only or single-dimension SVGs.
    cairosvg.svg2png(url=str(source), write_to=str(rendered), dpi=96,
                    parent_width=width * 96, parent_height=height * 96,
                    output_width=pixels[0], output_height=pixels[1])
    return "CairoSVG"


def prepare_image(source: Path, work: Path, tape_width: float,
                  tape_length: float | None, horizontal: float, vertical: float,
                  renderer: str = "auto"):
    from PIL import Image, ImageOps
    realistic = False
    rotate = False
    dpi = 360
    if source.suffix.lower() == ".svg":
        width, height = svg_dimensions(source)
        tolerance = 0.5 / 25.4 + 1e-12
        if abs(height - tape_width) <= tolerance:
            realistic = True
        elif abs(width - tape_width) <= tolerance:
            realistic, rotate = True, True
        dpi = 360 if realistic else 800
        rendered = work / "rendered.png"
        backend = render_svg(source, rendered, width, height, dpi, renderer)
        with Image.open(rendered) as opened:
            image = monochrome(opened)
        if rotate:
            image = image.transpose(Image.Transpose.ROTATE_90)
            width, height = height, width
    else:
        with Image.open(source) as opened:
            image = monochrome(ImageOps.exif_transpose(opened))

    printable_height = tape_width - 2 * vertical
    if realistic:
        length = width if tape_length is None else tape_length
        if length <= 2 * horizontal:
            raise ValueError("--horizontal-margin: twice the margin must be less "
                             "than the tape length (including automatically calculated length)")
        canvas_size = pixel_size(length, tape_width, dpi)
        before = image.histogram()[0]
        canvas = Image.new("1", canvas_size, 1)
        canvas.paste(image, ((canvas.width - image.width) // 2,
                             (canvas.height - image.height) // 2))
        box = (round(horizontal * dpi), round(vertical * dpi),
               round((length - horizontal) * dpi),
               round((tape_width - vertical) * dpi))
        if box[2] <= box[0] or box[3] <= box[1]:
            raise ValueError("margins: printable area is smaller than one pixel at 360 DPI")
        image = canvas.crop(box)
        # Report cropping of actual ink rather than silently discarding it.
        after = image.histogram()[0]
        if after < before:
            print(f"Warning: tape/margin cropping removes {before - after} black pixels; "
                  "check artwork near the label edges, tape size, or margins.", file=sys.stderr)
        placed_width, placed_height = length - 2 * horizontal, printable_height
        mode = "realistic SVG, 360 DPI, margins cropped" + ("; rotated 90 degrees" if rotate else "")
    else:
        ratio = image.width / image.height
        length = (printable_height * ratio + 2 * horizontal
                  if tape_length is None else tape_length)
        if not math.isfinite(length) or length <= 2 * horizontal:
            raise ValueError("--tape-length/--horizontal-margin: no finite positive printable length")
        placed_height = min(printable_height, (length - 2 * horizontal) / ratio)
        placed_width = placed_height * ratio
        mode = "SVG, 800 DPI" if source.suffix.lower() == ".svg" else "raster, original pixel dimensions"
    if source.suffix.lower() == ".svg":
        mode += f" ({backend})"
    return image, length, placed_width, placed_height, mode


def points(inches: float) -> str:
    return f"{inches * 72:.8f}".rstrip("0").rstrip(".") + "pt"


def write_xml(work: Path, source: Path, width: float, length: float,
              horizontal: float, vertical: float, image_width: float, image_height: float):
    root = ET.fromstring(LABEL_TEMPLATE)
    paper = root.find(".//style:paper", NAMESPACES)
    paper.set("width", points(width))
    paper.set("height", points(length))
    paper.set("autoLength", "false")
    if not math.isclose(width * 25.4, 24, rel_tol=0, abs_tol=1e-6):
        # Do not retain the sample's 24 mm preset for another tape width.
        paper.set("format", "")
    # Paper coordinates are portrait-oriented even though objects are landscape.
    for attr in ("marginTop", "marginBottom"):
        paper.set(attr, points(horizontal))
    for attr in ("marginLeft", "marginRight"):
        paper.set(attr, points(vertical))
    background = root.find(".//style:backGround", NAMESPACES)
    for key, value in dict(x=horizontal, y=vertical, width=length - 2 * horizontal,
                           height=width - 2 * vertical).items():
        background.set(key, points(value))
    placement = dict(x=(length - image_width) / 2, y=(width - image_height) / 2,
                     width=image_width, height=image_height)
    for query in (".//pt:objectStyle", ".//image:orgPos"):
        obj = root.find(query, NAMESPACES)
        for key, value in placement.items():
            obj.set(key, points(value))
    style = root.find(".//image:imageStyle", NAMESPACES)
    style.set("originalName", source.name)
    trimming = root.find(".//image:trimming", NAMESPACES)
    trimming.set("trimOrgWidth", points(image_width))
    trimming.set("trimOrgHeight", points(image_height))
    ET.ElementTree(root).write(work / "label.xml", encoding="utf-8", xml_declaration=True)

    meta = ET.Element(f"{{{NAMESPACES['meta']}}}properties")
    now = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    fields = {
        "meta:appName": "P-touch Editor 6.12.1.0 Windows",
        "dc:title": source.stem, "dc:subject": "", "dc:creator": "",
        "meta:keyword": "", "dc:description": "Generated by img_to_brother_label.py",
        "meta:template": "", "dcterms:created": now, "dcterms:modified": now,
        "meta:lastPrinted": "", "meta:modifiedBy": "", "meta:revision": "1",
        "meta:editTime": "0", "meta:numPages": "1", "meta:numWords": "0",
        "meta:numChars": "0", "meta:security": "0", "meta:transferScript": "",
    }
    namespaces = dict(NAMESPACES, dc="http://purl.org/dc/elements/1.1/",
                      dcterms="http://purl.org/dc/terms/")
    for name, value in fields.items():
        prefix, local = name.split(":")
        ET.SubElement(meta, f"{{{namespaces[prefix]}}}{local}").text = value
    ET.ElementTree(meta).write(work / "prop.xml", encoding="utf-8", xml_declaration=True)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input", type=Path, help="SVG or Pillow-supported raster image")
    parser.add_argument("output", nargs="?", type=Path, help="optional output path (default: input.lbx)")
    parser.add_argument("-o", "--output", dest="output_option", type=Path)
    parser.add_argument("--tape-width", type=argument_dimension("--tape-width"), default=24 / 25.4)
    parser.add_argument("--tape-length", type=argument_dimension("--tape-length", automatic=True),
                        default=None, nargs="?", const=None,
                        help="default automatic; auto, 0, and empty string also mean automatic")
    parser.add_argument("--horizontal-margin",
                        type=argument_dimension("--horizontal-margin", allow_zero=True),
                        default=DEFAULT_HORIZONTAL, help="per left/right end; default 5.6pt = 1.97556mm")
    parser.add_argument("--vertical-margin",
                        type=argument_dimension("--vertical-margin", allow_zero=True),
                        default=DEFAULT_VERTICAL, help="per top/bottom edge; default 8.4pt = 2.96333mm")
    parser.add_argument("--keep-temp", action="store_true", help="preserve the OS temporary workspace, even on error")
    parser.add_argument("--svg-renderer", choices=("auto", "inkscape", "cairosvg"), default="auto",
                        help="default: prefer Inkscape, fall back to CairoSVG")
    args = parser.parse_args(argv)
    if args.output and args.output_option:
        parser.error("output: specify either the positional output or --output, not both")
    source = args.input.expanduser().resolve()
    output = (args.output_option or args.output or source.with_suffix(".lbx")).expanduser().resolve()
    if not source.is_file():
        parser.error(f"input: file does not exist: {source}")
    if source == output:
        parser.error("output: must not overwrite the input image")
    if 2 * args.vertical_margin >= args.tape_width:
        parser.error("--vertical-margin: twice the margin must be less than --tape-width")
    if args.tape_length is not None and 2 * args.horizontal_margin >= args.tape_length:
        parser.error("--horizontal-margin: twice the margin must be less than --tape-length")
    if not output.parent.is_dir():
        parser.error(f"output: parent directory does not exist: {output.parent}")

    work = Path(tempfile.mkdtemp(prefix="brother-label-"))
    if args.keep_temp:
        print(f"Temporary files preserved at: {work}", file=sys.stderr)
    try:
        image, length, placed_width, placed_height, mode = prepare_image(
            source, work, args.tape_width, args.tape_length,
            args.horizontal_margin, args.vertical_margin, args.svg_renderer)
        image.convert("RGBA").save(work / "Object0.bmp", format="BMP")
        write_xml(work, source, args.tape_width, length, args.horizontal_margin,
                  args.vertical_margin, placed_width, placed_height)
        archive = work / "label.lbx"
        with ZipFile(archive, "w", ZIP_DEFLATED) as zipped:
            for name in ("label.xml", "Object0.bmp", "prop.xml"):
                zipped.write(work / name, arcname=name)
        shutil.copyfile(archive, output)
        print(f"Created: {output}")
        print(f"Tape: {length * 25.4:.4f} x {args.tape_width * 25.4:.4f} mm "
              f"({length:.5f} x {args.tape_width:.5f} in), landscape")
        print(f"Image: {image.width} x {image.height} pixels; {mode}; black/white threshold 128")
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        if not args.keep_temp:
            shutil.rmtree(work)


if __name__ == "__main__":
    raise SystemExit(main())
