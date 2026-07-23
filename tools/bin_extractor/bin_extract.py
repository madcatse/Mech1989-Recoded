#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import struct
import sys
from pathlib import Path
from typing import Iterable


SCHEMA_VERSION = 2

TOOL_DIR = Path(__file__).resolve().parent
DEFAULT_INPUT = TOOL_DIR
DEFAULT_OUT = TOOL_DIR / "out"

KNOWN_TAGS = {"PAL:", "EGA:", "CGA:"}

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

CGA_RGBI_PALETTE = EGA_PALETTE
CGA_VIDEO_PALETTES = {
    "palette0-low": [None, CGA_RGBI_PALETTE[2], CGA_RGBI_PALETTE[4], CGA_RGBI_PALETTE[6]],
    "palette0-high": [None, CGA_RGBI_PALETTE[10], CGA_RGBI_PALETTE[12], CGA_RGBI_PALETTE[14]],
    "palette1-low": [None, CGA_RGBI_PALETTE[3], CGA_RGBI_PALETTE[5], CGA_RGBI_PALETTE[7]],
    "palette1-high": [None, CGA_RGBI_PALETTE[11], CGA_RGBI_PALETTE[13], CGA_RGBI_PALETTE[15]],
    "index-preview": [CGA_RGBI_PALETTE[0], CGA_RGBI_PALETTE[3], CGA_RGBI_PALETTE[5], CGA_RGBI_PALETTE[15]],
}

EGA_SOURCE_TO_GAME = [
    5, 0, 15, 7,
    8, 11, 9, 1,
    12, 4, 10, 3,
    2, 14, 6, 13,
]
EGA_RGB_REMAP = {EGA_PALETTE[src]: EGA_PALETTE[dst] for src, dst in enumerate(EGA_SOURCE_TO_GAME)}

# Optional legacy raw offsets. Use this for ad-hoc/manual extraction only.
# MW_PICS.BIN is handled below by explicit record descriptors because some
# records are intentionally unheaded and cannot be parsed as u16 width/height.
KNOWN_RAW_OFFSETS: dict[str, list[int]] = {}

# Forensic map for MechWarrior (1989) MW_PICS.BIN. Offsets are in the
# nibble-RLE decoded stream, not in the compressed source file.
#
# Headered records have:
#   u16le width, u16le height, packed 4bpp pixels, high nibble first
# Raw records have no width/height header; the bytes at `offset` are already
# packed 4bpp pixel data. These were verified by visual correlation and by the
# way adjacent images line up in the decoded stream.
MW_PICS_RECORDS: dict[str, list[dict[str, object]]] = {
    "MW_PICS": [
        {"offset": 0x00000280, "width": 320, "height": 200, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": True, "category": "scene", "description": "spaceport/moon fullscreen scene"},
        {"offset": 0x00007F89, "width": 320, "height": 200, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": True, "category": "scene", "description": "city and green hills fullscreen scene"},
        {"offset": 0x0000FC92, "width": 320, "height": 200, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": True, "category": "scene", "description": "arctic/base fullscreen scene"},
        {"offset": 0x0001799B, "width": 320, "height": 200, "has_header": False, "header_size": 0, "nibble_phase": 0, "validate_header": False, "category": "scene", "description": "laboratory fullscreen scene A, raw/unheaded"},
        {"offset": 0x0001F6A5, "width": 320, "height": 200, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": True, "category": "scene", "description": "laboratory fullscreen scene B"},
        {"offset": 0x000273AE, "width": 161, "height": 70, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "image", "description": "manual mark 000 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 0},
        {"offset": 0x000289DE, "width": 161, "height": 70, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "image", "description": "manual mark 001 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 1},
        {"offset": 0x0002A00D, "width": 111, "height": 182, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "image", "description": "manual mark 002 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 2},
        {"offset": 0x0002C7E6, "width": 125, "height": 165, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "image", "description": "manual mark 003 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 3},
        {"offset": 0x0002F08A, "width": 115, "height": 183, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "image", "description": "manual mark 004 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 4},
        {"offset": 0x00031A09, "width": 119, "height": 182, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "image", "description": "manual mark 005 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 5},
        {"offset": 0x000344BB, "width": 135, "height": 187, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "image", "description": "manual mark 006 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 6},
        {"offset": 0x0003766F, "width": 113, "height": 178, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "image", "description": "manual mark 007 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 7},
        {"offset": 0x00039E1A, "width": 121, "height": 161, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "image", "description": "manual mark 008 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 8},
        {"offset": 0x0003C480, "width": 125, "height": 181, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "image", "description": "manual mark 009 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 9},
        {"offset": 0x0003F115, "width": 68, "height": 68, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "mech", "description": "manual mark 010 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 10},
        {"offset": 0x0003FA26, "width": 68, "height": 68, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "mech", "description": "manual mark 011 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 11},
        {"offset": 0x00040338, "width": 68, "height": 68, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "mech", "description": "manual mark 012 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 12},
        {"offset": 0x00040C49, "width": 68, "height": 68, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "mech", "description": "manual mark 013 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 13},
        {"offset": 0x0004155B, "width": 68, "height": 68, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "mech", "description": "manual mark 014 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 14},
        {"offset": 0x00041E6C, "width": 68, "height": 68, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "mech", "description": "manual mark 015 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 15},
        {"offset": 0x0004277E, "width": 68, "height": 68, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "mech", "description": "manual mark 016 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 16},
        {"offset": 0x0004308F, "width": 68, "height": 68, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "mech", "description": "manual mark 017 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 17},
        {"offset": 0x000439A0, "width": 129, "height": 36, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "banner", "description": "manual mark 018 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 18},
        {"offset": 0x000442CD, "width": 130, "height": 36, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "banner", "description": "manual mark 019 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 19},
        {"offset": 0x00044BFB, "width": 129, "height": 36, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "banner", "description": "manual mark 020 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 20},
        {"offset": 0x00045528, "width": 129, "height": 36, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "banner", "description": "manual mark 021 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 21},
        {"offset": 0x00045E56, "width": 129, "height": 36, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "banner", "description": "manual mark 022 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 22},
        {"offset": 0x00046783, "width": 60, "height": 55, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "emblem", "description": "manual mark 023 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 23},
        {"offset": 0x00046DFE, "width": 60, "height": 55, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "emblem", "description": "manual mark 024 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 24},
        {"offset": 0x00047479, "width": 60, "height": 55, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "emblem", "description": "manual mark 025 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 25},
        {"offset": 0x00047AF5, "width": 60, "height": 55, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "emblem", "description": "manual mark 026 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 26},
        {"offset": 0x00048170, "width": 60, "height": 55, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "emblem", "description": "manual mark 027 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 27},
        {"offset": 0x000487EB, "width": 55, "height": 49, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "emblem", "description": "manual mark 028 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 28},
        {"offset": 0x00048D50, "width": 55, "height": 49, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "emblem", "description": "manual mark 029 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 29},
        {"offset": 0x000492B6, "width": 55, "height": 49, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "emblem", "description": "manual mark 030 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 30},
        {"offset": 0x0004981B, "width": 55, "height": 49, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "emblem", "description": "manual mark 031 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 31},
        {"offset": 0x00049D80, "width": 55, "height": 49, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "emblem", "description": "manual mark 032 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 32},
        {"offset": 0x0004A2E5, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 033 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 33},
        {"offset": 0x0004B536, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 034 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 34},
        {"offset": 0x0004C787, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 035 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 35},
        {"offset": 0x0004D9D8, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "portrait", "description": "manual mark 036 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 36},
        {"offset": 0x0004EC29, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "portrait", "description": "manual mark 037 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 37},
        {"offset": 0x0004FE7B, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 038 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 38},
        {"offset": 0x000510CC, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 039 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 39},
        {"offset": 0x0005231D, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "portrait", "description": "manual mark 040 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 40},
        {"offset": 0x0005356F, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 041 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 41},
        {"offset": 0x000547C0, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 042 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 42},
        {"offset": 0x00055A11, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 043 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 43},
        {"offset": 0x00056C62, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "portrait", "description": "manual mark 044 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 44},
        {"offset": 0x00057EB3, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "portrait", "description": "manual mark 045 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 45},
        {"offset": 0x00059104, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "portrait", "description": "manual mark 046 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 46},
        {"offset": 0x0005A361, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "portrait", "description": "manual mark 047 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 47},
        {"offset": 0x0005B5B2, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "portrait", "description": "manual mark 048 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 48},
        {"offset": 0x0005C804, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 049 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 49},
        {"offset": 0x0005DA55, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 050 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 50},
        {"offset": 0x0005ECA6, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 051 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 51},
        {"offset": 0x0005FEF7, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 052 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 52},
        {"offset": 0x00061148, "width": 91, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "portrait", "description": "manual mark 053 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 53},
        {"offset": 0x00062401, "width": 90, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "portrait", "description": "manual mark 054 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 54},
        {"offset": 0x00063652, "width": 91, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "portrait", "description": "manual mark 055 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 55},
        {"offset": 0x000648DD, "width": 91, "height": 104, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "portrait", "description": "manual mark 056 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 56},
        {"offset": 0x00065B96, "width": 149, "height": 127, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "panel", "description": "manual mark 057 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 57},
        {"offset": 0x000680D5, "width": 320, "height": 200, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "scene", "description": "manual mark 058 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 58},
        {"offset": 0x0006FDF4, "width": 149, "height": 127, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "panel", "description": "manual mark 059 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 59},
        {"offset": 0x00072332, "width": 320, "height": 134, "has_header": True, "header_size": 4, "nibble_phase": 1, "validate_header": False, "category": "scene", "description": "manual mark 060 from mw_pics_manual_marks_saved.json; headered, phase 1", "manual_mark_index": 60},
        {"offset": 0x000776FC, "width": 148, "height": 127, "has_header": True, "header_size": 4, "nibble_phase": 0, "validate_header": False, "category": "panel", "description": "manual mark 061 from mw_pics_manual_marks_saved.json; headered, phase 0", "manual_mark_index": 61},
    ],
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def safe_name(value: str) -> str:
    keep = []
    for char in value:
        keep.append(char if char.isalnum() or char in "._-" else "_")
    return "".join(keep).strip("._") or "unnamed"


def is_tag(data: bytes) -> bool:
    if len(data) != 4:
        return False
    try:
        return data.decode("ascii") in KNOWN_TAGS
    except UnicodeDecodeError:
        return False


def parse_tagged_chunks(data: bytes, start: int = 0, limit: int | None = None, depth: int = 0) -> list[dict[str, object]]:
    if limit is None:
        limit = len(data)
    entries: list[dict[str, object]] = []
    offset = start
    while offset + 8 <= limit:
        tag_bytes = data[offset:offset + 4]
        if not is_tag(tag_bytes):
            break
        tag = tag_bytes.decode("ascii")
        raw_length = int.from_bytes(data[offset + 4:offset + 8], "little")
        payload_size = raw_length & 0x7FFFFFFF
        nested = (raw_length & 0x80000000) != 0
        payload_offset = offset + 8
        end_offset = payload_offset + payload_size
        if end_offset > limit:
            raise ValueError(f"{tag} payload ends at 0x{end_offset:X}, outside limit 0x{limit:X}")
        entry = {
            "tag": tag,
            "offset": offset,
            "payload_offset": payload_offset,
            "payload_size": payload_size,
            "end_offset": end_offset,
            "depth": depth,
            "has_nested_flag": nested,
        }
        entries.append(entry)
        if nested and payload_offset + 8 <= end_offset and is_tag(data[payload_offset:payload_offset + 4]):
            entries.extend(parse_tagged_chunks(data, payload_offset, end_offset, depth + 1))
        offset = end_offset
    return entries


def iter_nibbles_low_high(data: bytes) -> Iterable[int]:
    for value in data:
        yield value & 0x0F
        yield value >> 4


def pack_nibbles_high_low(nibbles: list[int]) -> bytes:
    output = bytearray()
    for index in range(0, len(nibbles) - 1, 2):
        output.append((nibbles[index] << 4) | nibbles[index + 1])
    return bytes(output)


def decompress_pics_bin_rle(data: bytes) -> tuple[bytes, int]:
    """Decode the nibble-RLE post-compressor used by MW_*PICS.BIN files."""
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
    return pack_nibbles_high_low(decoded_nibbles), len(decoded_nibbles)


def read_pics_bin_offset(decoded: bytes, offset: int) -> int:
    high = int.from_bytes(decoded[offset:offset + 2], "little")
    low = int.from_bytes(decoded[offset + 2:offset + 4], "little")
    return (high << 16) | low


def parse_pics_bin_header(decoded: bytes) -> tuple[dict[str, int | str], list[int], list[int]]:
    if len(decoded) < 8:
        return {}, [], []
    first_offset = read_pics_bin_offset(decoded, 0)
    if first_offset <= 0 or first_offset > len(decoded) or first_offset % 4 != 0:
        return {}, [], []
    if first_offset % 6 != 0:
        return {}, [], []
    entry_count = first_offset // 6
    if entry_count > 512:
        return {}, [], []

    offsets = [read_pics_bin_offset(decoded, index * 4) for index in range(entry_count)]
    sizes_offset = entry_count * 4
    sizes = [
        int.from_bytes(decoded[sizes_offset + index * 2:sizes_offset + index * 2 + 2], "little")
        for index in range(entry_count)
    ]

    valid_offsets = [value for value in offsets if 0 <= value <= len(decoded)]
    if len(valid_offsets) < max(2, entry_count // 2):
        return {}, [], []
    nonzero_offsets = [value for value in offsets if value != 0]
    return {
        "header_layout": "offset_table_then_u16le_size_table",
        "header_size": first_offset,
        "header_entry_count": entry_count,
        "nonzero_offset_count": len(nonzero_offsets),
        "nonzero_size_count": sum(1 for value in sizes if value != 0),
        "monotonic_nonzero_offsets": "yes" if nonzero_offsets == sorted(nonzero_offsets) else "no",
        "first_offsets_hex": " ".join(f"0x{value:08X}" for value in offsets[:12]),
        "first_sizes": " ".join(str(value) for value in sizes[:12]),
    }, offsets, sizes


def dac6_to_rgb(value: int) -> int:
    return round(max(0, min(63, value)) * 255 / 63)


def decode_pics_palette(payload: bytes) -> list[tuple[int, int, int]]:
    if len(payload) < 48:
        raise ValueError(f"raw PICS palette must be at least 48 bytes, got {len(payload)}")
    colors = []
    for index in range(0, min(len(payload), 48), 3):
        colors.append(tuple(dac6_to_rgb(value) for value in payload[index:index + 3]))
    while len(colors) < 16:
        colors.append(EGA_PALETTE[len(colors)])
    return colors[:16]


def load_pal_payloads(path: Path) -> dict[str, bytes]:
    data = path.read_bytes()
    entries = parse_tagged_chunks(data)
    payloads: dict[str, bytes] = {}
    for entry in entries:
        tag = str(entry["tag"])
        if tag not in payloads:
            start = int(entry["payload_offset"])
            size = int(entry["payload_size"])
            payloads[tag] = data[start:start + size]
    if "PAL:" not in payloads:
        raise ValueError(f"{path}: no PAL: container found")
    return payloads


def decode_pal_ega_palette(payload: bytes, bank: int, interpretation: str) -> list[tuple[int, int, int]]:
    if len(payload) != 128:
        raise ValueError(f"PAL:EGA payload must be 128 bytes, got {len(payload)}")
    if bank < 0 or bank > 3:
        raise ValueError(f"EGA palette bank out of range: {bank}")
    if interpretation not in {"low", "high"}:
        raise ValueError(f"unknown EGA palette interpretation: {interpretation}")

    colors = []
    bank_offset = bank * 32
    for color_index in range(16):
        raw = payload[bank_offset + color_index * 2]
        palette_index = (raw >> 4) & 0x0F if interpretation == "high" else raw & 0x0F
        colors.append(EGA_PALETTE[palette_index])
    return colors


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


def decode_pal_cga_table(payload: bytes, table_index: int) -> tuple[int, list[list[int]]]:
    if len(payload) != 162:
        raise ValueError(f"PAL:CGA payload must be 162 bytes, got {len(payload)}")
    if table_index < 0 or table_index > 4:
        raise ValueError(f"CGA table out of range: {table_index}")
    words = [int.from_bytes(payload[index:index + 2], "little") for index in range(0, len(payload), 2)]
    control_word = words[0]
    table_words = words[1 + table_index * 16:1 + (table_index + 1) * 16]
    return control_word, [cga_word_to_pixels(word) for word in table_words]


def build_cga_video_palette(control_word: int, palette_name: str) -> list[tuple[int, int, int]]:
    template = CGA_VIDEO_PALETTES[palette_name]
    if palette_name == "index-preview":
        return [color for color in template if color is not None]
    background = CGA_RGBI_PALETTE[control_word & 0x0F]
    return [background if color is None else color for color in template]


def resolve_render_profile(
    args: argparse.Namespace,
    decoded: bytes,
    entries: list[dict[str, object]],
) -> dict[str, object]:
    video_mode = args.video_mode
    if video_mode == "auto":
        if args.raw_palette:
            video_mode = "raw"
        elif args.palette:
            video_mode = "ega"
        else:
            video_mode = "bin"

    if video_mode == "raw":
        if not args.raw_palette:
            raise ValueError("--video-mode raw requires --raw-palette")
        palette_path = Path(args.raw_palette)
        palette = decode_pics_palette(palette_path.read_bytes())
        return {
            "mode": "raw",
            "palette": palette,
            "metadata": {
                "kind": "raw_pics_palette_rgb_16_dac6",
                "path": str(palette_path).replace("\\", "/"),
            },
        }

    if video_mode == "bin":
        palette = EGA_PALETTE
        metadata: dict[str, object] = {"kind": "ega_default_fallback"}
        for entry in entries:
            fields = entry.get("fields", {})
            if entry["status"] == "active" and isinstance(fields, dict) and fields.get("role") == "palette_rgb_16_dac6":
                offset = int(entry["offset"])
                size = int(entry["size"])
                palette = decode_pics_palette(decoded[offset:offset + size])
                metadata = {
                    "kind": "first_active_bin_palette_rgb_16_dac6",
                    "entry_index": int(entry["index"]),
                }
                break
        return {
            "mode": "bin",
            "palette": palette,
            "metadata": metadata,
        }

    if not args.palette:
        raise ValueError(f"--video-mode {video_mode} requires --palette")

    palette_path = Path(args.palette)
    payloads = load_pal_payloads(palette_path)
    if video_mode == "ega":
        if "EGA:" not in payloads:
            raise ValueError(f"{palette_path}: no EGA: chunk found")
        palette = decode_pal_ega_palette(payloads["EGA:"], args.palette_bank, args.palette_interpretation)
        return {
            "mode": "ega",
            "palette": palette,
            "metadata": {
                "kind": "pal_ega_pair_banks_tentative",
                "path": str(palette_path).replace("\\", "/"),
                "bank": args.palette_bank,
                "interpretation": args.palette_interpretation,
            },
        }

    if video_mode == "cga":
        if "CGA:" not in payloads:
            raise ValueError(f"{palette_path}: no CGA: chunk found")
        control_word, patterns = decode_pal_cga_table(payloads["CGA:"], args.cga_table)
        cga_palette = build_cga_video_palette(control_word, args.cga_palette)
        return {
            "mode": "cga",
            "cga_patterns": patterns,
            "cga_palette": cga_palette,
            "metadata": {
                "kind": "pal_cga_lookup_table",
                "path": str(palette_path).replace("\\", "/"),
                "table": args.cga_table,
                "cga_palette": args.cga_palette,
                "control_word_hex": f"0x{control_word:04X}",
                "control_low_nibble": control_word & 0x0F,
                "source_pixel_expansion": "one 4bpp source pixel becomes an 8-pixel CGA 2bpp pattern",
            },
        }

    raise ValueError(f"unknown video mode: {video_mode}")


def first_embedded_raw_palette(decoded: bytes, entries: list[dict[str, object]]) -> tuple[list[tuple[int, int, int]], int] | None:
    for entry in entries:
        fields = entry.get("fields", {})
        if entry["status"] == "active" and isinstance(fields, dict) and fields.get("role") == "palette_rgb_16_dac6":
            offset = int(entry["offset"])
            size = int(entry["size"])
            return decode_pics_palette(decoded[offset:offset + size]), int(entry["index"])
    return None


def load_first_palette_from_pics_bin(path: Path) -> tuple[list[tuple[int, int, int]], dict[str, object]] | None:
    if not path.exists():
        return None
    data = path.read_bytes()
    decoded, _decoded_nibbles = decompress_pics_bin_rle(data)
    entries = build_pics_bin_entries(decoded)
    embedded = first_embedded_raw_palette(decoded, entries)
    if embedded is None:
        return None
    palette, entry_index = embedded
    return palette, {
        "kind": "auto_raw_pics_palette_rgb_16_dac6",
        "path": str(path).replace("\\", "/"),
        "entry_index": entry_index,
    }


def resolve_corrected_render_profile(
    args: argparse.Namespace,
    source: Path,
    decoded: bytes,
    entries: list[dict[str, object]],
) -> dict[str, object] | None:
    if args.no_corrected:
        return None

    if args.corrected_raw_palette:
        palette_path = Path(args.corrected_raw_palette)
        return {
            "mode": "raw",
            "palette": decode_pics_palette(palette_path.read_bytes()),
            "metadata": {
                "kind": "corrected_raw_pics_palette_rgb_16_dac6",
                "path": str(palette_path).replace("\\", "/"),
            },
        }

    if args.raw_palette:
        palette_path = Path(args.raw_palette)
        return {
            "mode": "raw",
            "palette": decode_pics_palette(palette_path.read_bytes()),
            "metadata": {
                "kind": "corrected_from_raw_palette_argument",
                "path": str(palette_path).replace("\\", "/"),
            },
        }

    embedded = first_embedded_raw_palette(decoded, entries)
    if embedded is not None:
        palette, entry_index = embedded
        return {
            "mode": "raw",
            "palette": palette,
            "metadata": {
                "kind": "corrected_embedded_raw_pics_palette_rgb_16_dac6",
                "source": str(source).replace("\\", "/"),
                "entry_index": entry_index,
            },
        }

    mw_gpics = source.parent / "MW_GPICS.BIN"
    loaded = load_first_palette_from_pics_bin(mw_gpics)
    if loaded is not None:
        palette, metadata = loaded
        metadata["role"] = "corrected_default_palette_source"
        return {
            "mode": "raw",
            "palette": palette,
            "metadata": metadata,
        }

    # MW_PICS.BIN itself normally has no embedded palette. The verified EGA
    # previews use the standard EGA palette plus EGA_SOURCE_TO_GAME remap.
    # This fallback makes raw_images_corrected/ useful even when MW_GPICS.BIN
    # is not next to the file being extracted.
    if source.stem.upper() == "MW_PICS":
        return {
            "mode": "bin",
            "palette": EGA_PALETTE,
            "metadata": {
                "kind": "mw_pics_standard_ega_palette_with_game_remap",
                "reason": "MW_GPICS.BIN palette was not available; using verified EGA remap fallback",
            },
        }

    return None


def describe_pics_bin_entry(decoded: bytes, offset: int, size: int) -> dict[str, int | str]:
    if size == 48:
        return {
            "role": "palette_rgb_16_dac6",
            "palette_color_count": 16,
        }
    if size < 9 or offset + size > len(decoded):
        return {}
    width = int.from_bytes(decoded[offset:offset + 2], "little")
    height = int.from_bytes(decoded[offset + 2:offset + 4], "little")
    if width <= 0 or height <= 0 or width > 640 or height > 400:
        return {}
    packed_pixels = ((width + 1) // 2) * height
    trailing_size = size - 4 - packed_pixels
    if trailing_size < 0 or trailing_size > 32:
        return {}
    return {
        "role": "image_4bpp_packed",
        "width": width,
        "height": height,
        "pixel_data_offset": offset + 4,
        "pixel_data_size": packed_pixels,
        "row_stride_bytes": (width + 1) // 2,
        "trailing_size": trailing_size,
        "trailing_hex": decoded[offset + 4 + packed_pixels:offset + 4 + packed_pixels + trailing_size].hex().upper(),
    }


def build_pics_bin_entries(decoded: bytes) -> list[dict[str, object]]:
    header, offsets, sizes = parse_pics_bin_header(decoded)
    if not header:
        return []
    header_size = int(header["header_size"])
    entries: list[dict[str, object]] = []
    for index, (offset, declared_size) in enumerate(zip(offsets, sizes)):
        if offset == 0 and declared_size == 0:
            status = "empty"
            payload_sha256 = ""
            end_offset = 0
        elif offset < header_size or offset >= len(decoded) or declared_size <= 0 or offset + declared_size > len(decoded):
            status = "invalid"
            payload_sha256 = ""
            end_offset = 0
        else:
            status = "active"
            end_offset = offset + declared_size
            payload_sha256 = sha256_bytes(decoded[offset:end_offset])
        fields = describe_pics_bin_entry(decoded, offset, declared_size) if status == "active" else {}
        entries.append({
            "index": index,
            "offset": offset,
            "size": declared_size,
            "end_offset": end_offset,
            "status": status,
            "sha256": payload_sha256,
            "fields": fields,
        })
    return entries


def parse_pics_image_record(decoded: bytes, offset: int) -> dict[str, int | str]:
    if offset < 0 or offset + 4 > len(decoded):
        raise ValueError(f"image offset outside decoded data: 0x{offset:X}")
    width = int.from_bytes(decoded[offset:offset + 2], "little")
    raw_height = int.from_bytes(decoded[offset + 2:offset + 4], "little")
    height_flags = raw_height & 0x8000
    height = raw_height & 0x7FFF
    if width <= 0 or width > 640 or height <= 0 or height > 400:
        raise ValueError(f"not a plausible PICS image record at 0x{offset:X}: {width}x{raw_height:04X}")
    row_stride = (width + 1) // 2
    pixel_data_size = row_stride * height
    pixel_data_offset = offset + 4
    if pixel_data_offset + pixel_data_size > len(decoded):
        raise ValueError(f"PICS image record at 0x{offset:X} exceeds decoded data")
    return {
        "offset": offset,
        "width": width,
        "height": height,
        "raw_height": raw_height,
        "height_flags": height_flags,
        "row_stride_bytes": row_stride,
        "pixel_data_offset": pixel_data_offset,
        "pixel_data_size": pixel_data_size,
        "next_offset_without_padding": pixel_data_offset + pixel_data_size,
    }


def score_pics_raw_candidate(decoded: bytes, record: dict[str, int | str]) -> tuple[int, str]:
    pixel_data_offset = int(record["pixel_data_offset"])
    pixel_data_size = int(record["pixel_data_size"])
    sample = decoded[pixel_data_offset:pixel_data_offset + min(pixel_data_size, 512)]
    if not sample:
        return 0, "empty pixel sample"

    nibbles: list[int] = []
    for value in sample:
        nibbles.append(value >> 4)
        nibbles.append(value & 0x0F)
    counts: dict[int, int] = {}
    for nibble in nibbles:
        counts[nibble] = counts.get(nibble, 0) + 1

    unique = len(counts)
    dominant = max(counts.values()) / len(nibbles)
    common_palette = sum(
        count for nibble, count in counts.items()
        if nibble in {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xB, 0xC, 0xE}
    ) / len(nibbles)
    width = int(record["width"])
    height = int(record["height"])
    score = 0
    evidence = []
    if unique >= 2:
        score += 10
        evidence.append("multiple palette indexes")
    if dominant < 0.98:
        score += 10
        evidence.append("not single-color fill")
    if common_palette > 0.75:
        score += 10
        evidence.append("mostly low/common palette indexes")
    if width in {320, 286, 273, 206, 196, 145, 140, 101, 76, 72, 68, 64, 60, 49, 44, 40, 38, 29, 18, 17, 15, 6, 5}:
        score += 10
        evidence.append("known/near-known width")
    if height in {200, 154, 97, 68, 58, 48, 44, 35, 32, 28, 23, 21, 20, 17, 16, 12, 7, 5, 4}:
        score += 10
        evidence.append("known/near-known height")
    if int(record["height_flags"]):
        evidence.append("height high-bit flag set")
    return score, "; ".join(evidence)


def pics_4bpp_to_rgb(
    width: int,
    height: int,
    pixels_4bpp: bytes,
    palette: list[tuple[int, int, int]],
) -> list[list[tuple[int, int, int]]]:
    """Render row-aligned packed 4bpp pixels, high nibble first.

    MW_EGA image records are byte-aligned per scanline.  For odd widths the
    low nibble of the final byte in each row is padding and must not become
    the first pixel of the next row.  The previous extractor used one
    continuous pixel counter, which sheared all odd-width phase-0 images.
    """
    rows: list[list[tuple[int, int, int]]] = []
    row_stride = (width + 1) // 2
    for y in range(height):
        row = []
        row_offset = y * row_stride
        for x in range(width):
            byte_index = row_offset + (x // 2)
            byte = pixels_4bpp[byte_index] if byte_index < len(pixels_4bpp) else 0
            color_index = (byte >> 4) & 0x0F if (x & 1) == 0 else byte & 0x0F
            row.append(palette[color_index])
        rows.append(row)
    return rows


def pics_4bpp_to_cga_rgb(
    width: int,
    height: int,
    pixels_4bpp: bytes,
    patterns: list[list[int]],
    palette: list[tuple[int, int, int]],
) -> list[list[tuple[int, int, int]]]:
    rows: list[list[tuple[int, int, int]]] = []
    row_stride = (width + 1) // 2
    for y in range(height):
        row: list[tuple[int, int, int]] = []
        row_offset = y * row_stride
        for x in range(width):
            byte_index = row_offset + (x // 2)
            byte = pixels_4bpp[byte_index] if byte_index < len(pixels_4bpp) else 0
            color_index = (byte >> 4) & 0x0F if (x & 1) == 0 else byte & 0x0F
            for cga_index in patterns[color_index]:
                row.append(palette[cga_index & 0x03])
        rows.append(row)
    return rows


def render_4bpp_with_profile(
    width: int,
    height: int,
    pixels_4bpp: bytes,
    profile: dict[str, object],
) -> list[list[tuple[int, int, int]]]:
    if profile["mode"] == "cga":
        return pics_4bpp_to_cga_rgb(
            width,
            height,
            pixels_4bpp,
            profile["cga_patterns"],  # type: ignore[arg-type]
            profile["cga_palette"],  # type: ignore[arg-type]
        )
    return pics_4bpp_to_rgb(
        width,
        height,
        pixels_4bpp,
        profile["palette"],  # type: ignore[arg-type]
    )


def phasing_source_byte_count(width: int, height: int, nibble_phase: int) -> int:
    """Return bytes read by the manual-viewer nibble-phase renderer."""
    nibble_index = nibble_phase & 1
    max_byte_index = -1
    for _y in range(height):
        for _x in range(width):
            max_byte_index = max(max_byte_index, nibble_index >> 1)
            nibble_index += 1
        if width & 1:
            if (nibble_index & 1) != (nibble_phase & 1):
                nibble_index += 1
    return max_byte_index + 1


def pics_4bpp_to_rgb_phased(
    width: int,
    height: int,
    pixels_4bpp: bytes,
    palette: list[tuple[int, int, int]],
    nibble_phase: int,
) -> list[list[tuple[int, int, int]]]:
    rows: list[list[tuple[int, int, int]]] = []
    nibble_index = nibble_phase & 1
    for _y in range(height):
        row = []
        for _x in range(width):
            byte_index = nibble_index >> 1
            byte = pixels_4bpp[byte_index] if byte_index < len(pixels_4bpp) else 0
            color_index = (byte >> 4) & 0x0F if (nibble_index & 1) == 0 else byte & 0x0F
            row.append(palette[color_index])
            nibble_index += 1
        # This intentionally matches mw_pics_manual_viewer.html. Odd-width
        # manual records keep the selected nibble phase at each new row.
        if width & 1:
            if (nibble_index & 1) != (nibble_phase & 1):
                nibble_index += 1
        rows.append(row)
    return rows


def pics_4bpp_to_cga_rgb_phased(
    width: int,
    height: int,
    pixels_4bpp: bytes,
    patterns: list[list[int]],
    palette: list[tuple[int, int, int]],
    nibble_phase: int,
) -> list[list[tuple[int, int, int]]]:
    rows: list[list[tuple[int, int, int]]] = []
    nibble_index = nibble_phase & 1
    for _y in range(height):
        row: list[tuple[int, int, int]] = []
        for _x in range(width):
            byte_index = nibble_index >> 1
            byte = pixels_4bpp[byte_index] if byte_index < len(pixels_4bpp) else 0
            color_index = (byte >> 4) & 0x0F if (nibble_index & 1) == 0 else byte & 0x0F
            for cga_index in patterns[color_index]:
                row.append(palette[cga_index & 0x03])
            nibble_index += 1
        if width & 1:
            if (nibble_index & 1) != (nibble_phase & 1):
                nibble_index += 1
        rows.append(row)
    return rows


def render_4bpp_with_profile_phased(
    width: int,
    height: int,
    pixels_4bpp: bytes,
    profile: dict[str, object],
    nibble_phase: int,
) -> list[list[tuple[int, int, int]]]:
    # Always use the phased renderer for manual records.  nibble_phase=0 is
    # still significant for odd-width rows because each scanline is byte
    # aligned; falling back to the old continuous renderer shears the image.
    if profile["mode"] == "cga":
        return pics_4bpp_to_cga_rgb_phased(
            width,
            height,
            pixels_4bpp,
            profile["cga_patterns"],  # type: ignore[arg-type]
            profile["cga_palette"],  # type: ignore[arg-type]
            nibble_phase,
        )
    return pics_4bpp_to_rgb_phased(
        width,
        height,
        pixels_4bpp,
        profile["palette"],  # type: ignore[arg-type]
        nibble_phase,
    )


def scale_pixels_nearest(
    pixels: list[list[tuple[int, int, int]]],
    scale: int,
) -> list[list[tuple[int, int, int]]]:
    if scale <= 1:
        return pixels
    scaled: list[list[tuple[int, int, int]]] = []
    for row in pixels:
        expanded_row: list[tuple[int, int, int]] = []
        for pixel in row:
            expanded_row.extend([pixel] * scale)
        for _ in range(scale):
            scaled.append(list(expanded_row))
    return scaled


def remap_ega_source_to_game(
    pixels: list[list[tuple[int, int, int]]],
) -> list[list[tuple[int, int, int]]]:
    return [[EGA_RGB_REMAP.get(pixel, pixel) for pixel in row] for row in pixels]


def write_ppm(path: Path, pixels: list[list[tuple[int, int, int]]]) -> None:
    height = len(pixels)
    width = len(pixels[0]) if height else 0
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="ascii", newline="\n") as handle:
        handle.write(f"P3\n{width} {height}\n255\n")
        for row in pixels:
            handle.write(" ".join(f"{r} {g} {b}" for r, g, b in row))
            handle.write("\n")


def write_bmp(path: Path, pixels: list[list[tuple[int, int, int]]]) -> None:
    height = len(pixels)
    width = len(pixels[0]) if height else 0
    row_stride = ((width * 3 + 3) // 4) * 4
    pixel_data_size = row_stride * height
    file_size = 14 + 40 + pixel_data_size
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as handle:
        handle.write(b"BM")
        handle.write(struct.pack("<IHHI", file_size, 0, 0, 14 + 40))
        handle.write(struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, pixel_data_size, 0, 0, 0, 0))
        padding = b"\x00" * (row_stride - width * 3)
        for row in reversed(pixels):
            for r, g, b in row:
                handle.write(bytes((b, g, r)))
            handle.write(padding)


def write_png(path: Path, pixels: list[list[tuple[int, int, int]]]) -> None:
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError("PNG output requires Pillow. Use --image-format bmp or ppm without Pillow.") from exc

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
    scale: int,
    game_palette_remap: bool,
    force: bool,
) -> list[str]:
    output_pixels = remap_ega_source_to_game(pixels) if game_palette_remap else pixels
    output_pixels = scale_pixels_nearest(output_pixels, scale)
    formats = ["bmp", "ppm", "png"] if image_format == "all" else [image_format]
    written: list[str] = []
    for current_format in formats:
        out_path = base_path.parent / f"{base_path.name}.{current_format}"
        if out_path.exists() and not force:
            raise FileExistsError(f"refusing to overwrite existing file: {out_path}")
        if current_format == "ppm":
            write_ppm(out_path, output_pixels)
        elif current_format == "bmp":
            write_bmp(out_path, output_pixels)
        elif current_format == "png":
            write_png(out_path, output_pixels)
        else:
            raise ValueError(f"unsupported image format: {current_format}")
        written.append(str(out_path).replace("\\", "/"))
    return written


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        json.dump(value, handle, indent=2)
        handle.write("\n")


def write_entries_csv(path: Path, source: Path, entries: list[dict[str, object]]) -> None:
    fields = [
        "source_file",
        "entry_index",
        "offset_hex",
        "offset_dec",
        "size",
        "end_offset_hex",
        "status",
        "role",
        "width",
        "height",
        "row_stride_bytes",
        "trailing_size",
        "trailing_hex",
        "sha256",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for entry in entries:
            fields_data = entry.get("fields", {})
            if not isinstance(fields_data, dict):
                fields_data = {}
            writer.writerow({
                "source_file": str(source).replace("\\", "/"),
                "entry_index": entry["index"],
                "offset_hex": f"0x{int(entry['offset']):08X}",
                "offset_dec": entry["offset"],
                "size": entry["size"],
                "end_offset_hex": f"0x{int(entry['end_offset']):08X}" if int(entry["end_offset"]) else "",
                "status": entry["status"],
                "role": fields_data.get("role", ""),
                "width": fields_data.get("width", ""),
                "height": fields_data.get("height", ""),
                "row_stride_bytes": fields_data.get("row_stride_bytes", ""),
                "trailing_size": fields_data.get("trailing_size", ""),
                "trailing_hex": fields_data.get("trailing_hex", ""),
                "sha256": entry["sha256"],
            })


def write_raw_scan_csv(path: Path, rows: list[dict[str, int | str]]) -> None:
    fields = [
        "offset_hex",
        "offset_dec",
        "width",
        "height",
        "raw_height",
        "height_flags_hex",
        "row_stride_bytes",
        "pixel_data_size",
        "next_offset_without_padding_hex",
        "score",
        "evidence",
        "first_16_hex",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({
                "offset_hex": f"0x{int(row['offset']):08X}",
                "offset_dec": row["offset"],
                "width": row["width"],
                "height": row["height"],
                "raw_height": row["raw_height"],
                "height_flags_hex": f"0x{int(row['height_flags']):04X}",
                "row_stride_bytes": row["row_stride_bytes"],
                "pixel_data_size": row["pixel_data_size"],
                "next_offset_without_padding_hex": f"0x{int(row['next_offset_without_padding']):08X}",
                "score": row["score"],
                "evidence": row["evidence"],
                "first_16_hex": row["first_16_hex"],
            })


def scan_raw_image_candidates(
    decoded: bytes,
    min_score: int,
    min_area: int,
    max_area: int,
    limit: int,
) -> list[dict[str, int | str]]:
    rows: list[dict[str, int | str]] = []
    for offset in range(0, max(0, len(decoded) - 4)):
        try:
            record = parse_pics_image_record(decoded, offset)
        except ValueError:
            continue
        area = int(record["width"]) * int(record["height"])
        if area < min_area or area > max_area:
            continue
        score, evidence = score_pics_raw_candidate(decoded, record)
        if score < min_score:
            continue
        rows.append({
            **record,
            "score": score,
            "evidence": evidence,
            "first_16_hex": decoded[offset:offset + 16].hex().upper(),
        })
        if limit and len(rows) >= limit:
            break
    return rows


def iter_sources(input_path: Path) -> list[Path]:
    if input_path.is_dir():
        return sorted(path for path in input_path.iterdir() if path.suffix.upper() == ".BIN")
    return [input_path]


def export_active_entries(
    decoded: bytes,
    out_dir: Path,
    entries: list[dict[str, object]],
    force: bool,
) -> list[dict[str, object]]:
    raw_dir = out_dir / "entries_raw"
    manifest_entries = []
    for entry in entries:
        if entry["status"] != "active":
            continue
        entry_index = int(entry["index"])
        offset = int(entry["offset"])
        size = int(entry["size"])
        payload = decoded[offset:offset + size]
        target = raw_dir / f"entry_{entry_index:03d}_off_{offset:08X}_len_{size}.bin"
        if target.exists() and not force:
            raise FileExistsError(f"refusing to overwrite existing file: {target}")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
        manifest_entries.append({
            "entry_index": entry_index,
            "offset": offset,
            "size": size,
            "sha256": sha256_bytes(payload),
            "path": str(target).replace("\\", "/"),
        })
    return manifest_entries


def render_entries(
    decoded: bytes,
    out_dir: Path,
    entries: list[dict[str, object]],
    profile: dict[str, object],
    image_dir_name: str,
    image_format: str,
    scale: int,
    game_palette_remap: bool,
    force: bool,
) -> list[dict[str, object]]:
    outputs = []
    for entry in entries:
        fields = entry.get("fields", {})
        if entry["status"] != "active" or not isinstance(fields, dict) or fields.get("role") != "image_4bpp_packed":
            continue
        entry_index = int(entry["index"])
        width = int(fields["width"])
        height = int(fields["height"])
        pixel_data_offset = int(fields["pixel_data_offset"])
        pixel_data_size = int(fields["pixel_data_size"])
        pixels_4bpp = decoded[pixel_data_offset:pixel_data_offset + pixel_data_size]
        pixels = render_4bpp_with_profile(width, height, pixels_4bpp, profile)
        base_path = out_dir / image_dir_name / f"entry_{entry_index:03d}_{width}x{height}"
        paths = write_image_outputs(base_path, pixels, image_format, scale, game_palette_remap, force)
        outputs.append({
            "entry_index": entry_index,
            "width": width,
            "height": height,
            "output_width": len(pixels[0]) if pixels else 0,
            "output_height": len(pixels),
            "paths": paths,
        })
    return outputs



def known_record_to_runtime_record(decoded: bytes, spec: dict[str, object]) -> dict[str, int | str | bool]:
    """Return a normalized image-record description for a known/manual record."""
    offset = int(spec["offset"])
    width = int(spec["width"])
    height = int(spec["height"])
    has_header = bool(spec.get("has_header", True))
    header_size = int(spec.get("header_size", 4 if has_header else 0))
    nibble_phase = int(spec.get("nibble_phase", spec.get("phase", 0))) & 1
    row_stride = (width + 1) // 2
    pixel_data_offset = offset + header_size
    pixel_data_size = phasing_source_byte_count(width, height, nibble_phase)
    end_offset = pixel_data_offset + pixel_data_size
    declared_end = spec.get("end")
    if declared_end is not None:
        end_offset = max(end_offset, int(declared_end))
        pixel_data_size = end_offset - pixel_data_offset
    if offset < 0 or end_offset > len(decoded):
        raise ValueError(f"known record at 0x{offset:X} exceeds decoded data")
    validate_header = bool(spec.get("validate_header", has_header and header_size == 4 and nibble_phase == 0))
    if validate_header:
        stored_width = int.from_bytes(decoded[offset:offset + 2], "little")
        stored_height = int.from_bytes(decoded[offset + 2:offset + 4], "little")
        if stored_width != width or (stored_height & 0x7FFF) != height:
            raise ValueError(
                f"known headered record at 0x{offset:X} declares "
                f"{stored_width}x{stored_height & 0x7FFF}, expected {width}x{height}"
            )
    return {
        "offset": offset,
        "width": width,
        "height": height,
        "has_header": has_header,
        "header_size": header_size,
        "nibble_phase": nibble_phase,
        "row_stride_bytes": row_stride,
        "pixel_data_offset": pixel_data_offset,
        "pixel_data_size": pixel_data_size,
        "end_offset": end_offset,
        "span_size": end_offset - offset,
        "category": str(spec.get("category", "image")),
        "description": str(spec.get("description", "known image record")),
        "partial": bool(spec.get("partial", False)),
        "manual_mark_index": int(spec["manual_mark_index"]) if "manual_mark_index" in spec else -1,
        "validate_header": validate_header,
    }


def render_known_records(
    decoded: bytes,
    out_dir: Path,
    specs: list[dict[str, object]],
    profile: dict[str, object],
    image_dir_name: str,
    image_format: str,
    scale: int,
    game_palette_remap: bool,
    force: bool,
) -> list[dict[str, object]]:
    outputs: list[dict[str, object]] = []
    for index, spec in enumerate(specs):
        record = known_record_to_runtime_record(decoded, spec)
        width = int(record["width"])
        height = int(record["height"])
        pixel_data_offset = int(record["pixel_data_offset"])
        pixel_data_size = int(record["pixel_data_size"])
        pixels_4bpp = decoded[pixel_data_offset:pixel_data_offset + pixel_data_size]
        pixels = render_4bpp_with_profile_phased(width, height, pixels_4bpp, profile, int(record.get("nibble_phase", 0)))
        record_kind = "header" if bool(record["has_header"]) else "raw"
        if int(record.get("nibble_phase", 0)):
            record_kind += "_phase1"
        partial_suffix = "_partial" if bool(record["partial"]) else ""
        base_path = out_dir / image_dir_name / f"known_{index:03d}_{int(record['offset']):08X}_{record_kind}_{width}x{height}{partial_suffix}"
        paths = write_image_outputs(base_path, pixels, image_format, scale, game_palette_remap, force)
        outputs.append({
            "known_index": index,
            **record,
            "output_width": len(pixels[0]) if pixels else 0,
            "output_height": len(pixels),
            "paths": paths,
        })
    return outputs


def merge_ranges(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    merged: list[tuple[int, int]] = []
    for start, end in sorted((s, e) for s, e in ranges if e > s):
        if not merged or start > merged[-1][1]:
            merged.append((start, end))
        else:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
    return merged


def invert_ranges(total_size: int, covered_ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    gaps: list[tuple[int, int]] = []
    cursor = 0
    for start, end in merge_ranges(covered_ranges):
        start = max(0, min(total_size, start))
        end = max(0, min(total_size, end))
        if cursor < start:
            gaps.append((cursor, start))
        cursor = max(cursor, end)
    if cursor < total_size:
        gaps.append((cursor, total_size))
    return gaps


def ranges_from_entries(entries: list[dict[str, object]]) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for entry in entries:
        if entry.get("status") != "active":
            continue
        offset = int(entry["offset"])
        end_offset = int(entry["end_offset"])
        if end_offset > offset:
            ranges.append((offset, end_offset))
    return ranges


def ranges_from_known_records(decoded: bytes, specs: list[dict[str, object]]) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for spec in specs:
        record = known_record_to_runtime_record(decoded, spec)
        ranges.append((int(record["offset"]), int(record["end_offset"])))
    return ranges


def category_for_manual_mark(width: int, height: int) -> str:
    if width == 320 and height >= 130:
        return "scene"
    if width == 320:
        return "strip"
    if width == 68 and height == 68:
        return "mech"
    if width in {55, 60} and height in {49, 55}:
        return "emblem"
    if width in {129, 130} and height == 36:
        return "banner"
    if width in {90, 91} and height == 104:
        return "portrait"
    if width in {148, 149} and height == 127:
        return "panel"
    return "image"


def load_manual_mark_specs(path: Path) -> list[dict[str, object]]:
    """Load mw_pics_manual_viewer JSON marks as known-record descriptors."""
    payload = json.loads(path.read_text(encoding="utf-8"))
    specs: list[dict[str, object]] = []
    seen: set[tuple[int, int, int, int, int, int]] = set()
    for raw_index, mark in enumerate(payload.get("marks", [])):
        if mark.get("kind") != "image":
            continue
        offset = int(mark["offset"])
        width = int(mark["width"])
        height = int(mark["height"])
        mode = str(mark.get("mode", "headered"))
        header_size = 4 if mode == "headered" else 0
        nibble_phase = int(mark.get("phase", 0)) & 1
        end = int(mark.get("end", offset + header_size + ((width + 1) // 2) * height))
        key = (offset, end, width, height, header_size, nibble_phase)
        if key in seen:
            continue
        seen.add(key)
        specs.append({
            "offset": offset,
            "width": width,
            "height": height,
            "has_header": mode == "headered",
            "header_size": header_size,
            "nibble_phase": nibble_phase,
            "end": end,
            "validate_header": False,
            "category": category_for_manual_mark(width, height),
            "description": f"manual mark {len(specs):03d} from {path.name}; {mode}, phase {nibble_phase}",
            "manual_mark_index": len(specs),
        })
    return sorted(specs, key=lambda item: (int(item["offset"]), int(item.get("end", 0))))


def dedupe_known_specs(specs: list[dict[str, object]]) -> list[dict[str, object]]:
    deduped: list[dict[str, object]] = []
    seen: set[tuple[int, int, int, int, int]] = set()
    for spec in specs:
        key = (
            int(spec["offset"]),
            int(spec["width"]),
            int(spec["height"]),
            int(spec.get("header_size", 4 if bool(spec.get("has_header", True)) else 0)),
            int(spec.get("nibble_phase", spec.get("phase", 0))) & 1,
        )
        if key in seen:
            continue
        seen.add(key)
        deduped.append(spec)
    return deduped


def offset_in_ranges(offset: int, ranges: list[tuple[int, int]]) -> bool:
    return any(start <= offset < end for start, end in ranges)


def scan_uncovered_raw_candidates(
    decoded: bytes,
    covered_ranges: list[tuple[int, int]],
    min_score: int,
    min_area: int,
    max_area: int,
    limit: int,
) -> list[dict[str, int | str]]:
    rows: list[dict[str, int | str]] = []
    merged = merge_ranges(covered_ranges)
    for offset in range(0, max(0, len(decoded) - 4)):
        if offset_in_ranges(offset, merged):
            continue
        try:
            record = parse_pics_image_record(decoded, offset)
        except ValueError:
            continue
        record_start = int(record["offset"])
        record_end = int(record["next_offset_without_padding"])
        if any(max(record_start, start) < min(record_end, end) for start, end in merged):
            continue
        area = int(record["width"]) * int(record["height"])
        if area < min_area or area > max_area:
            continue
        score, evidence = score_pics_raw_candidate(decoded, record)
        if score < min_score:
            continue
        rows.append({
            **record,
            "score": score,
            "evidence": evidence,
            "first_16_hex": decoded[offset:offset + 16].hex().upper(),
        })
        if limit and len(rows) >= limit:
            break
    return rows


def write_known_records_csv(path: Path, records: list[dict[str, object]]) -> None:
    fields = [
        "known_index",
        "offset_hex",
        "offset_dec",
        "end_offset_hex",
        "span_size",
        "width",
        "height",
        "has_header",
        "pixel_data_offset_hex",
        "pixel_data_size",
        "nibble_phase",
        "header_size",
        "validate_header",
        "manual_mark_index",
        "category",
        "partial",
        "description",
        "paths",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for record in records:
            writer.writerow({
                "known_index": record.get("known_index", ""),
                "offset_hex": f"0x{int(record['offset']):08X}",
                "offset_dec": record["offset"],
                "end_offset_hex": f"0x{int(record['end_offset']):08X}",
                "span_size": record["span_size"],
                "width": record["width"],
                "height": record["height"],
                "has_header": record["has_header"],
                "pixel_data_offset_hex": f"0x{int(record['pixel_data_offset']):08X}",
                "pixel_data_size": record["pixel_data_size"],
                "nibble_phase": record.get("nibble_phase", 0),
                "header_size": record.get("header_size", ""),
                "validate_header": record.get("validate_header", ""),
                "manual_mark_index": record.get("manual_mark_index", ""),
                "category": record["category"],
                "partial": record["partial"],
                "description": record["description"],
                "paths": " ".join(str(path) for path in record.get("paths", [])),
            })


def write_coverage_csv(path: Path, rows: list[dict[str, object]]) -> None:
    fields = ["kind", "start_hex", "end_hex", "size", "notes"]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({
                "kind": row["kind"],
                "start_hex": f"0x{int(row['start']):08X}",
                "end_hex": f"0x{int(row['end']):08X}",
                "size": row["size"],
                "notes": row.get("notes", ""),
            })

def render_raw_offsets(
    decoded: bytes,
    out_dir: Path,
    offsets: list[int],
    profile: dict[str, object],
    image_dir_name: str,
    image_format: str,
    scale: int,
    game_palette_remap: bool,
    force: bool,
) -> list[dict[str, object]]:
    outputs = []
    for offset in offsets:
        record = parse_pics_image_record(decoded, offset)
        pixel_data_offset = int(record["pixel_data_offset"])
        pixel_data_size = int(record["pixel_data_size"])
        pixels_4bpp = decoded[pixel_data_offset:pixel_data_offset + pixel_data_size]
        pixels = render_4bpp_with_profile(int(record["width"]), int(record["height"]), pixels_4bpp, profile)
        suffix = f"{offset:08X}_{record['width']}x{record['height']}"
        if int(record["height_flags"]):
            suffix += f"_hflag_{int(record['height_flags']):04X}"
        base_path = out_dir / image_dir_name / f"raw_{suffix}"
        paths = write_image_outputs(base_path, pixels, image_format, scale, game_palette_remap, force)
        outputs.append({
            **record,
            "output_width": len(pixels[0]) if pixels else 0,
            "output_height": len(pixels),
            "paths": paths,
        })
    return outputs


def export_one(source: Path, out_root: Path, args: argparse.Namespace) -> dict[str, object]:
    data = source.read_bytes()
    decoded, decoded_nibble_count = decompress_pics_bin_rle(data)
    header, _offsets, _sizes = parse_pics_bin_header(decoded)
    entries = build_pics_bin_entries(decoded)

    resource_dir = out_root / safe_name(source.stem)
    resource_dir.mkdir(parents=True, exist_ok=True)

    decoded_path = ""
    if not args.no_decoded:
        target = resource_dir / f"{safe_name(source.stem)}.decoded.bin"
        if target.exists() and not args.force:
            raise FileExistsError(f"refusing to overwrite existing file: {target}")
        target.write_bytes(decoded)
        decoded_path = str(target).replace("\\", "/")

    write_entries_csv(resource_dir / "entries.csv", source, entries)
    raw_entries = export_active_entries(decoded, resource_dir, entries, args.force) if entries else []
    render_profile = resolve_render_profile(args, decoded, entries)
    palette_metadata = render_profile["metadata"]
    corrected_profile = resolve_corrected_render_profile(args, source, decoded, entries)
    corrected_palette_metadata = corrected_profile["metadata"] if corrected_profile else {
        "kind": "unavailable",
        "reason": "no embedded or default MW_GPICS raw palette was found",
    }
    rendered_entries = render_entries(
        decoded,
        resource_dir,
        entries,
        render_profile,
        "images",
        args.image_format,
        args.scale,
        args.game_palette_remap,
        args.force,
    ) if entries else []
    corrected_rendered_entries = render_entries(
        decoded,
        resource_dir,
        entries,
        corrected_profile,
        "images_corrected",
        args.image_format,
        args.scale,
        True,
        args.force,
    ) if entries and corrected_profile else []

    known_specs = list(MW_PICS_RECORDS.get(source.stem.upper(), []))
    for manual_marks_path in args.manual_marks or []:
        known_specs.extend(load_manual_mark_specs(Path(manual_marks_path)))
    known_specs = dedupe_known_specs(known_specs)
    known_rendered = render_known_records(
        decoded,
        resource_dir,
        known_specs,
        render_profile,
        "mw_pics_images",
        args.image_format,
        args.scale,
        args.game_palette_remap,
        args.force,
    ) if known_specs else []
    corrected_known_rendered = render_known_records(
        decoded,
        resource_dir,
        known_specs,
        corrected_profile,
        "mw_pics_images_corrected",
        args.image_format,
        args.scale,
        True,
        args.force,
    ) if known_specs and corrected_profile else []
    if known_rendered:
        write_known_records_csv(resource_dir / "known_records.csv", known_rendered)

    coverage_ranges = ranges_from_entries(entries) + ranges_from_known_records(decoded, known_specs)
    merged_coverage_ranges = merge_ranges(coverage_ranges)
    unknown_ranges = invert_ranges(len(decoded), merged_coverage_ranges)
    coverage_rows: list[dict[str, object]] = []
    for start, end in merged_coverage_ranges:
        coverage_rows.append({"kind": "covered", "start": start, "end": end, "size": end - start, "notes": "active table entry or verified MW_PICS image"})
    for start, end in unknown_ranges:
        coverage_rows.append({"kind": "unknown", "start": start, "end": end, "size": end - start, "notes": "not covered by verified exported images"})
    write_coverage_csv(resource_dir / "coverage.csv", sorted(coverage_rows, key=lambda row: int(row["start"])))

    raw_scan_rows: list[dict[str, int | str]] = []
    if args.scan_raw or (not entries and not known_specs):
        raw_scan_rows = scan_raw_image_candidates(
            decoded,
            args.min_raw_score,
            args.min_raw_area,
            args.max_raw_area,
            args.raw_limit,
        )
        write_raw_scan_csv(resource_dir / "raw_candidates.csv", raw_scan_rows)

    uncovered_raw_scan_rows: list[dict[str, int | str]] = []
    if args.scan_coverage_gaps or known_specs:
        uncovered_raw_scan_rows = scan_uncovered_raw_candidates(
            decoded,
            merged_coverage_ranges,
            args.min_raw_score,
            args.min_raw_area,
            args.max_raw_area,
            args.raw_limit,
        )
        write_raw_scan_csv(resource_dir / "uncovered_raw_candidates.csv", uncovered_raw_scan_rows)

    raw_offsets = list(KNOWN_RAW_OFFSETS.get(source.stem.upper(), []))
    for offset_text in args.raw_offset or []:
        raw_offsets.append(int(offset_text, 16) if offset_text.lower().startswith("0x") else int(offset_text))
    if args.render_raw_candidates:
        raw_offsets.extend(int(row["offset"]) for row in raw_scan_rows)
    raw_offsets = sorted(set(raw_offsets))

    raw_rendered = []
    raw_errors = []
    for offset in raw_offsets:
        try:
            raw_rendered.extend(render_raw_offsets(
                decoded,
                resource_dir,
                [offset],
                render_profile,
                "raw_images",
                args.image_format,
                args.scale,
                args.game_palette_remap,
                args.force,
            ))
        except ValueError as exc:
            raw_errors.append({"offset": offset, "error": str(exc)})

    corrected_raw_rendered = []
    corrected_raw_errors = []
    if corrected_profile:
        for offset in raw_offsets:
            try:
                corrected_raw_rendered.extend(render_raw_offsets(
                    decoded,
                    resource_dir,
                    [offset],
                    corrected_profile,
                    "raw_images_corrected",
                    args.image_format,
                    args.scale,
                    True,
                    args.force,
                ))
            except ValueError as exc:
                corrected_raw_errors.append({"offset": offset, "error": str(exc)})

    active_count = sum(1 for entry in entries if entry["status"] == "active")
    empty_count = sum(1 for entry in entries if entry["status"] == "empty")
    invalid_count = sum(1 for entry in entries if entry["status"] == "invalid")
    image_count = sum(
        1 for entry in entries
        if entry["status"] == "active"
        and isinstance(entry.get("fields"), dict)
        and entry["fields"].get("role") == "image_4bpp_packed"  # type: ignore[index]
    )
    palette_count = sum(
        1 for entry in entries
        if entry["status"] == "active"
        and isinstance(entry.get("fields"), dict)
        and entry["fields"].get("role") == "palette_rgb_16_dac6"  # type: ignore[index]
    )

    meaningful_record_count = len(known_rendered) if known_rendered else image_count
    coverage_bytes = sum(end - start for start, end in merged_coverage_ranges)
    unknown_bytes = sum(end - start for start, end in unknown_ranges)
    coverage_percent = round(coverage_bytes * 100 / len(decoded), 6) if decoded else 0.0
    unknown_percent = round(unknown_bytes * 100 / len(decoded), 6) if decoded else 0.0

    manifest: dict[str, object] = {
        "tool": "Sorted Original Files/BIN/bin_extract/bin_extract.py",
        "schema_version": SCHEMA_VERSION,
        "source": {
            "path": str(source).replace("\\", "/"),
            "size": len(data),
            "sha256": sha256_bytes(data),
        },
        "codec": "mw_pics_nibble_rle",
        "decoded_nibble_count": decoded_nibble_count,
        "decoded_size": len(decoded),
        "decoded_sha256": sha256_bytes(decoded),
        "decoded_path": decoded_path,
        "recognized_header": bool(header),
        "decoded_header_candidate": header,
        "video_mode": render_profile["mode"],
        "palette": palette_metadata,
        "game_palette_remap": bool(args.game_palette_remap),
        "corrected_palette": corrected_palette_metadata,
        "corrected_game_palette_remap": bool(corrected_profile),
        "rendering_notes": [
            "bin mode uses the first active 48-byte BIN DAC palette when present, otherwise standard EGA colors.",
            "raw mode uses a user-supplied 48-byte RGB DAC palette, useful for MW_PICS.BIN raw records.",
            "ega mode maps 4bpp indexes through PAL:EGA bank bytes.",
            "cga mode maps each 4bpp source index through a PAL:CGA 8-pixel 2bpp lookup pattern.",
            "game_palette_remap applies the project-observed EGA source-index to runtime-index remap.",
            "images_corrected/raw_images_corrected are generated with raw-palette mode plus game_palette_remap when a corrected palette is available.",
            "MW_PICS known records may include manual header_size/nibble_phase descriptors produced by mw_pics_manual_viewer; those are rendered exactly like the viewer and are not always runtime u16 width/u16 height records.",
        ],
        "entry_count": len(entries),
        "active_entry_count": active_count,
        "empty_entry_count": empty_count,
        "invalid_entry_count": invalid_count,
        "image_entry_count": image_count,
        "palette_entry_count": palette_count,
        "raw_extracted_entries": raw_entries,
        "rendered_entries": rendered_entries,
        "corrected_rendered_entries": corrected_rendered_entries,
        "known_record_count": len(known_rendered),
        "known_records": known_rendered,
        "corrected_known_records": corrected_known_rendered,
        "raw_candidate_count": len(raw_scan_rows),
        "uncovered_raw_candidate_count": len(uncovered_raw_scan_rows),
        "raw_rendered_entries": raw_rendered,
        "corrected_raw_rendered_entries": corrected_raw_rendered,
        "raw_render_errors": raw_errors,
        "corrected_raw_render_errors": corrected_raw_errors,
        "coverage_report": {
            "decoded_size": len(decoded),
            "covered_bytes": coverage_bytes,
            "covered_percent": coverage_percent,
            "unknown_bytes": unknown_bytes,
            "unknown_percent": unknown_percent,
            "unknown_range_count": len(unknown_ranges),
            "unknown_ranges": [
                {
                    "start": start,
                    "end": end,
                    "start_hex": f"0x{start:08X}",
                    "end_hex": f"0x{end:08X}",
                    "size": end - start,
                }
                for start, end in unknown_ranges
            ],
            "uncovered_raw_candidate_count": len(uncovered_raw_scan_rows),
            "notes": [
                "Coverage is byte coverage in the decoded stream, not in the compressed source file.",
                "Unknown ranges are not covered by verified exported images; they may contain tables, scripts, masks, metadata, or non-EGA data.",
                "uncovered_raw_candidates.csv lists plausible u16 width/u16 height false-positive candidates outside verified image spans for visual audit.",
            ],
        },
        "assessment": {
            "coverage": (
                "mw_pics_verified_record_export" if known_rendered
                else "full_table_export" if header and invalid_count == 0
                else "partial_raw_image_export" if raw_rendered
                else "decoded_only"
            ),
            "notes": (
                "MW_PICS.BIN has no regular table; verified headered and raw/unheaded records were exported from a forensic map."
                if known_rendered
                else "Header table parsed cleanly; active entries were extracted and image entries rendered."
                if header and invalid_count == 0
                else "No recognized offset/size table; decoded stream is saved and raw image records are heuristic."
            ),
        },
    }
    write_json(resource_dir / "manifest.json", manifest)
    return manifest


def write_summary(out_dir: Path, manifests: list[dict[str, object]]) -> None:
    summary = {
        "tool": "Sorted Original Files/BIN/bin_extract/bin_extract.py",
        "schema_version": SCHEMA_VERSION,
        "output": str(out_dir).replace("\\", "/"),
        "file_count": len(manifests),
        "total_active_entries": sum(int(item["active_entry_count"]) for item in manifests),
        "total_rendered_images": sum(
            len(item["rendered_entries"]) + len(item["raw_rendered_entries"]) + len(item.get("known_records", []))
            for item in manifests
        ),
        "total_corrected_images": sum(
            len(item["corrected_rendered_entries"]) + len(item["corrected_raw_rendered_entries"]) + len(item.get("corrected_known_records", []))
            for item in manifests
        ),
        "full_table_export_files": sum(1 for item in manifests if item["assessment"]["coverage"] == "full_table_export"),  # type: ignore[index]
        "partial_raw_image_export_files": sum(1 for item in manifests if item["assessment"]["coverage"] == "partial_raw_image_export"),  # type: ignore[index]
        "files": [
            {
                "source": item["source"]["path"],  # type: ignore[index]
                "source_size": item["source"]["size"],  # type: ignore[index]
                "decoded_size": item["decoded_size"],
                "recognized_header": item["recognized_header"],
                "video_mode": item["video_mode"],
                "palette_kind": item["palette"]["kind"],  # type: ignore[index]
                "game_palette_remap": item["game_palette_remap"],
                "corrected_palette_kind": item["corrected_palette"]["kind"],  # type: ignore[index]
                "entry_count": item["entry_count"],
                "active_entry_count": item["active_entry_count"],
                "empty_entry_count": item["empty_entry_count"],
                "invalid_entry_count": item["invalid_entry_count"],
                "image_entry_count": item["image_entry_count"],
                "palette_entry_count": item["palette_entry_count"],
                "rendered_entry_count": len(item["rendered_entries"]),
                "corrected_rendered_entry_count": len(item["corrected_rendered_entries"]),
                "known_record_count": item.get("known_record_count", 0),
                "corrected_known_record_count": len(item.get("corrected_known_records", [])),
                "raw_rendered_count": len(item["raw_rendered_entries"]),
                "corrected_raw_rendered_count": len(item["corrected_raw_rendered_entries"]),
                "raw_candidate_count": item["raw_candidate_count"],
                "uncovered_raw_candidate_count": item.get("uncovered_raw_candidate_count", 0),
                "covered_percent": item.get("coverage_report", {}).get("covered_percent", ""),
                "unknown_percent": item.get("coverage_report", {}).get("unknown_percent", ""),
                "unknown_range_count": item.get("coverage_report", {}).get("unknown_range_count", ""),
                "coverage": item["assessment"]["coverage"],  # type: ignore[index]
            }
            for item in manifests
        ],
    }
    write_json(out_dir / "summary.json", summary)

    fields = [
        "source",
        "source_size",
        "decoded_size",
        "recognized_header",
        "video_mode",
        "palette_kind",
        "game_palette_remap",
        "corrected_palette_kind",
        "entry_count",
        "active_entry_count",
        "empty_entry_count",
        "invalid_entry_count",
        "image_entry_count",
        "palette_entry_count",
        "rendered_entry_count",
        "corrected_rendered_entry_count",
        "known_record_count",
        "corrected_known_record_count",
        "raw_rendered_count",
        "corrected_raw_rendered_count",
        "raw_candidate_count",
        "uncovered_raw_candidate_count",
        "covered_percent",
        "unknown_percent",
        "unknown_range_count",
        "coverage",
    ]
    with (out_dir / "summary.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(summary["files"])


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export MechWarrior MW_*PICS.BIN decoded streams, entries, palettes, and image records."
    )
    parser.add_argument(
        "input",
        nargs="?",
        default=str(DEFAULT_INPUT),
        help="BIN file or directory with BIN files; default: parent BIN directory",
    )
    parser.add_argument("--out", default=str(DEFAULT_OUT), help="output directory")
    parser.add_argument("--image-format", choices=["bmp", "ppm", "png", "all"], default="bmp")
    parser.add_argument("--scale", type=int, default=1, help="nearest-neighbor scale for rendered images")
    parser.add_argument(
        "--video-mode",
        choices=["auto", "bin", "raw", "ega", "cga"],
        default="auto",
        help="rendering palette mode: auto uses raw palette, PAL:EGA, or BIN palette depending on supplied arguments",
    )
    parser.add_argument("--palette", help="optional tagged PAL file used by --video-mode ega or cga")
    parser.add_argument("--raw-palette", help="optional 48-byte RGB DAC palette used by --video-mode raw")
    parser.add_argument(
        "--corrected-raw-palette",
        help="48-byte RGB DAC palette for automatic images_corrected output; defaults to MW_GPICS.BIN entry 0",
    )
    parser.add_argument("--palette-bank", type=int, default=0, help="PAL:EGA bank index, 0..3")
    parser.add_argument("--palette-interpretation", choices=["low", "high"], default="low")
    parser.add_argument("--cga-table", type=int, default=0, help="PAL:CGA lookup table index, 0..4")
    parser.add_argument(
        "--cga-palette",
        choices=sorted(CGA_VIDEO_PALETTES),
        default="palette1-high",
        help="CGA monitor palette used for PAL:CGA 2bpp indexes",
    )
    parser.add_argument(
        "--game-palette-remap",
        action="store_true",
        help="apply the project-observed EGA source-index to runtime-index remap",
    )
    parser.add_argument("--no-decoded", action="store_true", help="do not write full decoded *.decoded.bin streams")
    parser.add_argument("--scan-raw", action="store_true", help="also scan recognized files for raw image-record candidates")
    parser.add_argument("--render-raw-candidates", action="store_true", help="render raw scan candidates; may include false positives")
    parser.add_argument(
        "--scan-coverage-gaps",
        action="store_true",
        help="scan decoded ranges not covered by verified entries/known records for raw image-record candidates",
    )
    parser.add_argument(
        "--manual-marks",
        action="append",
        help="optional mw_pics_manual_viewer JSON file; image marks are added as MW_PICS known records",
    )
    parser.add_argument("--raw-offset", action="append", help="render a raw image record by decoded offset, decimal or hex")
    parser.add_argument("--min-raw-score", type=int, default=40)
    parser.add_argument("--min-raw-area", type=int, default=16)
    parser.add_argument("--max-raw-area", type=int, default=64000)
    parser.add_argument("--raw-limit", type=int, default=0, help="maximum raw scan candidates, 0 means no limit")
    parser.add_argument("--no-corrected", action="store_true", help="skip automatic images_corrected/raw_images_corrected output")
    parser.add_argument("--force", action="store_true", help="overwrite existing outputs")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    input_path = Path(args.input)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    manifests = []
    for source in iter_sources(input_path):
        manifests.append(export_one(source, out_dir, args))
    write_summary(out_dir, manifests)

    rendered_total = sum(
        len(item["rendered_entries"]) + len(item["raw_rendered_entries"]) + len(item.get("known_records", []))
        for item in manifests
    )
    corrected_total = sum(
        len(item["corrected_rendered_entries"]) + len(item["corrected_raw_rendered_entries"]) + len(item.get("corrected_known_records", []))
        for item in manifests
    )
    print(
        f"Exported {len(manifests)} BIN file(s), rendered {rendered_total} image(s) "
        f"and {corrected_total} corrected image(s) to {out_dir}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
