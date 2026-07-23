#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
from pathlib import Path


SCHEMA_VERSION = 1

KNOWN_TAGS = {"BMP:", "INF:", "BIN:", "PAL:", "EGA:", "CGA:"}

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

EGA_SOURCE_TO_GAME = [
    5, 0, 15, 7,
    8, 11, 9, 1,
    12, 4, 10, 3,
    2, 14, 6, 13,
]
EGA_RGB_REMAP = {EGA_PALETTE[src]: EGA_PALETTE[dst] for src, dst in enumerate(EGA_SOURCE_TO_GAME)}


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


class BitReader:
    def __init__(self, data: bytes):
        self.data = data
        self.bit_index = 0

    def read(self, width: int) -> int | None:
        if self.bit_index + width > len(self.data) * 8:
            return None
        value = 0
        for bit_index in range(width):
            byte = self.data[self.bit_index // 8]
            bit_offset = self.bit_index % 8
            value |= ((byte >> bit_offset) & 1) << bit_index
            self.bit_index += 1
        return value

    @property
    def consumed_bytes(self) -> int:
        return (self.bit_index + 7) // 8


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


def parse_tagged(reader: Reader, start: int, limit: int, depth: int = 0) -> list[dict[str, object]]:
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
            "tag": tag,
            "offset": offset,
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
            entries.extend(parse_tagged(reader, payload_offset, end_offset, depth + 1))

        offset = end_offset
        local_index += 1
    return entries


def decode_inf_dimensions(payload: bytes) -> tuple[list[int], list[int]]:
    if len(payload) < 6 or len(payload) % 2:
        raise ValueError("INF payload size must be an even value of at least 6 bytes")
    values = [int.from_bytes(payload[index:index + 2], "little") for index in range(0, len(payload), 2)]
    count = values[0]
    if count <= 0:
        raise ValueError(f"invalid INF record count: {count}")
    if len(payload) != 2 + count * 4:
        raise ValueError(f"INF size mismatch for {count} records: {len(payload)} bytes")
    widths = values[1:1 + count]
    heights = values[1 + count:1 + count * 2]
    if any(width <= 0 or width > 640 for width in widths):
        raise ValueError(f"invalid INF widths: {widths}")
    if any(height <= 0 or height > 400 for height in heights):
        raise ValueError(f"invalid INF heights: {heights}")
    return widths, heights


def decompress_codec1(body: bytes, expected_size: int | None = None) -> bytes:
    output = bytearray()
    index = 0
    while index < len(body):
        command = body[index]
        index += 1
        if command & 0x80:
            count = command & 0x7F
            if index >= len(body):
                raise ValueError("codec 1 run command is missing value byte")
            output.extend([body[index]] * count)
            index += 1
        else:
            count = command
            if index + count > len(body):
                raise ValueError("codec 1 literal command exceeds input")
            output.extend(body[index:index + count])
            index += count
    if expected_size is not None and len(output) != expected_size:
        raise ValueError(f"codec 1 decoded size mismatch: expected {expected_size}, got {len(output)}")
    return bytes(output)


def decompress_codec2(body: bytes, expected_size: int | None = None) -> bytes:
    reader = BitReader(body)
    width = 9
    max_width = 12
    next_code = 257
    dictionary: dict[int, bytes] = {index: bytes([index]) for index in range(256)}
    previous: bytes | None = None
    output = bytearray()

    while True:
        code = reader.read(width)
        if code is None:
            break
        if code in dictionary:
            current = dictionary[code]
        elif previous is not None and code == next_code:
            current = previous + previous[:1]
        else:
            raise ValueError(f"codec 2 bad LZW code {code} at byte {reader.consumed_bytes}")

        output.extend(current)
        if previous is not None:
            dictionary[next_code] = previous + current[:1]
            next_code += 1
            if next_code >= (1 << width) and width < max_width:
                width += 1
        previous = current

    if reader.consumed_bytes != len(body):
        raise ValueError(f"codec 2 did not consume whole body: {reader.consumed_bytes}/{len(body)}")
    if expected_size is not None and len(output) != expected_size:
        raise ValueError(f"codec 2 decoded size mismatch: expected {expected_size}, got {len(output)}")
    return bytes(output)


def decode_bin_payload(payload: bytes) -> tuple[int, int, bytes]:
    if len(payload) < 5:
        raise ValueError("BIN payload is too small for codec header")
    codec = payload[0]
    decoded_size = int.from_bytes(payload[1:5], "little")
    body = payload[5:]
    if codec == 1:
        return codec, decoded_size, decompress_codec1(body, decoded_size)
    if codec == 2:
        return codec, decoded_size, decompress_codec2(body, decoded_size)
    raise NotImplementedError(f"unknown BIN codec: {codec}")


def split_packed_4bpp_records(decoded: bytes, widths: list[int], heights: list[int]) -> list[dict[str, object]]:
    records: list[dict[str, object]] = []
    offset = 0
    for index, (width, height) in enumerate(zip(widths, heights)):
        row_stride = (width + 1) // 2
        record_size = row_stride * height
        if offset + record_size > len(decoded):
            raise ValueError("decoded packed 4bpp buffer is shorter than INF dimensions require")
        records.append({
            "index": index,
            "width": width,
            "height": height,
            "row_stride_bytes": row_stride,
            "decoded_offset": offset,
            "decoded_size": record_size,
            "pixels": decoded[offset:offset + record_size],
        })
        offset += record_size
    if offset != len(decoded):
        raise ValueError(f"decoded packed 4bpp buffer has {len(decoded) - offset} trailing bytes")
    return records


def decode_pal_ega_payload(payload: bytes) -> dict[str, object]:
    if len(payload) != 128:
        raise ValueError(f"PAL:EGA payload must be 128 bytes, got {len(payload)}")
    pairs = [payload[index:index + 2] for index in range(0, len(payload), 2)]
    banks = []
    for bank_index in range(4):
        colors = []
        for color_index, pair in enumerate(pairs[bank_index * 16:(bank_index + 1) * 16]):
            raw = pair[0]
            colors.append({
                "index": color_index,
                "raw": raw,
                "low_rgb": EGA_PALETTE[raw & 0x0F],
                "high_rgb": EGA_PALETTE[(raw >> 4) & 0x0F],
            })
        banks.append({"bank": bank_index, "colors": colors})
    return {"layout": "ega_pair_banks_tentative", "banks": banks}


def load_pal_ega_palette(path: Path, bank: int, interpretation: str) -> list[tuple[int, int, int]]:
    reader = Reader(path)
    entries = parse_tagged(reader, 0, reader.size)
    ega_entries = [entry for entry in entries if entry["tag"] == "EGA:"]
    if not ega_entries:
        raise ValueError(f"no EGA: chunk found in {path}")
    entry = ega_entries[0]
    payload = reader.slice(int(entry["payload_offset"]), int(entry["payload_size"]))
    decoded = decode_pal_ega_payload(payload)
    banks = decoded["banks"]
    if not isinstance(banks, list) or bank < 0 or bank >= len(banks):
        raise ValueError(f"palette bank out of range: {bank}")
    bank_data = banks[bank]
    if not isinstance(bank_data, dict):
        raise ValueError(f"invalid palette bank: {bank}")
    colors = bank_data["colors"]
    key = "high_rgb" if interpretation == "high" else "low_rgb"
    return [tuple(color[key]) for color in colors]  # type: ignore[index]


def render_4bpp_to_rgb(width: int, height: int, data: bytes, palette: list[tuple[int, int, int]]) -> list[list[tuple[int, int, int]]]:
    rows: list[list[tuple[int, int, int]]] = []
    pixel_index = 0
    for _y in range(height):
        row = []
        for _x in range(width):
            byte = data[pixel_index // 2]
            color_index = (byte >> 4) & 0x0F if pixel_index % 2 == 0 else byte & 0x0F
            row.append(palette[color_index])
            pixel_index += 1
        rows.append(row)
    return rows


def remap_ega_source_to_game(pixels: list[list[tuple[int, int, int]]]) -> list[list[tuple[int, int, int]]]:
    return [[EGA_RGB_REMAP.get(pixel, pixel) for pixel in row] for row in pixels]


def scale_pixels_nearest(
    pixels: list[list[tuple[int, int, int]]],
    xscale: int,
    yscale: int,
) -> list[list[tuple[int, int, int]]]:
    if xscale <= 0 or yscale <= 0:
        raise ValueError("scale factors must be positive")
    scaled: list[list[tuple[int, int, int]]] = []
    for row in pixels:
        expanded_row: list[tuple[int, int, int]] = []
        for pixel in row:
            expanded_row.extend([pixel] * xscale)
        for _ in range(yscale):
            scaled.append(list(expanded_row))
    return scaled


def write_ppm(path: Path, pixels: list[list[tuple[int, int, int]]]) -> None:
    height = len(pixels)
    width = len(pixels[0]) if height else 0
    with path.open("w", encoding="ascii", newline="\n") as handle:
        handle.write(f"P3\n{width} {height}\n255\n")
        for row in pixels:
            handle.write(" ".join(f"{r} {g} {b}" for r, g, b in row))
            handle.write("\n")


def write_bitmap_image(path: Path, pixels: list[list[tuple[int, int, int]]], image_format: str) -> None:
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError("PNG/BMP output requires Pillow; PPM works without extra packages") from exc

    height = len(pixels)
    width = len(pixels[0]) if height else 0
    image = Image.new("RGB", (width, height))
    image.putdata([pixel for row in pixels for pixel in row])
    image.save(path, format=image_format.upper())


def write_image_outputs(
    base_path: Path,
    pixels: list[list[tuple[int, int, int]]],
    image_format: str,
    force: bool,
) -> list[str]:
    formats = ["ppm", "png", "bmp"] if image_format == "all" else [image_format]
    written: list[str] = []
    for current_format in formats:
        out_path = base_path.parent / f"{base_path.name}.{current_format}"
        if out_path.exists() and not force:
            raise FileExistsError(f"refusing to overwrite existing file: {out_path}")
        if current_format == "ppm":
            write_ppm(out_path, pixels)
        else:
            write_bitmap_image(out_path, pixels, current_format)
        written.append(str(out_path).replace("\\", "/"))
    return written


def export_one(source: Path, out_dir: Path, args: argparse.Namespace) -> dict[str, object]:
    reader = Reader(source)
    entries = parse_tagged(reader, 0, reader.size)
    inf_entries = [entry for entry in entries if entry["tag"] == "INF:"]
    bin_entries = [entry for entry in entries if entry["tag"] == "BIN:"]
    if not inf_entries:
        raise ValueError(f"no INF: chunk found in {source}")
    if not bin_entries:
        raise ValueError(f"no BIN: chunk found in {source}")
    if args.index < 0 or args.index >= len(bin_entries):
        raise ValueError(f"BIN index out of range for {source.name}: {args.index}; available 0..{len(bin_entries) - 1}")

    resource_dir = out_dir / safe_name(source.stem)
    resource_dir.mkdir(parents=True, exist_ok=True)

    chunk_rows = []
    for entry in entries:
        chunk_rows.append({
            "tag": entry["tag"],
            "offset": entry["offset"],
            "payload_offset": entry["payload_offset"],
            "payload_size": entry["payload_size"],
            "end_offset": entry["end_offset"],
            "depth": entry["depth"],
            "local_index": entry["local_index"],
            "has_nested_flag": "yes" if entry["has_nested_flag"] else "no",
            "sha256": entry["sha256"],
        })
    with (resource_dir / "chunks.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(chunk_rows[0]))
        writer.writeheader()
        writer.writerows(chunk_rows)

    inf_entry = inf_entries[0]
    inf_payload = reader.slice(int(inf_entry["payload_offset"]), int(inf_entry["payload_size"]))
    widths, heights = decode_inf_dimensions(inf_payload)
    bin_entry = bin_entries[args.index]
    bin_payload = reader.slice(int(bin_entry["payload_offset"]), int(bin_entry["payload_size"]))
    codec, declared_decoded_size, decoded = decode_bin_payload(bin_payload)
    records = split_packed_4bpp_records(decoded, widths, heights)

    if args.extract_raw:
        (resource_dir / f"{safe_name(source.stem)}.INF.bin").write_bytes(inf_payload)
        (resource_dir / f"{safe_name(source.stem)}.BIN.payload.bin").write_bytes(bin_payload)
        (resource_dir / f"{safe_name(source.stem)}.BIN.decoded.bin").write_bytes(decoded)
        raw_records_dir = resource_dir / "records_raw"
        raw_records_dir.mkdir(exist_ok=True)
        for record in records:
            raw_path = raw_records_dir / (
                f"{safe_name(source.stem)}_{int(record['index']):03d}_"
                f"{int(record['width'])}x{int(record['height'])}.4bpp"
            )
            raw_path.write_bytes(record["pixels"])  # type: ignore[arg-type]

    palette = args.palette_rgb
    palette_metadata = args.palette_metadata

    rendered = []
    for record in records:
        pixels = render_4bpp_to_rgb(
            int(record["width"]),
            int(record["height"]),
            record["pixels"],  # type: ignore[arg-type]
            palette,
        )
        if args.game_palette_remap:
            pixels = remap_ega_source_to_game(pixels)
        if args.display_scale:
            pixels = scale_pixels_nearest(pixels, args.xscale, args.yscale)

        base_path = resource_dir / (
            f"{safe_name(source.stem)}_{int(record['index']):03d}_"
            f"{int(record['width'])}x{int(record['height'])}"
        )
        paths = write_image_outputs(base_path, pixels, args.image_format, args.force)
        rendered.append({
            "index": record["index"],
            "width": record["width"],
            "height": record["height"],
            "row_stride_bytes": record["row_stride_bytes"],
            "decoded_offset": record["decoded_offset"],
            "decoded_size": record["decoded_size"],
            "paths": paths,
        })

    manifest = {
        "tool": "BMP/bmp_extract/bmp_extract.py",
        "schema_version": SCHEMA_VERSION,
        "source": str(source).replace("\\", "/"),
        "source_size": reader.size,
        "source_sha256": sha256_bytes(reader.data),
        "container": "BMP: tagged resource",
        "inf_layout": "u16le count; u16le widths[count]; u16le heights[count]",
        "bin_layout": "u8 codec; u32le decoded_size; encoded_body",
        "codec": codec,
        "declared_decoded_size": declared_decoded_size,
        "decoded_layout": "packed_4bpp_high_low_nibbles",
        "decoded_size": len(decoded),
        "palette": palette_metadata,
        "extract_raw": bool(args.extract_raw),
        "entries": rendered,
    }
    with (resource_dir / "manifest.json").open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")
    return manifest


def iter_sources(input_path: Path) -> list[Path]:
    if input_path.is_dir():
        return sorted(path for path in input_path.iterdir() if path.suffix.upper() == ".BMP")
    return [input_path]


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Extract MechWarrior tagged BMP resources to raw chunks and rendered images."
    )
    parser.add_argument(
        "input",
        nargs="?",
        default=str(Path("Sorted Original Files") / "BMP"),
        help="BMP file or directory with BMP files; default: Sorted Original Files/BMP",
    )
    parser.add_argument("--out", default=str(Path("BMP") / "bmp_extract" / "out"), help="output directory")
    parser.add_argument("--index", type=int, default=0, help="BIN chunk index inside each BMP resource")
    parser.add_argument("--extract-raw", action="store_true", help="also write INF/BIN payloads and decoded 4bpp records")
    parser.add_argument("--image-format", choices=["ppm", "png", "bmp", "all"], default="png")
    parser.add_argument("--palette", help="optional tagged PAL file with EGA banks")
    parser.add_argument("--palette-bank", type=int, default=0)
    parser.add_argument("--palette-interpretation", choices=["low", "high"], default="low")
    parser.add_argument("--game-palette-remap", action="store_true", help="apply the remap used by the older preview pipeline")
    parser.add_argument("--display-scale", action="store_true", help="scale images for easier viewing")
    parser.add_argument("--xscale", type=int, default=4)
    parser.add_argument("--yscale", type=int, default=4)
    parser.add_argument("--force", action="store_true", help="overwrite existing outputs")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    input_path = Path(args.input)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.palette:
        palette_path = Path(args.palette)
        args.palette_rgb = load_pal_ega_palette(palette_path, args.palette_bank, args.palette_interpretation)
        args.palette_metadata = {
            "kind": "pal_ega_pair_banks_tentative",
            "path": str(palette_path).replace("\\", "/"),
            "bank": args.palette_bank,
            "interpretation": args.palette_interpretation,
        }
    else:
        args.palette_rgb = EGA_PALETTE
        args.palette_metadata = {"kind": "ega_default"}

    manifests = []
    for source in iter_sources(input_path):
        manifests.append(export_one(source, out_dir, args))

    summary = {
        "tool": "BMP/bmp_extract/bmp_extract.py",
        "schema_version": SCHEMA_VERSION,
        "input": str(input_path).replace("\\", "/"),
        "output": str(out_dir).replace("\\", "/"),
        "file_count": len(manifests),
        "image_count": sum(len(manifest["entries"]) for manifest in manifests),
        "files": [
            {
                "source": manifest["source"],
                "codec": manifest["codec"],
                "entry_count": len(manifest["entries"]),
                "decoded_size": manifest["decoded_size"],
            }
            for manifest in manifests
        ],
    }
    with (out_dir / "summary.json").open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(summary, handle, indent=2)
        handle.write("\n")

    print(f"Extracted {summary['file_count']} BMP file(s), rendered {summary['image_count']} image(s) to {out_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
