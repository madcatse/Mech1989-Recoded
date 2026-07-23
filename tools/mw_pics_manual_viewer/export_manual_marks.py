#!/usr/bin/env python3
"""Export images from JSON marks made by mw_pics_manual_viewer.html.

Usage:
  python export_manual_marks.py MW_PICS.BIN mw_pics_manual_marks.json --out manual_export

The JSON may be either the whole viewer export object {"marks": [...]} or a plain list of marks.
"""
from __future__ import annotations
import argparse, json, struct
from pathlib import Path
from typing import Iterable

EGA_PALETTE = [
    (0x00,0x00,0x00),(0x00,0x00,0xAA),(0x00,0xAA,0x00),(0x00,0xAA,0xAA),
    (0xAA,0x00,0x00),(0xAA,0x00,0xAA),(0xAA,0x55,0x00),(0xAA,0xAA,0xAA),
    (0x55,0x55,0x55),(0x55,0x55,0xFF),(0x55,0xFF,0x55),(0x55,0xFF,0xFF),
    (0xFF,0x55,0x55),(0xFF,0x55,0xFF),(0xFF,0xFF,0x55),(0xFF,0xFF,0xFF),
]
EGA_SOURCE_TO_GAME = [5,0,15,7,8,11,9,1,12,4,10,3,2,14,6,13]
GAME_PALETTE = [EGA_PALETTE[i] for i in EGA_SOURCE_TO_GAME]

def iter_nibbles_low_high(data: bytes) -> Iterable[int]:
    for value in data:
        yield value & 0x0F
        yield value >> 4

def pack_nibbles_high_low(nibbles: list[int]) -> bytes:
    out = bytearray()
    for i in range(0, len(nibbles)-1, 2):
        out.append((nibbles[i] << 4) | nibbles[i+1])
    return bytes(out)

def decompress_pics_bin_rle(data: bytes) -> bytes:
    decoded_nibbles: list[int] = []
    source = iter(iter_nibbles_low_high(data))
    for nibble in source:
        if nibble != 0x0F:
            decoded_nibbles.append(nibble)
            continue
        try:
            count_minus_one = next(source)
            value = next(source)
        except StopIteration:
            break
        decoded_nibbles.extend([value] * (count_minus_one + 1))
    return pack_nibbles_high_low(decoded_nibbles)

def render_4bpp(decoded: bytes, offset: int, width: int, height: int, has_header: bool, phase: int, palette):
    pixel_offset = offset + 4 if has_header else offset
    rows = []
    nib_index = phase
    for y in range(height):
        row = []
        for x in range(width):
            byte_off = pixel_offset + (nib_index // 2)
            b = decoded[byte_off] if 0 <= byte_off < len(decoded) else 0
            ci = (b & 0x0F) if (nib_index & 1) else ((b >> 4) & 0x0F)
            row.append(palette[ci])
            nib_index += 1
        if width & 1:
            # rows in the confirmed format are byte-aligned
            if (nib_index & 1) != phase:
                nib_index += 1
        else:
            # even widths naturally end on a byte boundary
            pass
    return rows

def write_bmp(path: Path, pixels):
    h = len(pixels); w = len(pixels[0]) if h else 0
    row_stride = ((w * 3 + 3) // 4) * 4
    pixel_data_size = row_stride * h
    file_size = 14 + 40 + pixel_data_size
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('wb') as f:
        f.write(b'BM')
        f.write(struct.pack('<IHHI', file_size, 0, 0, 54))
        f.write(struct.pack('<IiiHHIIiiII', 40, w, h, 1, 24, 0, pixel_data_size, 0, 0, 0, 0))
        pad = b'\0' * (row_stride - w*3)
        for row in reversed(pixels):
            for r,g,b in row:
                f.write(bytes((b,g,r)))
            f.write(pad)

def write_png_or_bmp(path: Path, pixels, image_format: str):
    if image_format == 'png':
        try:
            from PIL import Image
            h=len(pixels); w=len(pixels[0]) if h else 0
            im=Image.new('RGB',(w,h)); im.putdata([p for row in pixels for p in row])
            path.parent.mkdir(parents=True, exist_ok=True); im.save(path)
            return
        except ImportError:
            path = path.with_suffix('.bmp')
    write_bmp(path.with_suffix('.bmp'), pixels)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bin', type=Path)
    ap.add_argument('marks_json', type=Path)
    ap.add_argument('--out', type=Path, default=Path('manual_export'))
    ap.add_argument('--palette', choices=['game','ega'], default='game')
    ap.add_argument('--image-format', choices=['png','bmp'], default='png')
    args = ap.parse_args()
    decoded = decompress_pics_bin_rle(args.bin.read_bytes())
    obj = json.loads(args.marks_json.read_text(encoding='utf-8'))
    marks = obj.get('marks', obj) if isinstance(obj, dict) else obj
    pal = GAME_PALETTE if args.palette == 'game' else EGA_PALETTE
    manifest=[]
    for i,m in enumerate(marks):
        if m.get('kind') != 'image':
            continue
        off=int(m['offset']); w=int(m['width']); h=int(m['height'])
        has_header=bool(m.get('has_header', m.get('mode') == 'headered'))
        phase=int(m.get('phase',0))
        pixels=render_4bpp(decoded, off, w, h, has_header, phase, pal)
        mode='header' if has_header else 'raw'
        base=args.out / f'manual_{i:03d}_{off:08X}_{mode}_{w}x{h}'
        out_path=base.with_suffix('.png' if args.image_format == 'png' else '.bmp')
        write_png_or_bmp(out_path, pixels, args.image_format)
        manifest.append({**m, 'export_path': str(out_path).replace('\\','/')})
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out/'manual_export_manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'exported {len(manifest)} image marks to {args.out}')
if __name__ == '__main__':
    main()
