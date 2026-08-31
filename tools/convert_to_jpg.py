#!/usr/bin/env python3
"""Convert an image, or a directory of images, to JPEG."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageOps, UnidentifiedImageError


def positive_integer(value: str) -> int:
    """Return a positive integer suitable for an image dimension."""
    try:
        number = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"expected an integer, got {value!r}") from exc
    if number <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return number


def output_path(source: Path) -> Path:
    """Choose the JPEG path, avoiding replacement of an input .jpg file."""
    if source.suffix.lower() == ".jpg":
        return source.with_name(f"{source.stem}.s.jpg")
    return source.with_suffix(".jpg")


def is_generated_jpeg(path: Path) -> bool:
    """Return whether a path has the suffix used for converted JPEG inputs."""
    return path.name.lower().endswith(".s.jpg")


def flatten_onto_white(image: Image.Image) -> Image.Image:
    """Return an RGB image, compositing transparency onto white if necessary."""
    if image.mode in {"RGBA", "LA"} or "transparency" in image.info:
        rgba = image.convert("RGBA")
        background = Image.new("RGBA", rgba.size, "white")
        return Image.alpha_composite(background, rgba).convert("RGB")
    return image.convert("RGB")


def convert_image(
    source: Path,
    *,
    max_width: int | None,
    max_height: int | None,
) -> Path:
    """Convert one image to JPEG and return the destination path."""
    destination = output_path(source)
    with Image.open(source) as opened:
        image = ImageOps.exif_transpose(opened)
        image.load()

    if max_width is not None or max_height is not None:
        width_limit = max_width if max_width is not None else image.width
        height_limit = max_height if max_height is not None else image.height
        image.thumbnail((width_limit, height_limit), Image.Resampling.LANCZOS)

    image = flatten_onto_white(image)
    image.save(destination, format="JPEG", quality=95)
    return destination


def directory_images(directory: Path) -> list[Path]:
    """List recognized image files directly inside a directory."""
    image_extensions = {extension.lower() for extension in Image.registered_extensions()}
    return sorted(
        (
            path
            for path in directory.iterdir()
            if path.is_file()
            and path.suffix.lower() in image_extensions
            and not is_generated_jpeg(path)
        ),
        key=lambda path: path.name.lower(),
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert an image or a directory of images to JPEG."
    )
    parser.add_argument("path", type=Path, help="image file or directory to convert")
    parser.add_argument(
        "--max-width",
        type=positive_integer,
        help="maximum output width in pixels (images are never enlarged)",
    )
    parser.add_argument(
        "--max-height",
        type=positive_integer,
        help="maximum output height in pixels (images are never enlarged)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    source: Path = args.path

    if source.is_file():
        if is_generated_jpeg(source):
            print(f"Skipping already converted image: {source}")
            return 0
        sources = [source]
    elif source.is_dir():
        sources = directory_images(source)
        if not sources:
            print(f"No recognized image files found in {source}", file=sys.stderr)
            return 1
    else:
        print(f"Path does not exist: {source}", file=sys.stderr)
        return 2

    failed = False
    for image_path in sources:
        try:
            destination = convert_image(
                image_path,
                max_width=args.max_width,
                max_height=args.max_height,
            )
        except (OSError, UnidentifiedImageError) as exc:
            failed = True
            print(f"Could not convert {image_path}: {exc}", file=sys.stderr)
        else:
            print(f"{image_path} -> {destination}")

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
