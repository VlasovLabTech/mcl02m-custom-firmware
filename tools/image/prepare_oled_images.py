#!/usr/bin/env python3
"""Prepare source artwork for a 64x48 one-bit monochrome OLED.

The source PNGs are already pixel-art-like, but their antialiased edges contain
thousands of grey/color values.  The default pipeline first reconstructs a
high-resolution binary mask, then downsamples that mask as pixel coverage.  A
low coverage cutoff preserves narrow whiskers, steam strokes, punctuation and
small highlights better than converting a conventionally resized image to a
two-color indexed palette.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont, ImageOps


TARGET_SIZE = (64, 48)
SOURCE_GLOB = "*.png"
DEFAULT_COVERAGE = 0.34


def otsu_threshold(gray: Image.Image) -> int:
    """Return a global Otsu threshold for an 8-bit grayscale image."""
    histogram = gray.histogram()
    total = sum(histogram)
    weighted_sum = sum(level * count for level, count in enumerate(histogram))
    background_weight = 0
    background_sum = 0
    best_variance = -1.0
    best_threshold = 127

    for threshold, count in enumerate(histogram):
        background_weight += count
        if background_weight == 0:
            continue
        foreground_weight = total - background_weight
        if foreground_weight == 0:
            break

        background_sum += threshold * count
        background_mean = background_sum / background_weight
        foreground_mean = (weighted_sum - background_sum) / foreground_weight
        between_variance = (
            background_weight
            * foreground_weight
            * (background_mean - foreground_mean) ** 2
        )
        if between_variance > best_variance:
            best_variance = between_variance
            best_threshold = threshold

    return best_threshold


def flatten_to_gray(source: Image.Image) -> Image.Image:
    """Composite transparency on black and convert arbitrary PNG modes to L."""
    rgba = source.convert("RGBA")
    black = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
    black.alpha_composite(rgba)
    return black.convert("L")


def high_resolution_mask(gray: Image.Image) -> tuple[Image.Image, int]:
    """Remove antialias/color noise before spatial downsampling."""
    threshold = otsu_threshold(gray)
    mask = gray.point(lambda value: 255 if value > threshold else 0, mode="L")
    return mask, threshold


def coverage_resize(
    gray: Image.Image,
    coverage: float = DEFAULT_COVERAGE,
) -> tuple[Image.Image, int]:
    """Downsample a binary mask and retain pixels with enough white coverage."""
    mask, source_threshold = high_resolution_mask(gray)
    coverage_map = mask.resize(TARGET_SIZE, Image.Resampling.BOX)
    cutoff = round(255 * coverage)
    output = coverage_map.point(
        lambda value: 255 if value >= cutoff else 0,
        mode="1",
    )
    return output, source_threshold


def classic_resize(gray: Image.Image) -> Image.Image:
    """Reference method: Lanczos first, then a target-size Otsu threshold."""
    reduced = gray.resize(TARGET_SIZE, Image.Resampling.LANCZOS)
    threshold = otsu_threshold(reduced)
    return reduced.point(lambda value: 255 if value > threshold else 0, mode="1")


def floyd_steinberg_resize(gray: Image.Image) -> Image.Image:
    """Reference method: grayscale Lanczos with Floyd-Steinberg dithering."""
    reduced = ImageOps.autocontrast(
        gray.resize(TARGET_SIZE, Image.Resampling.LANCZOS),
        cutoff=0.5,
    )
    return reduced.convert("1", dither=Image.Dither.FLOYDSTEINBERG)


def load_font(size: int) -> ImageFont.ImageFont:
    candidates = (
        Path(r"C:\Windows\Fonts\consola.ttf"),
        Path(r"C:\Windows\Fonts\arial.ttf"),
    )
    for candidate in candidates:
        if candidate.exists():
            return ImageFont.truetype(str(candidate), size=size)
    return ImageFont.load_default()


def make_final_preview(images: list[tuple[str, Image.Image]], path: Path) -> None:
    """Write a nearest-neighbor 8x contact sheet for visual verification."""
    scale = 8
    cell_width = TARGET_SIZE[0] * scale
    image_height = TARGET_SIZE[1] * scale
    label_height = 34
    columns = 2
    rows = (len(images) + columns - 1) // columns
    sheet = Image.new(
        "RGB",
        (columns * cell_width, rows * (label_height + image_height)),
        "#202020",
    )
    draw = ImageDraw.Draw(sheet)
    font = load_font(22)

    for index, (name, image) in enumerate(images):
        column = index % columns
        row = index // columns
        x = column * cell_width
        y = row * (label_height + image_height)
        draw.text((x + 8, y + 4), name, fill="white", font=font)
        enlarged = image.convert("L").resize(
            (cell_width, image_height),
            Image.Resampling.NEAREST,
        )
        sheet.paste(enlarged.convert("RGB"), (x, y + label_height))

    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path, optimize=True)


def make_method_preview(
    sources: list[tuple[str, Image.Image]],
    path: Path,
) -> None:
    """Compare conventional, coverage and dither methods at a 4x zoom."""
    scale = 4
    cell_width = TARGET_SIZE[0] * scale
    image_height = TARGET_SIZE[1] * scale
    label_height = 28
    row_height = label_height + image_height
    coverages = (0.25, 0.34, 0.43)
    headings = ("classic", "coverage 25%", "coverage 34%", "coverage 43%", "dither")
    sheet = Image.new(
        "RGB",
        (len(headings) * cell_width, label_height + len(sources) * row_height),
        "#202020",
    )
    draw = ImageDraw.Draw(sheet)
    font = load_font(18)

    for column, heading in enumerate(headings):
        draw.text((column * cell_width + 7, 5), heading, fill="white", font=font)

    for row, (name, gray) in enumerate(sources):
        methods = [classic_resize(gray)]
        methods.extend(coverage_resize(gray, value)[0] for value in coverages)
        methods.append(floyd_steinberg_resize(gray))
        y = label_height + row * row_height
        draw.rectangle((0, y, sheet.width, y + label_height - 1), fill="#303030")
        draw.text((7, y + 4), name, fill="white", font=font)
        for column, image in enumerate(methods):
            enlarged = image.convert("L").resize(
                (cell_width, image_height),
                Image.Resampling.NEAREST,
            )
            sheet.paste(enlarged.convert("RGB"), (column * cell_width, y + label_height))

    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path, optimize=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "assets" / "images" / "source",
        help="Directory with source PNG files",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path(__file__).resolve().parents[2] / "assets" / "images" / "oled-64x48",
        help="Destination directory",
    )
    parser.add_argument(
        "--coverage",
        type=float,
        default=DEFAULT_COVERAGE,
        help="Minimum white source-pixel coverage, from 0 to 1 (default: 0.34)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not 0.0 < args.coverage <= 1.0:
        raise SystemExit("--coverage must be greater than 0 and at most 1")

    input_dir = args.input.resolve()
    output_dir = args.output.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    source_paths = [
        path
        for path in sorted(input_dir.glob(SOURCE_GLOB))
        if output_dir not in path.parents
    ]
    if not source_paths:
        raise SystemExit(f"No PNG files found in {input_dir}")

    final_images: list[tuple[str, Image.Image]] = []
    preview_sources: list[tuple[str, Image.Image]] = []
    for source_path in source_paths:
        with Image.open(source_path) as source:
            gray = flatten_to_gray(source)
        output, source_threshold = coverage_resize(gray, args.coverage)
        output_path = output_dir / source_path.name
        output.save(output_path, optimize=True)
        final_images.append((source_path.name, output.copy()))
        preview_sources.append((f"{source_path.name}  src Otsu={source_threshold}", gray))
        print(
            f"{source_path.name}: {source_path.stat().st_size} bytes -> "
            f"{output_path.stat().st_size} bytes, source threshold={source_threshold}"
        )

    previews_dir = output_dir / "_previews"
    make_final_preview(final_images, previews_dir / "final-contact-sheet-8x.png")
    make_method_preview(preview_sources, previews_dir / "method-comparison-4x.png")
    print(f"Prepared {len(final_images)} images in {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
