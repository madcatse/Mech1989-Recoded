#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import struct
import sys
from pathlib import Path


SCHEMA_VERSION = 1

KNOWN_TAGS = {"BMP:", "PAL:", "FNT:", "INF:", "BIN:", "EGA:", "CGA:"}

EGA_PALETTE = [
    (0x00, 0x00, 0x00),
    (0x00, 0x00, 0xAA),
    (0x00, 0xAA, 0x00),
    (0x00, 0xAA, 0xAA),
    (0xAA, 0x00, 0x00),
    (0xAA, 0x00, 0xAA),
    (0xAA, 0x55, 0x00),
    (0xAA, 0xAA, 0xAA),
    (0x55, 0x55, 0x55),
    (0x55, 0x55, 0xFF),
    (0x55, 0xFF, 0x55),
    (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55),
    (0xFF, 0x55, 0xFF),
    (0xFF, 0xFF, 0x55),
    (0xFF, 0xFF, 0xFF),
]

CGA_RGBI_PALETTE = [
    (0x00, 0x00, 0x00),
    (0x00, 0x00, 0xAA),
    (0x00, 0xAA, 0x00),
    (0x00, 0xAA, 0xAA),
    (0xAA, 0x00, 0x00),
    (0xAA, 0x00, 0xAA),
    (0xAA, 0x55, 0x00),
    (0xAA, 0xAA, 0xAA),
    (0x55, 0x55, 0x55),
    (0x55, 0x55, 0xFF),
    (0x55, 0xFF, 0x55),
    (0x55, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55),
    (0xFF, 0x55, 0xFF),
    (0xFF, 0xFF, 0x55),
    (0xFF, 0xFF, 0xFF),
]

CGA_INDEX_PREVIEW_PALETTE = [
    (0x00, 0x00, 0x00),
    (0x00, 0xAA, 0xAA),
    (0xAA, 0x00, 0xAA),
    (0xFF, 0xFF, 0xFF),
]


class Reader:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()

    @property
    def size(self) -> int:
        return len(self.data)

    def u32le(self, offset: int) -> int:
        self.require(offset, 4)
        return int.from_bytes(self.data[offset:offset + 4], "little")

    def slice(self, offset: int, length: int) -> bytes:
        self.require(offset, length)
        return self.data[offset:offset + length]

    def require(self, offset: int, length: int) -> None:
        if offset < 0 or length < 0 or offset + length > self.size:
            raise ValueError(f"range outside file: offset={offset} length={length} size={self.size}")


def is_tag(data: bytes) -> bool:
    if len(data) != 4:
        return False
    try:
        return data.decode("ascii") in KNOWN_TAGS
    except UnicodeDecodeError:
        return False


def safe_name(value: str) -> str:
    keep = []
    for char in value:
        keep.append(char if char.isalnum() or char in "._-" else "_")
    return "".join(keep).strip("._") or "unnamed"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def relpath(path: Path) -> str:
    return str(path).replace("\\", "/")


def write_bytes_once(path: Path, data: bytes, force: bool) -> None:
    if path.exists() and not force:
        raise FileExistsError(f"refusing to overwrite existing file: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def write_text_once(path: Path, text: str, force: bool) -> None:
    if path.exists() and not force:
        raise FileExistsError(f"refusing to overwrite existing file: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def parse_tagged(reader: Reader, start: int, limit: int, depth: int = 0, prefix: str = "chunk") -> list[dict[str, object]]:
    entries: list[dict[str, object]] = []
    offset = start
    local_index = 0

    while offset + 8 <= limit:
        tag_bytes = reader.slice(offset, 4)
        if not is_tag(tag_bytes):
            break

        tag = tag_bytes.decode("ascii")
        raw_length = reader.u32le(offset + 4)
        payload_size = raw_length & 0x7FFFFFFF
        nested = (raw_length & 0x80000000) != 0
        payload_offset = offset + 8
        end_offset = payload_offset + payload_size
        if end_offset > limit:
            raise ValueError(
                f"{reader.path}: {tag} payload ends at 0x{end_offset:X}, outside limit 0x{limit:X}"
            )

        entry = {
            "id": f"{prefix}_{len(entries):04d}_{safe_name(tag[:-1])}_{offset:06X}",
            "tag": tag,
            "offset": offset,
            "header_size": 8,
            "payload_offset": payload_offset,
            "payload_size": payload_size,
            "end_offset": end_offset,
            "depth": depth,
            "local_index": local_index,
            "has_nested_flag": nested,
            "raw_length_hex": f"0x{raw_length:08X}",
            "sha256": sha256_bytes(reader.slice(payload_offset, payload_size)),
        }
        entries.append(entry)

        if nested and payload_offset + 8 <= end_offset and is_tag(reader.slice(payload_offset, 4)):
            entries.extend(parse_tagged(reader, payload_offset, end_offset, depth + 1, prefix))

        offset = end_offset
        local_index += 1

    return entries


def decode_ega_payload(payload: bytes) -> dict[str, object]:
    if len(payload) != 128:
        raise ValueError(f"PAL:EGA payload must be 128 bytes, got {len(payload)}")

    pairs = [payload[index:index + 2] for index in range(0, len(payload), 2)]
    duplicate_pair_count = sum(1 for pair in pairs if len(pair) == 2 and pair[0] == pair[1])
    banks = []

    for bank_index in range(4):
        colors = []
        for color_index, pair in enumerate(pairs[bank_index * 16:(bank_index + 1) * 16]):
            raw = pair[0]
            low_nibble = raw & 0x0F
            high_nibble = (raw >> 4) & 0x0F
            colors.append({
                "index": color_index,
                "raw": raw,
                "raw_hex": f"0x{raw:02X}",
                "low_nibble": low_nibble,
                "high_nibble": high_nibble,
                "low_rgb": EGA_PALETTE[low_nibble],
                "high_rgb": EGA_PALETTE[high_nibble],
                "pair_hex": pair.hex().upper(),
                "pair_duplicate": "yes" if pair[0] == pair[1] else "no",
            })
        banks.append({"bank": bank_index, "colors": colors})

    return {
        "layout": "ega_pair_banks_tentative",
        "payload_size": len(payload),
        "pair_count": len(pairs),
        "duplicate_pair_count": duplicate_pair_count,
        "banks": banks,
    }


def cga_word_to_pixels(word: int) -> list[int]:
    pixels = []
    for byte in word.to_bytes(2, "little"):
        pixels.extend([
            (byte >> 6) & 0x03,
            (byte >> 4) & 0x03,
            (byte >> 2) & 0x03,
            byte & 0x03,
        ])
    return pixels


def decode_cga_payload(payload: bytes) -> dict[str, object]:
    if len(payload) != 162:
        raise ValueError(f"PAL:CGA payload must be 162 bytes, got {len(payload)}")

    words = [int.from_bytes(payload[index:index + 2], "little") for index in range(0, len(payload), 2)]
    control_word = words[0]
    table_words = words[1:]
    if len(table_words) != 80:
        raise ValueError(f"PAL:CGA table body must be 80 words, got {len(table_words)}")

    tables = []
    for table_index in range(5):
        rows = []
        for color_index, word in enumerate(table_words[table_index * 16:(table_index + 1) * 16]):
            pixels = cga_word_to_pixels(word)
            rows.append({
                "index": color_index,
                "word": word,
                "word_hex": f"0x{word:04X}",
                "bytes_hex": word.to_bytes(2, "little").hex().upper(),
                "pixels": pixels,
                "pixels_text": "".join(str(pixel) for pixel in pixels),
            })
        tables.append({"table": table_index, "entries": rows})

    return {
        "layout": "cga_2bpp_lookup_tables",
        "payload_size": len(payload),
        "control_word": control_word,
        "control_word_hex": f"0x{control_word:04X}",
        "control_low_nibble": control_word & 0x0F,
        "control_low_nibble_rgbi": CGA_RGBI_PALETTE[control_word & 0x0F],
        "table_count": len(tables),
        "entries_per_table": 16,
        "word_encoding": "each u16le word is two CGA bytes; each byte contains four 2-bit pixels in high-to-low bit-pair order",
        "tables": tables,
        "first_32_hex": payload[:32].hex().upper(),
    }


def cga_table_pixels(
    table: dict[str, object],
    palette: list[tuple[int, int, int]],
    pixel_scale: int,
    cell_height: int,
) -> list[list[tuple[int, int, int]]]:
    entries = table.get("entries")
    if not isinstance(entries, list):
        raise ValueError("invalid CGA table entries")

    row_pixels: list[tuple[int, int, int]] = []
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        pixels = entry.get("pixels")
        if not isinstance(pixels, list):
            raise ValueError("invalid CGA pixel pattern")
        for pixel in pixels:
            row_pixels.extend([palette[int(pixel) & 0x03]] * pixel_scale)

    return [list(row_pixels) for _y in range(cell_height)]


def palette_from_bank(bank: dict[str, object], interpretation: str) -> list[tuple[int, int, int]]:
    key = "high_rgb" if interpretation == "high" else "low_rgb"
    colors = bank.get("colors")
    if not isinstance(colors, list):
        raise ValueError("invalid EGA bank data")
    palette = []
    for color in colors:
        if not isinstance(color, dict) or key not in color:
            raise ValueError("invalid EGA color data")
        rgb = color[key]
        if not isinstance(rgb, (list, tuple)) or len(rgb) != 3:
            raise ValueError("invalid RGB triplet")
        palette.append((int(rgb[0]), int(rgb[1]), int(rgb[2])))
    return palette


def swatch_pixels(palette: list[tuple[int, int, int]], cell_width: int, cell_height: int) -> list[list[tuple[int, int, int]]]:
    pixels: list[list[tuple[int, int, int]]] = []
    for _y in range(cell_height):
        row: list[tuple[int, int, int]] = []
        for color in palette:
            row.extend([color] * cell_width)
        pixels.append(row)
    return pixels


def write_ppm(path: Path, pixels: list[list[tuple[int, int, int]]], force: bool) -> None:
    if path.exists() and not force:
        raise FileExistsError(f"refusing to overwrite existing file: {path}")
    height = len(pixels)
    width = len(pixels[0]) if height else 0
    lines = [f"P3\n{width} {height}\n255"]
    for row in pixels:
        lines.append(" ".join(f"{r} {g} {b}" for r, g, b in row))
    write_text_once(path, "\n".join(lines) + "\n", force)


def write_bmp(path: Path, pixels: list[list[tuple[int, int, int]]], force: bool) -> None:
    if path.exists() and not force:
        raise FileExistsError(f"refusing to overwrite existing file: {path}")
    height = len(pixels)
    width = len(pixels[0]) if height else 0
    row_stride = ((width * 3 + 3) // 4) * 4
    pixel_data_size = row_stride * height
    file_size = 14 + 40 + pixel_data_size

    data = bytearray()
    data.extend(b"BM")
    data.extend(struct.pack("<IHHI", file_size, 0, 0, 14 + 40))
    data.extend(struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, pixel_data_size, 0, 0, 0, 0))
    padding = b"\x00" * (row_stride - width * 3)
    for row in reversed(pixels):
        for r, g, b in row:
            data.extend(bytes((b, g, r)))
        data.extend(padding)
    write_bytes_once(path, bytes(data), force)


def write_png(path: Path, pixels: list[list[tuple[int, int, int]]], force: bool) -> None:
    if path.exists() and not force:
        raise FileExistsError(f"refusing to overwrite existing file: {path}")
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError("PNG output requires Pillow; use --image-format bmp or ppm without extra packages") from exc

    height = len(pixels)
    width = len(pixels[0]) if height else 0
    image = Image.new("RGB", (width, height))
    image.putdata([pixel for row in pixels for pixel in row])
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, format="PNG")


def write_image_outputs(
    base_path: Path,
    pixels: list[list[tuple[int, int, int]]],
    image_format: str,
    force: bool,
) -> list[str]:
    formats = ["bmp", "ppm", "png"] if image_format == "all" else [image_format]
    written = []
    for current_format in formats:
        path = base_path.parent / f"{base_path.name}.{current_format}"
        if current_format == "bmp":
            write_bmp(path, pixels, force)
        elif current_format == "ppm":
            write_ppm(path, pixels, force)
        elif current_format == "png":
            write_png(path, pixels, force)
        else:
            raise ValueError(f"unsupported image format: {current_format}")
        written.append(relpath(path))
    return written


def write_chunks_csv(path: Path, entries: list[dict[str, object]], force: bool) -> None:
    rows = []
    for entry in entries:
        rows.append({
            "id": entry["id"],
            "tag": entry["tag"],
            "offset_hex": f"0x{int(entry['offset']):06X}",
            "header_size": entry["header_size"],
            "payload_offset_hex": f"0x{int(entry['payload_offset']):06X}",
            "payload_size": entry["payload_size"],
            "end_offset_hex": f"0x{int(entry['end_offset']):06X}",
            "depth": entry["depth"],
            "local_index": entry["local_index"],
            "has_nested_flag": "yes" if entry["has_nested_flag"] else "no",
            "raw_length_hex": entry["raw_length_hex"],
            "sha256": entry["sha256"],
        })
    write_csv(path, rows, force)


def write_ega_csv(path: Path, source: Path, ega: dict[str, object], force: bool) -> None:
    rows = []
    banks = ega.get("banks", [])
    if not isinstance(banks, list):
        raise ValueError("invalid EGA bank list")
    for bank in banks:
        if not isinstance(bank, dict):
            continue
        colors = bank.get("colors", [])
        if not isinstance(colors, list):
            continue
        for color in colors:
            if not isinstance(color, dict):
                continue
            low_rgb = color["low_rgb"]
            high_rgb = color["high_rgb"]
            rows.append({
                "source_file": relpath(source),
                "bank": bank["bank"],
                "index": color["index"],
                "raw_hex": color["raw_hex"],
                "low_nibble": color["low_nibble"],
                "high_nibble": color["high_nibble"],
                "low_rgb": " ".join(str(value) for value in low_rgb),
                "high_rgb": " ".join(str(value) for value in high_rgb),
                "pair_hex": color["pair_hex"],
                "pair_duplicate": color["pair_duplicate"],
            })
    write_csv(path, rows, force)


def write_cga_csv(path: Path, source: Path, cga: dict[str, object], force: bool) -> None:
    rows = []
    tables = cga.get("tables", [])
    if not isinstance(tables, list):
        raise ValueError("invalid CGA table list")
    for table in tables:
        if not isinstance(table, dict):
            continue
        entries = table.get("entries", [])
        if not isinstance(entries, list):
            continue
        for entry in entries:
            if not isinstance(entry, dict):
                continue
            rows.append({
                "source_file": relpath(source),
                "control_word_hex": cga["control_word_hex"],
                "control_low_nibble": cga["control_low_nibble"],
                "control_low_nibble_rgb": " ".join(str(value) for value in cga["control_low_nibble_rgbi"]),
                "table": table["table"],
                "index": entry["index"],
                "word_hex": entry["word_hex"],
                "bytes_hex": entry["bytes_hex"],
                "pixels": " ".join(str(pixel) for pixel in entry["pixels"]),
                "pixels_text": entry["pixels_text"],
            })
    write_csv(path, rows, force)


def write_csv(path: Path, rows: list[dict[str, object]], force: bool) -> None:
    if path.exists() and not force:
        raise FileExistsError(f"refusing to overwrite existing file: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = list(rows[0]) if rows else ["empty"]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def export_raw_chunks(reader: Reader, resource_dir: Path, source_stem: str, entries: list[dict[str, object]], force: bool) -> list[dict[str, object]]:
    raw_outputs = []
    raw_dir = resource_dir / "raw"
    for sequence, entry in enumerate(entries):
        tag_name = safe_name(str(entry["tag"])[:-1])
        payload = reader.slice(int(entry["payload_offset"]), int(entry["payload_size"]))
        path = raw_dir / (
            f"{source_stem}_chunk_{sequence:04d}_{tag_name}_"
            f"off_{int(entry['payload_offset']):06X}_len_{int(entry['payload_size'])}.bin"
        )
        write_bytes_once(path, payload, force)
        raw_outputs.append({
            "tag": entry["tag"],
            "payload_offset": entry["payload_offset"],
            "payload_size": entry["payload_size"],
            "path": relpath(path),
        })
    return raw_outputs


def export_one(source: Path, out_dir: Path, args: argparse.Namespace) -> dict[str, object]:
    reader = Reader(source)
    entries = parse_tagged(reader, 0, reader.size, prefix=safe_name(source.stem))
    if not entries:
        raise ValueError(f"{source}: no tagged PAL chunks found")

    pal_entries = [entry for entry in entries if entry["tag"] == "PAL:"]
    ega_entries = [entry for entry in entries if entry["tag"] == "EGA:"]
    cga_entries = [entry for entry in entries if entry["tag"] == "CGA:"]
    if not pal_entries:
        raise ValueError(f"{source}: no PAL: container chunk found")

    source_stem = safe_name(source.stem)
    resource_dir = out_dir / source_stem
    resource_dir.mkdir(parents=True, exist_ok=True)

    write_chunks_csv(resource_dir / "chunks.csv", entries, args.force)
    raw_outputs = export_raw_chunks(reader, resource_dir, source_stem, entries, args.force)

    decoded: dict[str, object] = {}
    swatch_outputs: list[dict[str, object]] = []
    cga_swatch_outputs: list[dict[str, object]] = []

    if ega_entries:
        ega_entry = ega_entries[0]
        ega_payload = reader.slice(int(ega_entry["payload_offset"]), int(ega_entry["payload_size"]))
        ega = decode_ega_payload(ega_payload)
        decoded["ega"] = ega
        write_ega_csv(resource_dir / "ega_banks.csv", source, ega, args.force)

        if not args.no_swatches:
            banks = ega["banks"]
            if not isinstance(banks, list):
                raise ValueError("invalid EGA bank list")
            for bank in banks:
                if not isinstance(bank, dict):
                    continue
                bank_index = int(bank["bank"])
                for interpretation in ("low", "high"):
                    palette = palette_from_bank(bank, interpretation)
                    pixels = swatch_pixels(palette, args.cell_width, args.cell_height)
                    base_path = resource_dir / "swatches" / f"{source_stem}_ega_bank{bank_index}_{interpretation}"
                    paths = write_image_outputs(base_path, pixels, args.image_format, args.force)
                    swatch_outputs.append({
                        "bank": bank_index,
                        "interpretation": interpretation,
                        "paths": paths,
                    })

    if cga_entries:
        cga_entry = cga_entries[0]
        cga_payload = reader.slice(int(cga_entry["payload_offset"]), int(cga_entry["payload_size"]))
        cga = decode_cga_payload(cga_payload)
        decoded["cga"] = cga
        write_cga_csv(resource_dir / "cga_tables.csv", source, cga, args.force)

        if not args.no_swatches:
            tables = cga["tables"]
            if not isinstance(tables, list):
                raise ValueError("invalid CGA table list")
            for table in tables:
                if not isinstance(table, dict):
                    continue
                table_index = int(table["table"])
                pixels = cga_table_pixels(
                    table,
                    CGA_INDEX_PREVIEW_PALETTE,
                    args.cga_pixel_scale,
                    args.cell_height,
                )
                base_path = resource_dir / "swatches" / f"{source_stem}_cga_table{table_index}_2bpp_indexes"
                paths = write_image_outputs(base_path, pixels, args.image_format, args.force)
                cga_swatch_outputs.append({
                    "table": table_index,
                    "palette": "cga_2bpp_index_preview",
                    "paths": paths,
                })

    manifest = {
        "tool": "Sorted Original Files/PAL/pal_extract/pal_extract.py",
        "schema_version": SCHEMA_VERSION,
        "source": {
            "path": relpath(source),
            "size": reader.size,
            "sha256": sha256_bytes(reader.data),
        },
        "container": "PAL: tagged resource",
        "format_notes": [
            "PAL files are tagged containers with a PAL: parent chunk.",
            "EGA: payload is decoded as four tentative 16-color banks from duplicated byte pairs.",
            "CGA: payload is decoded as one control word plus five 16-entry lookup tables of CGA 2bpp pixel patterns.",
        ],
        "chunk_count": len(entries),
        "chunks": {
            "pal_count": len(pal_entries),
            "ega_count": len(ega_entries),
            "cga_count": len(cga_entries),
        },
        "raw_outputs": raw_outputs,
        "decoded": decoded,
        "swatches": swatch_outputs,
        "cga_swatches": cga_swatch_outputs,
    }
    write_text_once(resource_dir / "manifest.json", json.dumps(manifest, indent=2) + "\n", args.force)
    return manifest


def iter_sources(input_path: Path) -> list[Path]:
    if input_path.is_dir():
        return sorted(path for path in input_path.iterdir() if path.is_file() and path.suffix.upper() == ".PAL")
    return [input_path]


def default_input_dir() -> Path:
    return Path(__file__).resolve().parent.parent


def default_out_dir() -> Path:
    return Path(__file__).resolve().parent / "out"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export MechWarrior tagged PAL resources to raw chunks, EGA tables, CGA diagnostics, and swatches."
    )
    parser.add_argument(
        "input",
        nargs="?",
        default=str(default_input_dir()),
        help="PAL file or directory with PAL files; default: parent PAL directory",
    )
    parser.add_argument("--out", default=str(default_out_dir()), help="output directory")
    parser.add_argument("--image-format", choices=["bmp", "ppm", "png", "all"], default="bmp")
    parser.add_argument("--cell-width", type=int, default=24, help="swatch cell width in pixels")
    parser.add_argument("--cell-height", type=int, default=24, help="swatch cell height in pixels")
    parser.add_argument("--cga-pixel-scale", type=int, default=3, help="horizontal scale for each decoded CGA 2bpp pixel")
    parser.add_argument("--no-swatches", action="store_true", help="skip visual EGA and CGA swatch images")
    parser.add_argument("--force", action="store_true", help="overwrite existing outputs")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    input_path = Path(args.input)
    out_dir = Path(args.out)
    if args.cell_width <= 0 or args.cell_height <= 0 or args.cga_pixel_scale <= 0:
        raise ValueError("swatch dimensions and CGA pixel scale must be positive")
    out_dir.mkdir(parents=True, exist_ok=True)

    manifests = []
    for source in iter_sources(input_path):
        manifests.append(export_one(source, out_dir, args))

    summary = {
        "tool": "Sorted Original Files/PAL/pal_extract/pal_extract.py",
        "schema_version": SCHEMA_VERSION,
        "input": relpath(input_path),
        "output": relpath(out_dir),
        "file_count": len(manifests),
        "files": [
            {
                "source": manifest["source"]["path"],
                "chunk_count": manifest["chunk_count"],
                "ega_count": manifest["chunks"]["ega_count"],
                "cga_count": manifest["chunks"]["cga_count"],
                "swatch_count": len(manifest["swatches"]),
                "cga_swatch_count": len(manifest["cga_swatches"]),
            }
            for manifest in manifests
        ],
    }
    write_text_once(out_dir / "summary.json", json.dumps(summary, indent=2) + "\n", args.force)

    print(f"Extracted {summary['file_count']} PAL file(s) to {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
