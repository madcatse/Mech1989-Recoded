#!/usr/bin/env python3
"""Export MechWarrior FNT font resources.

The known MechWarrior FNT payload is:

    u8 width
    u8 height
    u8 first_code
    u8 glyph_count
    glyph_count * height row bytes, 1bpp, high bit at the left edge

Some files store that payload directly. Others wrap it in a tagged
``FNT:`` chunk with a little-endian payload length.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import string
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


SCHEMA_VERSION = 1


@dataclass(frozen=True)
class FntFont:
    path: Path
    data: bytes
    payload: bytes
    wrapper: str
    payload_offset: int
    width: int
    height: int
    first_code: int
    glyph_count: int

    @property
    def last_code(self) -> int:
        return self.first_code + self.glyph_count - 1


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def display_path(path: Path) -> str:
    return str(path).replace("\\", "/")


def safe_name(value: str) -> str:
    allowed = set(string.ascii_letters + string.digits + "-_.")
    safe = "".join(ch if ch in allowed else "_" for ch in value)
    return safe.strip("._") or "font"


def load_fnt(path: Path) -> FntFont:
    data = path.read_bytes()
    wrapper = "raw"
    payload_offset = 0
    payload = data

    if len(data) >= 8 and data[:4] == b"FNT:":
        payload_size = int.from_bytes(data[4:8], "little") & 0x7FFFFFFF
        if 8 + payload_size != len(data):
            raise ValueError(
                f"{path}: FNT: chunk length is {payload_size}, but file size is {len(data)}"
            )
        wrapper = "FNT:"
        payload_offset = 8
        payload = data[payload_offset:payload_offset + payload_size]

    if len(payload) < 4:
        raise ValueError(f"{path}: FNT payload is too small")

    width = payload[0]
    height = payload[1]
    first_code = payload[2]
    glyph_count = payload[3]
    expected_size = 4 + glyph_count * height

    if width <= 0 or width > 8:
        raise ValueError(f"{path}: unsupported glyph width {width}; expected 1..8")
    if height <= 0 or height > 16:
        raise ValueError(f"{path}: unsupported glyph height {height}; expected 1..16")
    if glyph_count <= 0 or glyph_count > 128:
        raise ValueError(f"{path}: unsupported glyph count {glyph_count}; expected 1..128")
    if expected_size != len(payload):
        raise ValueError(
            f"{path}: payload size mismatch; expected {expected_size}, got {len(payload)}"
        )

    return FntFont(
        path=path,
        data=data,
        payload=payload,
        wrapper=wrapper,
        payload_offset=payload_offset,
        width=width,
        height=height,
        first_code=first_code,
        glyph_count=glyph_count,
    )


def iter_glyphs(font: FntFont) -> Iterable[dict[str, object]]:
    glyph_data = font.payload[4:]
    printable = set(string.printable) - set("\r\n\t\x0b\x0c")
    for glyph_index in range(font.glyph_count):
        code = font.first_code + glyph_index
        glyph_offset = glyph_index * font.height
        row_bytes = glyph_data[glyph_offset:glyph_offset + font.height]
        bitmap_rows = []
        for row_byte in row_bytes:
            bitmap_rows.append(
                "".join("#" if ((row_byte >> (7 - x)) & 1) else "." for x in range(font.width))
            )
        char = chr(code) if 0 <= code <= 0x10FFFF and chr(code) in printable else ""
        yield {
            "index": glyph_index,
            "code_dec": code,
            "code_hex": f"0x{code:02X}",
            "char": char,
            "rows_hex": " ".join(f"{value:02X}" for value in row_bytes),
            "bitmap": "/".join(bitmap_rows),
        }


def render_glyph_sheet(font: FntFont, columns: int, scale: int) -> list[list[tuple[int, int, int]]]:
    columns = max(1, columns)
    scale = max(1, scale)
    rows = math.ceil(font.glyph_count / columns)
    cell_width = font.width + 2
    cell_height = font.height + 2
    image_width = columns * cell_width
    image_height = rows * cell_height
    pixels = [[(0, 0, 0) for _ in range(image_width)] for _ in range(image_height)]

    glyph_data = font.payload[4:]
    for glyph_index in range(font.glyph_count):
        cell_x = (glyph_index % columns) * cell_width + 1
        cell_y = (glyph_index // columns) * cell_height + 1
        glyph_offset = glyph_index * font.height
        for y in range(font.height):
            row_byte = glyph_data[glyph_offset + y]
            for x in range(font.width):
                if (row_byte >> (7 - x)) & 1:
                    pixels[cell_y + y][cell_x + x] = (255, 255, 255)

    if scale == 1:
        return pixels

    scaled: list[list[tuple[int, int, int]]] = []
    for row in pixels:
        expanded = []
        for pixel in row:
            expanded.extend([pixel] * scale)
        for _ in range(scale):
            scaled.append(list(expanded))
    return scaled


def write_ppm(path: Path, pixels: list[list[tuple[int, int, int]]]) -> None:
    height = len(pixels)
    width = len(pixels[0]) if height else 0
    with path.open("w", encoding="ascii", newline="\n") as handle:
        handle.write(f"P3\n{width} {height}\n255\n")
        for row in pixels:
            handle.write(" ".join(f"{r} {g} {b}" for r, g, b in row))
            handle.write("\n")


def write_rgb_image(path: Path, pixels: list[list[tuple[int, int, int]]], image_format: str) -> None:
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError("PNG/BMP output requires Pillow; use --image-format ppm") from exc

    height = len(pixels)
    width = len(pixels[0]) if height else 0
    image = Image.new("RGB", (width, height))
    image.putdata([pixel for row in pixels for pixel in row])
    image.save(path, format=image_format.upper())


def write_sheet_outputs(
    out_dir: Path,
    base_name: str,
    pixels: list[list[tuple[int, int, int]]],
    image_format: str,
    force: bool,
) -> list[str]:
    if image_format == "none":
        return []
    formats = ["ppm", "png", "bmp"] if image_format == "all" else [image_format]
    outputs = []
    for fmt in formats:
        out_path = out_dir / f"{base_name}.{fmt}"
        ensure_can_write(out_path, force)
        if fmt == "ppm":
            write_ppm(out_path, pixels)
        else:
            write_rgb_image(out_path, pixels, fmt)
        outputs.append(display_path(out_path))
    return outputs


def ensure_can_write(path: Path, force: bool) -> None:
    if path.exists() and not force:
        raise FileExistsError(f"refusing to overwrite existing file: {path}")


def write_json(path: Path, value: object, force: bool) -> None:
    ensure_can_write(path, force)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def write_glyph_csv(path: Path, glyphs: list[dict[str, object]], force: bool) -> None:
    ensure_can_write(path, force)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=["index", "code_dec", "code_hex", "char", "rows_hex", "bitmap"],
        )
        writer.writeheader()
        writer.writerows(glyphs)


def write_glyph_jsonl(path: Path, glyphs: list[dict[str, object]], force: bool) -> None:
    ensure_can_write(path, force)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        for glyph in glyphs:
            handle.write(json.dumps(glyph, ensure_ascii=False) + "\n")


def export_font(
    path: Path,
    out_root: Path,
    columns: int,
    scale: int,
    image_format: str,
    force: bool,
) -> dict[str, object]:
    font = load_fnt(path)
    name = safe_name(path.stem)
    out_dir = out_root / name
    out_dir.mkdir(parents=True, exist_ok=True)

    payload_path = out_dir / f"{name}.payload.bin"
    ensure_can_write(payload_path, force)
    payload_path.write_bytes(font.payload)

    glyphs = list(iter_glyphs(font))
    glyphs_csv_path = out_dir / "glyphs.csv"
    glyphs_jsonl_path = out_dir / "glyphs.jsonl"
    write_glyph_csv(glyphs_csv_path, glyphs, force)
    write_glyph_jsonl(glyphs_jsonl_path, glyphs, force)

    sheet_paths = write_sheet_outputs(
        out_dir=out_dir,
        base_name=f"{name}_{font.width}x{font.height}_{font.glyph_count}glyphs",
        pixels=render_glyph_sheet(font, columns=columns, scale=scale),
        image_format=image_format,
        force=force,
    )

    manifest = {
        "tool": "export_fnt",
        "schema_version": SCHEMA_VERSION,
        "source": {
            "path": display_path(path),
            "size": len(font.data),
            "sha256": sha256_bytes(font.data),
        },
        "wrapper": font.wrapper,
        "payload_offset": font.payload_offset,
        "payload_size": len(font.payload),
        "payload_sha256": sha256_bytes(font.payload),
        "layout": "u8_width_u8_height_u8_first_code_u8_glyph_count_rows_1bpp",
        "width": font.width,
        "height": font.height,
        "first_code": font.first_code,
        "first_code_hex": f"0x{font.first_code:02X}",
        "glyph_count": font.glyph_count,
        "last_code": font.last_code,
        "last_code_hex": f"0x{font.last_code:02X}",
        "outputs": {
            "payload": display_path(payload_path),
            "glyphs_csv": display_path(glyphs_csv_path),
            "glyphs_jsonl": display_path(glyphs_jsonl_path),
            "sheets": sheet_paths,
        },
    }
    write_json(out_dir / "manifest.json", manifest, force)
    return manifest


def collect_inputs(input_path: Path) -> list[Path]:
    if input_path.is_dir():
        return sorted(input_path.glob("*.FNT"))
    return [input_path]


def write_index(out_root: Path, manifests: list[dict[str, object]], force: bool) -> None:
    rows = []
    for manifest in manifests:
        source = manifest["source"]
        outputs = manifest["outputs"]
        rows.append({
            "source_path": source["path"],
            "source_size": source["size"],
            "wrapper": manifest["wrapper"],
            "payload_size": manifest["payload_size"],
            "width": manifest["width"],
            "height": manifest["height"],
            "first_code_hex": manifest["first_code_hex"],
            "glyph_count": manifest["glyph_count"],
            "manifest": display_path(out_root / safe_name(Path(source["path"]).stem) / "manifest.json"),
            "glyphs_csv": outputs["glyphs_csv"],
        })

    index_json = {
        "tool": "export_fnt",
        "schema_version": SCHEMA_VERSION,
        "font_count": len(manifests),
        "fonts": manifests,
    }
    write_json(out_root / "index.json", index_json, force)

    index_csv_path = out_root / "index.csv"
    ensure_can_write(index_csv_path, force)
    with index_csv_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "source_path", "source_size", "wrapper", "payload_size", "width", "height",
                "first_code_hex", "glyph_count", "manifest", "glyphs_csv",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Export MechWarrior raw/tagged FNT fonts to payload, metadata, glyph tables, and glyph sheets."
    )
    parser.add_argument(
        "input",
        nargs="?",
        default=Path("Sorted Original Files") / "FNT",
        type=Path,
        help="FNT file or directory with *.FNT files; defaults to Sorted Original Files/FNT",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("research") / "extract" / "fnt_export",
        help="output directory; defaults to research/extract/fnt_export",
    )
    parser.add_argument("--columns", type=int, default=16, help="glyph sheet columns")
    parser.add_argument("--scale", type=int, default=4, help="nearest-neighbor glyph sheet scale")
    parser.add_argument(
        "--image-format",
        choices=["ppm", "png", "bmp", "all", "none"],
        default="png",
        help="glyph sheet format",
    )
    parser.add_argument("--force", action="store_true", help="overwrite existing outputs")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    inputs = collect_inputs(args.input)
    if not inputs:
        raise FileNotFoundError(f"no FNT files found in {args.input}")

    args.out.mkdir(parents=True, exist_ok=True)
    manifests = [
        export_font(
            path=path,
            out_root=args.out,
            columns=args.columns,
            scale=args.scale,
            image_format=args.image_format,
            force=args.force,
        )
        for path in inputs
    ]
    write_index(args.out, manifests, force=args.force)
    print(f"Exported {len(manifests)} FNT file(s) to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
