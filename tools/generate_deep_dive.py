#!/usr/bin/env python3
"""Generate the two self-contained, static interactive schematic guides.

Install tools/requirements-deep-dive.txt, then run this script from any directory.
See doc/electrical-deep-dive/README.md for rendering and deployment details.
"""
from __future__ import annotations

import argparse
import copy
from dataclasses import dataclass
import hashlib
import html
from html.parser import HTMLParser
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile
from urllib.parse import quote, unquote, urlsplit
import xml.etree.ElementTree as ET

try:
    import markdown
    from PIL import Image, ImageChops, ImageDraw
except ImportError as exc:
    raise SystemExit("Install dependencies: python -m pip install -r tools/requirements-deep-dive.txt") from exc

from eagle_schematic_preview import DEFAULT_DPI, DEFAULT_EAGLECON_EXE, export_png

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "doc/electrical-deep-dive"
GITHUB_BASE = "https://github.com/frank26080115/Hot-Wand/blob/master/"
RENDER_VERSION = 1
TILE_SIZE = 512
OVERLAP = 1
CAD_DEVICESETS = {"PCB-OUTLINE", "PCB-ART-VERSION", "PCB-ART-VER-LITE", "STANDOFF"}
TITLE_ALIASES = {"fan ctrl": "fan control"}
VARIANTS = {
    "13mhz": ("hot-wand.sch", "Hot-Wand · 13.56 MHz"),
    "lite-470khz": ("hot-wand-lite.sch", "Hot-Wand Lite · 470 kHz"),
}
# These are trusted local EAGLE exports, which can exceed Pillow's web-image limit.
Image.MAX_IMAGE_PIXELS = None


@dataclass(frozen=True)
class Bounds:
    left: float
    bottom: float
    right: float
    top: float

    @property
    def width(self):
        return self.right - self.left

    @property
    def height(self):
        return self.top - self.bottom

    @property
    def center(self):
        return ((self.left + self.right) / 2, (self.bottom + self.top) / 2)

    def pad(self, amount):
        return Bounds(self.left - amount, self.bottom - amount, self.right + amount, self.top + amount)

    def union(self, other):
        return Bounds(min(self.left, other.left), min(self.bottom, other.bottom),
                      max(self.right, other.right), max(self.top, other.top))

    def hotspot(self):
        dx, dy = max(0.5, self.width * 0.1), max(0.5, self.height * 0.1)
        width, height = self.width + 2 * dx, self.height + 2 * dy
        width, height = max(width, height / 1.5), max(height, width / 1.5)
        x, y = self.center
        return Bounds(x - width / 2, y - height / 2, x + width / 2, y + height / 2)

    def distance(self, other):
        return math.hypot(self.center[0] - other.center[0], self.center[1] - other.center[1])


def bounds_of(points):
    points = list(points)
    if not points:
        raise ValueError("Cannot calculate bounds of empty geometry")
    xs, ys = zip(*points)
    return Bounds(min(xs), min(ys), max(xs), max(ys))


def union_all(boxes):
    boxes = iter(boxes)
    result = next(boxes)
    for box in boxes:
        result = result.union(box)
    return result


def transform_point(x, y, rotation="R0", offset=(0, 0)):
    """EAGLE mirrors the local X coordinate before rotating counterclockwise."""
    if "M" in rotation:
        x = -x
    angle = math.radians(float(rotation.split("R")[-1]))
    return (offset[0] + x * math.cos(angle) - y * math.sin(angle),
            offset[1] + x * math.sin(angle) + y * math.cos(angle))


def transform_bounds(box, rotation, offset):
    return bounds_of(transform_point(x, y, rotation, offset)
                     for x in (box.left, box.right) for y in (box.bottom, box.top))


def wire_bounds(x1, y1, x2, y2, curve=0, width=0):
    points = [(x1, y1), (x2, y2)]
    if curve and (x1 != x2 or y1 != y2):
        theta = math.radians(curve)
        dx, dy = x2 - x1, y2 - y1
        factor = 1 / (2 * math.tan(theta / 2))
        cx, cy = (x1 + x2) / 2 - dy * factor, (y1 + y2) / 2 + dx * factor
        radius = math.hypot(x1 - cx, y1 - cy)
        start = math.atan2(y1 - cy, x1 - cx)
        for a in (0, math.pi / 2, math.pi, 3 * math.pi / 2):
            sweep = (a - start) % (2 * math.pi) if theta > 0 else (start - a) % (2 * math.pi)
            if sweep <= abs(theta) + 1e-9:
                points.append((cx + radius * math.cos(a), cy + radius * math.sin(a)))
    return bounds_of(points).pad(width / 2)


def symbol_bounds(symbol):
    boxes = []
    for item in symbol:
        a = item.attrib
        num = lambda key, default="0": float(a.get(key, default))
        if item.tag == "wire":
            boxes.append(wire_bounds(num("x1"), num("y1"), num("x2"), num("y2"), num("curve"), num("width")))
        elif item.tag == "circle":
            r = num("radius") + num("width") / 2
            boxes.append(Bounds(num("x") - r, num("y") - r, num("x") + r, num("y") + r))
        elif item.tag == "rectangle":
            box = bounds_of([(num("x1"), num("y1")), (num("x2"), num("y2"))])
            cx, cy = box.center
            boxes.append(transform_bounds(Bounds(-box.width / 2, -box.height / 2, box.width / 2, box.height / 2),
                                          a.get("rot", "R0"), (cx, cy)))
        elif item.tag == "polygon":
            vertices = list(item)
            for v1, v2 in zip(vertices, vertices[1:] + vertices[:1]):
                boxes.append(wire_bounds(float(v1.get("x")), float(v1.get("y")),
                                         float(v2.get("x")), float(v2.get("y")),
                                         float(v1.get("curve", "0")), num("width")))
        elif item.tag == "pin":
            length = {"point": 0, "short": 2.54, "middle": 5.08, "long": 7.62}[a.get("length", "long")]
            origin = (num("x"), num("y"))
            boxes.append(bounds_of([origin, transform_point(length, 0, a.get("rot", "R0"), origin)]))
        elif item.tag == "text":
            if not (item.text or "").startswith(">"):
                # Static labels belong to the body; smashed >NAME/>VALUE labels do not.
                size = num("size")
                lines = (item.text or "").splitlines() or [""]
                width, height = max(map(len, lines)) * size, len(lines) * size * 1.5
                align = a.get("align", "bottom-left")
                x = -width / 2 if "center" in align else -width if "right" in align else 0
                y = -height if "top" in align else -height / 2 if align.startswith("center") else 0
                boxes.append(transform_bounds(Bounds(x, y, x + width, y + height),
                                              a.get("rot", "R0"), (num("x"), num("y"))))
        elif item.tag not in {"description"}:
            raise ValueError(f"Unsupported symbol primitive {item.tag} in {symbol.get('name')}")
    if not boxes:
        raise ValueError(f"Empty symbol {symbol.get('name')}")
    return union_all(boxes)


def read_schematic(path):
    tree = ET.parse(path)
    schematic = tree.find("./drawing/schematic")
    sheets = schematic.findall("./sheets/sheet")
    if len(sheets) != 1:
        raise ValueError(f"Expected one sheet in {path}; found {len(sheets)}")
    libraries = {lib.get("name"): lib for lib in schematic.findall("./libraries/library")}
    parts = {p.get("name"): p for p in schematic.findall("./parts/part")}
    components = {}
    for instance in sheets[0].findall("./instances/instance"):
        ref = instance.get("part")
        part = parts[ref]
        lib = libraries[part.get("library")]
        deviceset = lib.find(f"./devicesets/deviceset[@name='{part.get('deviceset')}']")
        gate = deviceset.find(f"./gates/gate[@name='{instance.get('gate')}']")
        symbol = lib.find(f"./symbols/symbol[@name='{gate.get('symbol')}']")
        pins = symbol.findall("./pin")
        if part.get("deviceset") in CAD_DEVICESETS or (pins and all(p.get("direction") == "sup" for p in pins)):
            continue
        box = transform_bounds(symbol_bounds(symbol), instance.get("rot", "R0"),
                               (float(instance.get("x")), float(instance.get("y"))))
        # Gate-definition x/y are library-editor defaults, not extra instance offsets.
        item = components.setdefault(ref, {"value": part.get("value", part.get("deviceset")), "boxes": []})
        item["boxes"].append(box)
    titles = [t for t in sheets[0].findall("./plain/text") if float(t.get("size")) >= 5]
    return tree, components, titles


class FragmentParser(HTMLParser):
    """Parse rendered HTML fragments without requiring an XML-compatible dialect."""
    VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"}

    def __init__(self, source):
        super().__init__(convert_charrefs=True)
        self.root = ET.Element("div")
        self.stack = [self.root]
        self.feed(source)

    def handle_starttag(self, tag, attrs):
        node = ET.SubElement(self.stack[-1], tag, dict((k, v or "") for k, v in attrs))
        if tag not in self.VOID:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in self.VOID:
            self.handle_endtag(tag)

    def handle_endtag(self, tag):
        for i in range(len(self.stack) - 1, 0, -1):
            if self.stack[i].tag == tag:
                del self.stack[i:]
                break

    def handle_data(self, data):
        node = self.stack[-1]
        if len(node):
            node[-1].tail = (node[-1].tail or "") + data
        else:
            node.text = (node.text or "") + data


def expand_heading_ranges(title):
    """Expand component ranges for parsing and individually linked section indexes."""
    pattern = r"\b([A-Z]+)(\d+)(?:\s*(?:-|–|—|thru|through|to)\s*)(?:\1)?(\d+)\b"
    def expand(match):
        prefix, first, last = match.groups()
        start, end = int(first), int(last)
        if end < start or end - start > 1000:
            raise ValueError(f"Invalid component range: {match.group()}")
        return ", ".join(f"{prefix}{i}" for i in range(start, end + 1))
    return re.sub(pattern, expand, title)


def heading_refs(title, known):
    expanded = expand_heading_ranges(title)
    return list(dict.fromkeys(ref for ref in re.findall(r"[A-Z]+\d+[A-Z]*", expanded) if ref in known))


def normalized_title(title):
    title = " ".join(title.lower().split())
    return TITLE_ALIASES.get(title, title)


def serialize(node):
    return ET.tostring(node, encoding="unicode", method="html")


def read_guide(path, components):
    source = markdown.markdown(path.read_text(encoding="utf-8"),
                               extensions=["tables", "fenced_code", "sane_lists", "toc"], output_format="html5")
    root = FragmentParser(source).root
    sections, groups, assignments, anchors = [], {}, {}, {}
    section = container = None
    for node in root:
        title = "".join(node.itertext()).strip()
        if node.tag == "h1":
            sid = "section-" + node.get("id")
            section = {"id": sid, "label": title, "intro": ET.Element("div"), "groups": []}
            sections.append(section)
            container = section["intro"]
            anchors[node.get("id")] = sid
        elif node.tag == "h2":
            if section is None:
                raise ValueError("Component heading has no major section")
            refs = heading_refs(title, components)
            if not refs:
                raise ValueError(f"No schematic refdes found in heading: {title}")
            gid = "entry-" + node.get("id")
            if gid in groups:
                raise ValueError(f"Duplicate component heading: {title}")
            container = ET.Element("div")
            group = {"id": gid, "label": title, "refs": refs, "body": container}
            groups[gid] = group
            section["groups"].append(gid)
            anchors[node.get("id")] = refs[0]
            for ref in refs:
                if ref in assignments:
                    raise ValueError(f"{ref} appears in multiple component headings")
                assignments[ref] = (gid, section["id"])
                anchors[ref.lower()] = ref
        elif container is not None:
            container.append(copy.deepcopy(node))
        elif title:
            raise ValueError("Content before the first major section")
    if not sections:
        raise ValueError(f"No major sections in {path}")
    return sections, groups, assignments, anchors


def rewrite_links(container, components, anchors, guide_path):
    for link in container.iter("a"):
        href = link.get("href", "")
        url = urlsplit(href)
        if not url.scheme and not url.netloc:
            target_path = (guide_path.parent / unquote(url.path)).resolve() if url.path else guide_path.resolve()
            internal = anchors.get(unquote(url.fragment)) if target_path == guide_path.resolve() else None
            if internal:
                link.set("href", "#" + quote(internal))
                link.set("data-target", internal)
                continue
            try:
                relative = target_path.relative_to(ROOT).as_posix()
            except ValueError as exc:
                raise ValueError(f"Link escapes repository: {href}") from exc
            href = GITHUB_BASE + quote(relative, safe="/")
            if url.query:
                href += "?" + url.query
            if url.fragment:
                href += "#" + url.fragment
            link.set("href", href)
        if href.startswith(("https://", "http://")):
            link.set("target", "_blank")
            link.set("rel", "noopener noreferrer")

    names = sorted(components, key=len, reverse=True)
    pattern = re.compile(r"(?<![A-Za-z0-9_$])(" + "|".join(map(re.escape, names)) + r")(?![A-Za-z0-9_$])")

    def fragments(text):
        start = 0
        for match in pattern.finditer(text or ""):
            yield (text[start:match.start()], None)
            yield (match.group(), match.group())
            start = match.end()
        yield ((text or "")[start:], None)

    def append_text(parent, text):
        if len(parent):
            parent[-1].tail = (parent[-1].tail or "") + text
        else:
            parent.text = (parent.text or "") + text

    def walk(node):
        if node.tag in {"a", "code", "pre", "script", "style"}:
            return
        old_text, children = node.text, list(node)
        node.text = None
        for child in children:
            node.remove(child)
        def add(text):
            for piece, ref in fragments(text):
                if ref:
                    ET.SubElement(node, "a", {"href": "#" + ref, "data-target": ref}).text = piece
                else:
                    append_text(node, piece)
        add(old_text)
        for child in children:
            tail = child.tail
            child.tail = None
            walk(child)
            node.append(child)
            add(tail)
    walk(container)


def add_registration(tree, frame, title=None):
    tree = copy.deepcopy(tree)
    sheet = tree.find("./drawing/schematic/sheets/sheet")
    plain = sheet.find("./plain")
    if title is not None:
        for child in list(plain):
            plain.remove(child)
        plain.append(copy.deepcopy(title))
        for name in ("instances", "nets", "busses"):
            child = sheet.find(name)
            if child is not None:
                sheet.remove(child)
    for x in (frame.left, frame.right):
        for y in (frame.bottom, frame.top):
            ET.SubElement(plain, "circle", {"x": str(x), "y": str(y), "radius": "1", "width": "0.254", "layer": "97"})
    layer = tree.find("./drawing/layers/layer[@number='97']")
    layer.set("visible", "yes")
    return tree


def registration_frame(tree, components):
    points = []
    sheet = tree.find("./drawing/schematic/sheets/sheet")
    for node in sheet.iter():
        for x, y in (("x", "y"), ("x1", "y1"), ("x2", "y2")):
            if x in node.attrib and y in node.attrib:
                points.append((float(node.get(x)), float(node.get(y))))
        if node.tag == "text":
            size = float(node.get("size", "1"))
            # Deliberately conservative; registration corners must be outside all ink.
            lines = (node.text or "").splitlines() or [""]
            extent = size * max(map(len, lines)) + size * len(lines) * 2
            x, y = float(node.get("x")), float(node.get("y"))
            points.extend([(x - extent, y - extent), (x + extent, y + extent)])
    boxes = [bounds_of(points)] + [b for c in components.values() for b in c["boxes"]]
    return union_all(boxes).pad(30)


def registered_export(tree, frame, path, dpi, eaglecon, title=None):
    # A fresh directory avoids both EAGLE's image-overwrite prompt and schematic
    # locks left behind by an interrupted export. Never reuse a loaded .sch path.
    with tempfile.TemporaryDirectory(prefix=path.stem + "-", dir=path.parent) as folder:
        work = Path(folder).resolve()
        if not work.is_relative_to((OUTPUT / "temp").resolve()):
            raise ValueError(f"Export workspace escapes the temporary directory: {work}")
        schematic, exported = work / "drawing.sch", work / "drawing.png"
        add_registration(tree, frame, title).write(schematic, encoding="utf-8", xml_declaration=True)
        export_png(eaglecon, schematic, exported, dpi)
        exported.replace(path)
    with Image.open(path) as source:
        image = source.convert("RGB")
    width, height = image.size
    size = math.ceil(dpi / 25.4 * 4 + 10)
    corners = [(0, 0, size, size), (width - size, 0, width, size),
               (0, height - size, size, height), (width - size, height - size, width, height)]
    centers = []
    for box in corners:
        crop = image.crop(box)
        bounds = ImageChops.difference(crop, Image.new("RGB", crop.size, "white")).getbbox()
        if bounds is None:
            raise ValueError(f"Registration mark missing in {path}")
        centers.append((box[0] + (bounds[0] + bounds[2] - 1) / 2,
                        box[1] + (bounds[1] + bounds[3] - 1) / 2))
    sx = ((centers[1][0] - centers[0][0]) + (centers[3][0] - centers[2][0])) / (2 * frame.width)
    sy = ((centers[2][1] - centers[0][1]) + (centers[3][1] - centers[1][1])) / (2 * frame.height)
    tx, ty = centers[0][0] - sx * frame.left, centers[0][1] + sy * frame.top
    if abs(sx - dpi / 25.4) > 0.02 or abs(sy - dpi / 25.4) > 0.02:
        raise ValueError(f"Unexpected image registration scale in {path}")
    draw = ImageDraw.Draw(image)
    for box in corners:
        draw.rectangle(box, fill="white")
    return image, [sx, sy, tx, ty]


def ink_bounds(image):
    bounds = ImageChops.difference(image, Image.new("RGB", image.size, "white")).getbbox()
    if bounds is None:
        raise ValueError("EAGLE export contains no drawing")
    return bounds


def image_box(box, transform):
    sx, sy, tx, ty = transform
    return [round(box.left * sx + tx, 3), round(-box.top * sy + ty, 3),
            round(box.width * sx, 3), round(box.height * sy, 3)]


def tile_image(image, folder):
    expected = set()
    max_level = math.ceil(math.log2(max(image.size)))
    # Go downwards, retaining at most the current and previous pyramid image.
    level_image = image
    for level in range(max_level, -1, -1):
        if level != max_level:
            next_size = (math.ceil(image.width / 2 ** (max_level - level)),
                         math.ceil(image.height / 2 ** (max_level - level)))
            resized = level_image.resize(next_size, Image.Resampling.LANCZOS)
            if level_image is not image:
                level_image.close()
            level_image = resized
        level_dir = folder / str(level)
        level_dir.mkdir(parents=True, exist_ok=True)
        for row in range(math.ceil(level_image.height / TILE_SIZE)):
            for col in range(math.ceil(level_image.width / TILE_SIZE)):
                box = (max(0, col * TILE_SIZE - OVERLAP), max(0, row * TILE_SIZE - OVERLAP),
                       min(level_image.width, (col + 1) * TILE_SIZE + OVERLAP),
                       min(level_image.height, (row + 1) * TILE_SIZE + OVERLAP))
                path = level_dir / f"{col}_{row}.png"
                level_image.crop(box).save(path, optimize=True)
                expected.add(path.relative_to(folder).as_posix())
    if level_image is not image:
        level_image.close()
    for path in folder.rglob("*.png"):
        if path.relative_to(folder).as_posix() not in expected:
            path.unlink()
    return sorted(expected)


def render_schematic(variant, path, tree, components, titles, dpi, eaglecon, force):
    work = OUTPUT / "temp" / variant
    work.mkdir(parents=True, exist_ok=True)
    tile_dir = OUTPUT / "tiles" / variant
    cache_path = work / "render.json"
    key = hashlib.sha256(path.read_bytes() + json.dumps([RENDER_VERSION, dpi, TILE_SIZE, OVERLAP]).encode()
                         + (ROOT / "tools/eagle_schematic_preview.py").read_bytes()).hexdigest()
    if not force and cache_path.exists():
        cached = json.loads(cache_path.read_text(encoding="utf-8"))
        if cached.get("key") == key and all((tile_dir / p).is_file() for p in cached["tiles"]):
            print(f"{variant}: reusing cached rendering", flush=True)
            return cached
    if not eaglecon.is_file():
        raise FileNotFoundError(f"EAGLE executable not found: {eaglecon}. Use --eaglecon or EAGLECON_EXE.")
    frame = registration_frame(tree, components)
    print(f"{variant}: rendering schematic at {dpi} DPI", flush=True)
    image, transform = registered_export(tree, frame, work / "registered.png", dpi, eaglecon)
    crop = ink_bounds(image)
    pad = math.ceil(dpi / 25.4 * 2)
    crop = (max(0, crop[0] - pad), max(0, crop[1] - pad),
            min(image.width, crop[2] + pad), min(image.height, crop[3] + pad))
    cropped = image.crop(crop)
    image.close()
    transform[2] -= crop[0]
    transform[3] -= crop[1]
    title_boxes = []
    for i, title in enumerate(titles):
        print(f"{variant}: measuring title {i + 1}/{len(titles)}", flush=True)
        mask, tr = registered_export(tree, frame, work / f"title-{i}.png", 100, eaglecon, title)
        left, top, right, bottom = ink_bounds(mask)
        sx, sy, tx, ty = tr
        title_boxes.append([ (left - tx) / sx, (ty - bottom) / sy,
                             (right - tx) / sx, (ty - top) / sy ])
        mask.close()
    print(f"{variant}: tiling {cropped.width} × {cropped.height} image", flush=True)
    tiles = tile_image(cropped, tile_dir)
    result = {"key": key, "width": cropped.width, "height": cropped.height,
              "transform": transform, "titleBoxes": title_boxes, "tiles": tiles}
    cropped.close()
    cache_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def build_content(guide, components, titles, rendering):
    sections, groups, assignments, anchors = read_guide(guide, components)
    by_section = {s["id"]: s for s in sections}
    title_names = {}
    for index, title in enumerate(titles):
        key = normalized_title(title.text or "")
        if key in title_names:
            raise ValueError(f"Ambiguous schematic title: {title.text}")
        title_names[key] = index
    section_boxes = {}
    for section in sections:
        key = normalized_title(section["label"])
        if key not in title_names:
            raise ValueError(f"No schematic title matches {section['label']}")
        section_boxes[section["id"]] = Bounds(*rendering["titleBoxes"][title_names.pop(key)]).pad(1)
    if title_names:
        raise ValueError(f"Schematic titles missing guide sections: {', '.join(title_names)}")
    missing = set(components) - set(assignments)
    if not assignments:
        raise ValueError("Guide has no documented components")
    documented = tuple(assignments)
    for ref in sorted(missing):
        box = union_all(components[ref]["boxes"])
        nearest = min(documented, key=lambda other: (box.distance(union_all(components[other]["boxes"])), other))
        parent = assignments[nearest][1]
        gid = "fallback-" + ref
        body = ET.Element("div")
        if components[ref]["value"]:
            ET.SubElement(body, "p").text = "Value: " + components[ref]["value"]
        ET.SubElement(body, "p").text = "This component does not have an explanation yet."
        groups[gid] = {"id": gid, "label": ref, "refs": [ref], "body": body}
        assignments[ref] = (gid, parent)
        by_section[parent]["groups"].append(gid)
        anchors[ref.lower()] = ref
        print(f"WARNING: {ref}: no guide entry; inferred parent {by_section[parent]['label']} from {nearest}", file=sys.stderr)
    targets, zones, templates = {}, [], {}
    transform = rendering["transform"]
    for group in groups.values():
        rewrite_links(group["body"], components, anchors, guide)
        templates[group["id"]] = serialize(group["body"])
    for ref, item in components.items():
        gid, parent = assignments[ref]
        boxes = [box.hotspot() for box in item["boxes"]]
        targets[ref] = {"id": ref, "label": ref, "kind": "component", "entry": gid,
                        "parent": parent, "group": groups[gid]["label"], "value": item["value"],
                        "bounds": image_box(union_all(boxes), transform)}
        for box in boxes:
            zones.append({"target": ref, "bounds": image_box(box, transform)})
    for section in sections:
        sid = section["id"]
        body = section["intro"]
        rewrite_links(body, components, anchors, guide)
        ET.SubElement(body, "h2").text = "Components in this section"
        listing = ET.SubElement(body, "ul", {"class": "component-list"})
        for gid in section["groups"]:
            group = groups[gid]
            row = ET.SubElement(listing, "li")
            if len(group["refs"]) > 1:
                # Keep each named member navigable when several components share
                # one explanation, including suffixes such as R46 and R46B.
                row.text = expand_heading_ranges(group["label"])
                rewrite_links(row, components, anchors, guide)
            else:
                ref = group["refs"][0]
                ET.SubElement(row, "a", {"href": "#" + ref, "data-target": ref}).text = group["label"]
        templates[sid] = serialize(body)
        boxes = [section_boxes[sid]] + [union_all(components[ref]["boxes"]) for ref in components if assignments[ref][1] == sid]
        targets[sid] = {"id": sid, "label": section["label"], "kind": "section", "entry": sid,
                        "bounds": image_box(union_all(boxes).pad(3), transform)}
        zones.append({"target": sid, "bounds": image_box(section_boxes[sid], transform)})
    return targets, zones, templates, [s["id"] for s in sections]


def page_html(variant, label, rendering, targets, zones, templates, sections):
    data = {"variant": variant, "label": label, "width": rendering["width"], "height": rendering["height"],
            "pixelsPerMm": sum(rendering["transform"][:2]) / 2, "renderKey": rendering["key"],
            "targets": targets, "zones": zones, "sections": sections,
            "tileSource": {"Image": {"xmlns": "http://schemas.microsoft.com/deepzoom/2008", "Url": f"tiles/{variant}/",
                                       "Format": "png", "Overlap": OVERLAP, "TileSize": TILE_SIZE,
                                       "Size": {"Width": rendering["width"], "Height": rendering["height"]}}}}
    payload = json.dumps(data, ensure_ascii=False, separators=(",", ":")).replace("<", "\\u003c")
    baked = "\n".join(f'<template id="{html.escape(key, quote=True)}">{value}</template>' for key, value in templates.items())
    return f'''<!doctype html>
<!-- Generated by tools/generate_deep_dive.py. Edit the Markdown or shared assets. -->
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{html.escape(label)} — Interactive schematic</title><link rel="stylesheet" href="assets/deep-dive.css">
<script defer src="assets/vendor/openseadragon/openseadragon.min.js"></script><script defer src="assets/deep-dive.js"></script></head>
<body><header class="toolbar" aria-label="Schematic controls">
<span class="brand">{html.escape(label)}</span>
<label class="target-label">Explore <select id="target-picker" aria-label="Select a component or section"><option value="">Choose a component or section…</option></select></label>
<div class="buttons"><button data-action="zoom-in" aria-label="Zoom in">+</button><button data-action="zoom-out" aria-label="Zoom out">−</button><button data-action="fit">Fit</button>
<button data-action="left" aria-label="Pan left">←</button><button data-action="up" aria-label="Pan up">↑</button><button data-action="down" aria-label="Pan down">↓</button><button data-action="right" aria-label="Pan right">→</button><button id="help-open">Help</button></div>
</header><main id="workspace"><div id="schematic" tabindex="0" role="region" aria-label="Interactive schematic. Drag to pan, use the wheel to zoom, or choose an item above."></div>
<aside id="inspector" role="dialog" aria-labelledby="inspector-title" hidden><header class="inspector-header"><div><h1 id="inspector-title"></h1><p id="inspector-group"></p></div><button id="inspector-close" aria-label="Close component information">×</button></header>
<iframe id="information" title="Schematic explanation"></iframe></aside></main>
<p id="status" class="sr-only" role="status" aria-live="polite">Loading schematic…</p>
<dialog id="help"><h1>Explore this schematic</h1><p>Click a dotted component outline or a large section title to learn what it does. Related component references open their own explanations.</p>
<ul><li>Drag to pan. Use the mouse wheel, trackpad, or pinch gesture to zoom.</li><li>Use the controls above or select a component or section from Explore.</li><li>With the schematic focused, use arrow keys to pan, + and − to zoom, and Home to fit the whole drawing.</li><li>Press Escape or use × to close an explanation. You can keep moving the schematic while reading.</li><li>On a phone, the drawing uses one third of the display while an explanation is open. Rotate your phone to change the split.</li><li>Links to supporting documents and shopping pages open a new tab.</li></ul>
<p><a href="{GITHUB_BASE}doc/electrical-deep-dive/electrical-deep-dive-{variant}.md" target="_blank" rel="noopener noreferrer">Read the complete written guide</a></p><form method="dialog"><button>Close help</button></form></dialog>
<noscript>This interactive schematic needs JavaScript. <a href="{GITHUB_BASE}doc/electrical-deep-dive/electrical-deep-dive-{variant}.md">Read the written guide</a>.</noscript>
<script id="deep-dive-data" type="application/json">{payload}</script>
{baked}
</body></html>
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--variant", choices=list(VARIANTS), help="build only one guide (default: both)")
    parser.add_argument("--dpi", type=int, default=DEFAULT_DPI)
    parser.add_argument("--eaglecon", type=Path, default=Path(os.environ.get("EAGLECON_EXE", DEFAULT_EAGLECON_EXE)))
    parser.add_argument("--force-render", action="store_true", help="ignore the schematic image cache")
    args = parser.parse_args()
    if not 50 <= args.dpi <= 1200:
        parser.error("--dpi must be between 50 and 1200")
    for variant in ([args.variant] if args.variant else VARIANTS):
        schematic_name, label = VARIANTS[variant]
        path = ROOT / "electrical" / schematic_name
        guide = OUTPUT / f"electrical-deep-dive-{variant}.md"
        tree, components, titles = read_schematic(path)
        # Validate guide structure before invoking the relatively expensive renderer.
        read_guide(guide, components)
        rendering = render_schematic(variant, path, tree, components, titles, args.dpi, args.eaglecon, args.force_render)
        targets, zones, templates, sections = build_content(guide, components, titles, rendering)
        output = OUTPUT / f"electrical-deep-dive-{variant}.html"
        temporary = output.with_suffix(".html.tmp")
        temporary.write_text(page_html(variant, label, rendering, targets, zones, templates, sections), encoding="utf-8", newline="\n")
        temporary.replace(output)
        print(f"{variant}: wrote {output.relative_to(ROOT)} ({len(components)} components, {len(sections)} sections)", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, RuntimeError) as exc:
        raise SystemExit(str(exc)) from exc
