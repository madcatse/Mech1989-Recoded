#!/usr/bin/env python3
"""
mw1989_tbl_to_mesh_v3.py

Experimental deep parser/converter for MechWarrior (1989) / Dynamix PCK/TBL
model resources such as JENPCK.TBL, LOCPCK.TBL and MARPCK.TBL.

Compared with the first parser, v2 fixes the important bug that only records with
group_count 1 or 7 were recognized. LOCPCK and MARPCK also contain valid records
with group_count 2, 3 and 6.

Pipeline:
  1. unpack Dynamix/Sierra compression wrapper;
  2. scan decompressed data for candidate 0x0080 3D records;
  3. parse int16 XYZ vertex arrays;
  4. parse 0xff-terminated primitive index lists;
  5. export OBJ/PLY/experimental STEP/JSON and optional matplotlib tri-view PNGs.

This remains a reverse-engineering aid. The exact game-time assembly transforms,
materials, draw order, and mech part semantics are still unresolved.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple


# ----------------------------------------------------------------------------
# Basic binary helpers
# ----------------------------------------------------------------------------

def u16(buf: bytes, off: int) -> int:
    return struct.unpack_from("<H", buf, off)[0]


def s16(buf: bytes, off: int) -> int:
    return struct.unpack_from("<h", buf, off)[0]


def u32(buf: bytes, off: int) -> int:
    return struct.unpack_from("<I", buf, off)[0]


def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    c = Counter(data)
    n = len(data)
    return -sum((count / n) * math.log2(count / n) for count in c.values())


# ----------------------------------------------------------------------------
# Dynamix/Sierra compression wrapper
# ----------------------------------------------------------------------------

class BitEOF(Exception):
    pass


class LsbBitReader:
    """Read little-endian bit-packed codes from a byte stream."""

    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0
        self.current = 0
        self.bit = 8
        self.bits_read = 0

    def read_bits(self, n: int) -> int:
        value = 0
        for out_bit in range(n):
            if self.bit >= 8:
                if self.pos >= len(self.data):
                    raise BitEOF()
                self.current = self.data[self.pos]
                self.pos += 1
                self.bit = 0
            if self.current & (1 << self.bit):
                value |= 1 << out_bit
            self.bit += 1
            self.bits_read += 1
        return value

    @property
    def fully_consumed(self) -> bool:
        return self.pos == len(self.data) and self.bit == 8


def unpack_lzw_dynamix(payload: bytes, expected_size: int) -> Tuple[bytes, Dict[str, Any]]:
    """
    Dynamix LZW variant observed in these files:
      - literal codes 0..255
      - reset code 256
      - first free dictionary code 257
      - code width 9..12 bits
      - little-endian bit packing
      - on reset, ignore padding codewords until code count at current width is
        divisible by 8.
    """
    br = LsbBitReader(payload)
    table: List[Optional[Tuple[int, int]]] = [None] * 4096  # code -> (prefix_code, appended_byte)
    out = bytearray()
    stack: List[int] = []

    nbits = 9
    free_code = 257
    old_code: Optional[int] = None
    last_byte = 0
    resets = 0
    width_changes = 0
    codes_at_width = 0
    total_codes = 0

    def read_code() -> int:
        nonlocal codes_at_width, total_codes
        code = br.read_bits(nbits)
        codes_at_width += 1
        total_codes += 1
        return code

    def reset_dictionary() -> None:
        nonlocal nbits, free_code, old_code, last_byte, codes_at_width, resets
        resets += 1
        while codes_at_width % 8 != 0:
            try:
                read_code()
            except BitEOF:
                break
        nbits = 9
        free_code = 257
        old_code = None
        last_byte = 0
        codes_at_width = 0

    try:
        while len(out) < expected_size:
            code = read_code()

            if code == 256:
                reset_dictionary()
                continue

            if old_code is None:
                if code > 255:
                    raise ValueError(f"First LZW code after reset/start is not a literal: {code}")
                out.append(code)
                old_code = code
                last_byte = code
                continue

            decode_code = code
            if decode_code >= free_code:
                # Standard KwKwK case.
                stack.append(last_byte)
                decode_code = old_code

            guard = 0
            while decode_code >= 256:
                entry = table[decode_code]
                if entry is None:
                    raise ValueError(
                        f"Missing LZW dictionary entry {decode_code} at output offset 0x{len(out):x}"
                    )
                prefix, appended = entry
                stack.append(appended)
                decode_code = prefix
                guard += 1
                if guard > 4096:
                    raise ValueError("LZW dictionary cycle detected")

            stack.append(decode_code & 0xFF)
            last_byte = decode_code & 0xFF

            while stack and len(out) < expected_size:
                out.append(stack.pop())
            stack.clear()

            if free_code < 4096:
                table[free_code] = (old_code, last_byte)
                free_code += 1
                if free_code >= (1 << nbits) and nbits < 12:
                    nbits += 1
                    width_changes += 1
                    codes_at_width = 0

            old_code = code

    except BitEOF as exc:
        raise ValueError(
            f"Compressed stream ended early at output offset 0x{len(out):x}; expected 0x{expected_size:x}"
        ) from exc

    return bytes(out), {
        "resets": resets,
        "width_changes": width_changes,
        "total_codes_read": total_codes,
        "payload_bytes_consumed": br.pos,
        "payload_bits_in_last_byte": br.bit,
        "payload_fully_consumed": br.fully_consumed,
    }


def unpack_rle_dynamix(payload: bytes, expected_size: int) -> Tuple[bytes, Dict[str, Any]]:
    out = bytearray()
    pos = 0
    while pos < len(payload) and len(out) < expected_size:
        control = payload[pos]
        pos += 1
        if control & 0x80:
            if pos >= len(payload):
                raise ValueError("RLE repeat command lacks count byte")
            count = payload[pos]
            pos += 1
            out.extend([control & 0x7F] * count)
        else:
            count = control
            out.extend(payload[pos:pos + count])
            pos += count
    if len(out) < expected_size:
        raise ValueError(f"RLE output too short: {len(out)} < {expected_size}")
    return bytes(out[:expected_size]), {
        "payload_bytes_consumed": pos,
        "payload_fully_consumed": pos == len(payload),
    }


def unpack_dynamix_block(data: bytes) -> Tuple[bytes, Dict[str, Any]]:
    """
    Unpack a Dynamix compressed block. If the first 5 bytes do not look like a
    plausible wrapper, return the input as already-unpacked.
    """
    if len(data) < 5:
        raise ValueError("Input too short")

    ctype = data[0]
    expected_size = u32(data, 1)
    payload = data[5:]

    if ctype not in (0, 1, 2) or expected_size == 0 or expected_size > 64 * 1024 * 1024:
        return data, {
            "compression_type": None,
            "method": "already-unpacked/unknown-wrapper",
            "declared_unpacked_size": None,
            "input_size": len(data),
            "payload_size": None,
            "output_size": len(data),
            "output_size_matches_header": None,
            "compressed_entropy_bits_per_byte": entropy(data),
            "unpacked_entropy_bits_per_byte": entropy(data),
        }

    if ctype == 0:
        out = payload[:expected_size]
        method = "stored/uncompressed"
        details: Dict[str, Any] = {
            "payload_bytes_consumed": len(out),
            "payload_fully_consumed": len(payload) == expected_size,
        }
    elif ctype == 1:
        out, details = unpack_rle_dynamix(payload, expected_size)
        method = "Dynamix RLE"
    else:
        out, details = unpack_lzw_dynamix(payload, expected_size)
        method = "Dynamix LZW"

    report = {
        "compression_type": ctype,
        "method": method,
        "declared_unpacked_size": expected_size,
        "input_size": len(data),
        "payload_size": len(payload),
        "output_size": len(out),
        "output_size_matches_header": len(out) == expected_size,
        "compressed_entropy_bits_per_byte": entropy(payload),
        "unpacked_entropy_bits_per_byte": entropy(out),
        **details,
    }
    return out, report


# ----------------------------------------------------------------------------
# Candidate model parser v3
# ----------------------------------------------------------------------------

def parse_record_header_descriptors(buf: bytes, off: int, group_count: int) -> Optional[List[Dict[str, int]]]:
    """
    Validate and parse the repeated 8-byte descriptors in the model record header.

    Observed descriptor shape:
      uint16 link_or_offset_hint;
      uint8  start_vertex_hint;       // first is 0, nondecreasing
      uint8  vertex_count;            // same in all descriptors
      uint16 extent_or_sort_hint;
      uint16 constant_one;            // always 1 in observed files
    """
    if off + 12 + group_count * 8 + 6 > len(buf):
        return None
    descs: List[Dict[str, int]] = []
    vertex_count = buf[off + 15]
    prev_start = -1
    for i in range(group_count):
        p = off + 12 + i * 8
        link = u16(buf, p)
        start_hint = buf[p + 2]
        vc = buf[p + 3]
        extent = u16(buf, p + 4)
        one = u16(buf, p + 6)
        if vc != vertex_count:
            return None
        if one != 1:
            return None
        if i == 0 and start_hint != 0:
            return None
        if not (0 <= start_hint < vertex_count):
            return None
        if start_hint < prev_start:
            return None
        prev_start = start_hint
        descs.append({
            "index": i,
            "link_or_offset_hint": link,
            "start_vertex_hint": start_hint,
            "vertex_count": vc,
            "extent_or_sort_hint": extent,
            "constant_one": one,
        })
    return descs


def valid_group_core(buf: bytes, abs_pos: int) -> bool:
    if abs_pos + 8 >= len(buf):
        return False
    if u16(buf, abs_pos) != 0:
        return False
    primitive_count = u16(buf, abs_pos + 2)
    return 1 <= primitive_count <= 128


def looks_like_record(buf: bytes, off: int, *, max_group_count: int = 16, max_vertex_count: int = 128) -> bool:
    if off + 32 >= len(buf):
        return False
    if buf[off:off + 2] != b"\x80\x00":
        return False
    if buf[off + 4:off + 10] != b"\x00" * 6:
        return False
    group_count = u16(buf, off + 10)
    if not (1 <= group_count <= max_group_count):
        return False
    vertex_count = buf[off + 15]
    if not (1 <= vertex_count <= max_vertex_count):
        return False
    header_descs = parse_record_header_descriptors(buf, off, group_count)
    if header_descs is None:
        return False

    vertex_start = 12 + group_count * 8 + 6
    face_start = off + vertex_start + vertex_count * 6
    if face_start >= len(buf):
        return False

    # Most records have the group core exactly here. One known JENPCK record has
    # 12 bytes of padding/control bytes before the first non-empty group.
    return any(valid_group_core(buf, face_start + delta) for delta in range(0, 65))


def scan_records(buf: bytes, *, max_group_count: int = 16, max_vertex_count: int = 128) -> List[int]:
    return [
        i for i in range(0, max(0, len(buf) - 32))
        if looks_like_record(buf, i, max_group_count=max_group_count, max_vertex_count=max_vertex_count)
    ]


def normalize_primitive_indices(raw: List[int]) -> Tuple[List[int], bool]:
    """
    JENPCK and MARPCK commonly encode primitive lists as N,N,... while LOCPCK
    uses plain index lists. If the duplicated leading value is present, drop the
    first copy for modern mesh export.
    """
    if len(raw) >= 2 and raw[0] == raw[1]:
        return raw[1:], True
    return raw, False


def classify_primitive(opcode: int, indices_1based: List[int]) -> str:
    if opcode == 0x80 or len(indices_1based) == 2:
        return "line"
    if opcode == 0x81 and len(indices_1based) >= 3:
        return "polygon"
    if len(indices_1based) >= 3:
        return "polygon_candidate"
    return "degenerate"


def parse_primitive_group(buf: bytes, rec_start: int, rec_end: int, pos: int, has_prefix: bool) -> Tuple[Dict[str, Any], int]:
    prefix = None
    if has_prefix:
        prefix = {
            "prefix_a": u16(buf, pos),
            "prefix_b_offset_hint": u16(buf, pos + 2),
        }
        pos += 4

    group_core_pos = pos
    zero = u16(buf, pos)
    primitive_count = u16(buf, pos + 2)
    descriptor_hint = u16(buf, pos + 4)
    group_bytes = [buf[pos + 6], buf[pos + 7]]

    desc_start = pos + 8
    descs: List[Dict[str, Any]] = []
    for j in range(primitive_count):
        dpos = desc_start + j * 8
        desc = buf[dpos:dpos + 8]
        if len(desc) < 8:
            raise ValueError("Descriptor table extends past record end")
        descs.append({
            "index": j,
            "offset": dpos - rec_start,
            "raw_hex": desc.hex(),
            "opcode": desc[0],
            "shade_or_mask": list(desc[1:5]),
            "byte_5": desc[5],
            "u16_at_6": u16(desc, 6),
        })

    q = desc_start + primitive_count * 8
    prims: List[Dict[str, Any]] = []
    for j in range(primitive_count):
        vals: List[int] = []
        start = q
        while q < rec_end and buf[q] != 0xFF:
            vals.append(buf[q])
            q += 1
        terminated = q < rec_end and buf[q] == 0xFF
        if terminated:
            q += 1

        norm, dropped_duplicate = normalize_primitive_indices(vals)
        opcode = descs[j]["opcode"] if j < len(descs) else -1
        prims.append({
            "index": j,
            "offset": start - rec_start,
            "raw_indices": vals,
            "indices_1based_guess": norm,
            "dropped_duplicate_first_index": dropped_duplicate,
            "kind": classify_primitive(opcode, norm),
            "terminated_by_ff": terminated,
        })

    return ({
        "core_offset": group_core_pos - rec_start,
        "prefix": prefix,
        "zero": zero,
        "primitive_count": primitive_count,
        "descriptor_hint": descriptor_hint,
        "group_bytes": group_bytes,
        "descriptors": descs,
        "primitives": prims,
        # Backward-compatible aliases:
        "face_count": primitive_count,
        "faces": [
            {
                "offset": p["offset"],
                "raw_indices": p["raw_indices"],
                "polygon_indices_1based_guess": p["indices_1based_guess"],
                "terminated_by_ff": p["terminated_by_ff"],
                "kind": p["kind"],
            }
            for p in prims
        ],
        "end_offset": q - rec_start,
    }, q)


def parse_record(buf: bytes, start: int, end: int, index: int) -> Dict[str, Any]:
    group_count = u16(buf, start + 10)
    header_descs = parse_record_header_descriptors(buf, start, group_count)
    if header_descs is None:
        raise ValueError(f"Invalid record header at 0x{start:x}")

    vertex_count = header_descs[0]["vertex_count"]
    vertex_start = 12 + group_count * 8 + 6
    tail_pos = start + 12 + group_count * 8
    tail6 = {
        "word0": u16(buf, tail_pos),
        "word1_signed": s16(buf, tail_pos + 2),
        "word2_signed": s16(buf, tail_pos + 4),
        "raw_hex": buf[tail_pos:tail_pos + 6].hex(),
    }

    vertices: List[List[int]] = []
    for i in range(vertex_count):
        vo = start + vertex_start + i * 6
        vertices.append([s16(buf, vo), s16(buf, vo + 2), s16(buf, vo + 4)])

    pos = start + vertex_start + vertex_count * 6
    padding_before_first_group = b""
    if not valid_group_core(buf, pos):
        for delta in range(1, 65):
            if valid_group_core(buf, pos + delta):
                padding_before_first_group = buf[pos:pos + delta]
                pos += delta
                break

    groups: List[Dict[str, Any]] = []
    ok = True
    parse_error = None
    try:
        for gi in range(group_count):
            if pos >= end:
                break
            group, pos = parse_primitive_group(buf, start, end, pos, has_prefix=(gi > 0))
            groups.append(group)
    except Exception as exc:
        ok = False
        parse_error = repr(exc)
        groups.append({"parse_error": parse_error, "offset": pos - start})

    invalid_indices: List[Dict[str, int]] = []
    polygon_count = 0
    line_count = 0
    degenerate_count = 0
    total_primitives = 0
    duplicate_prefix_count = 0
    opcodes = Counter()

    for gi, g in enumerate(groups):
        descs = g.get("descriptors", [])
        prims = g.get("primitives", [])
        for pi, p in enumerate(prims):
            total_primitives += 1
            if pi < len(descs):
                opcodes[descs[pi]["opcode"]] += 1
            if p.get("dropped_duplicate_first_index"):
                duplicate_prefix_count += 1
            kind = p.get("kind")
            if kind in ("polygon", "polygon_candidate"):
                polygon_count += 1
            elif kind == "line":
                line_count += 1
            else:
                degenerate_count += 1

            for idx1 in p.get("indices_1based_guess", []):
                if idx1 < 1 or idx1 > vertex_count:
                    invalid_indices.append({"group": gi, "primitive": pi, "index": idx1})

    return {
        "record_index": index,
        "offset": start,
        "end_offset": end,
        "size": end - start,
        "magic_or_flags": u16(buf, start),
        "field_02": u16(buf, start + 2),
        "zeros_04_09": buf[start + 4:start + 10].hex(),
        "group_count": group_count,
        "header_descriptors": header_descs,
        "tail6_after_header_descriptors": tail6,
        "vertex_count": vertex_count,
        "vertex_start": vertex_start,
        "vertex_bytes": vertex_count * 6,
        "first_primitive_group_offset": vertex_start + vertex_count * 6,
        "vertices": vertices,
        "padding_before_first_group_hex": padding_before_first_group.hex(),
        "groups": groups,
        "total_primitives": total_primitives,
        "polygon_count": polygon_count,
        "line_count": line_count,
        "degenerate_count": degenerate_count,
        # Backward-compatible alias:
        "face_total": polygon_count,
        "parsed_group_count": len(groups),
        "primitive_opcodes": {f"0x{k:02x}": v for k, v in sorted(opcodes.items())},
        "duplicate_prefix_primitive_count": duplicate_prefix_count,
        "parse_ok": ok and not invalid_indices and pos <= end,
        "parse_error": parse_error,
        "parse_end_offset": pos - start,
        "invalid_indices": invalid_indices,
    }


def parse_pointer_table(buf: bytes) -> Dict[str, Any]:
    """
    Parse the leading table confirmed by BTECH.EXE.

    The EXE shape-list loader treats the beginning of a loaded shape resource as
    an array of FAR pointers and rebases each nonzero pointer by the allocation
    base until it reaches a 0000:0000 terminator.

    On disk, before rebasing:
        uint16 offset;
        uint16 segment;
        linear_file_offset = segment * 16 + offset;

    This exactly resolves all known record starts in JENPCK/LOCPCK/MARPCK.
    """
    entries: List[Dict[str, int]] = []
    for pos in range(0, len(buf) - 3, 4):
        off = u16(buf, pos)
        seg = u16(buf, pos + 2)
        linear = seg * 16 + off
        entries.append({
            "index": len(entries),
            "table_offset": pos,
            "offset": off,
            "segment": seg,
            "linear": linear,
        })
        if off == 0 and seg == 0:
            break

    nonterm = [e for e in entries if not (e["offset"] == 0 and e["segment"] == 0)]
    linears = [e["linear"] for e in nonterm]
    return {
        "entry_count_including_terminator": len(entries),
        "record_pointer_count": len(nonterm),
        "byte_size_including_terminator": len(entries) * 4,
        "has_terminator": bool(entries and entries[-1]["offset"] == 0 and entries[-1]["segment"] == 0),
        "linear_offsets_monotonic": all(linears[i] < linears[i + 1] for i in range(len(linears) - 1)),
        "first_entries": entries[:16],
        "last_entries": entries[-8:],
        "entries": entries,
    }


def parse_unpacked_model(buf: bytes, *, max_group_count: int = 16, max_vertex_count: int = 128) -> Dict[str, Any]:
    pointer_table = parse_pointer_table(buf)
    ptr_starts = [e["linear"] for e in pointer_table["entries"] if not (e["offset"] == 0 and e["segment"] == 0)]

    # Prefer the EXE-confirmed pointer table when all entries look like records.
    pointer_table_valid = (
        pointer_table["has_terminator"]
        and pointer_table["linear_offsets_monotonic"]
        and len(ptr_starts) > 0
        and all(0 <= p < len(buf) for p in ptr_starts)
        and all(looks_like_record(buf, p, max_group_count=max_group_count, max_vertex_count=max_vertex_count) for p in ptr_starts)
    )

    scanner_starts = scan_records(buf, max_group_count=max_group_count, max_vertex_count=max_vertex_count)
    if pointer_table_valid:
        starts = ptr_starts
        record_source = "exe_confirmed_far_pointer_table"
    else:
        starts = scanner_starts
        record_source = "pattern_scanner_fallback"

    records = []
    for i, s in enumerate(starts):
        e = starts[i + 1] if i + 1 < len(starts) else len(buf)
        records.append(parse_record(buf, s, e, i))

    all_vertices = [v for r in records for v in r["vertices"]]
    if all_vertices:
        ranges = {
            "x": [min(v[0] for v in all_vertices), max(v[0] for v in all_vertices)],
            "y": [min(v[1] for v in all_vertices), max(v[1] for v in all_vertices)],
            "z": [min(v[2] for v in all_vertices), max(v[2] for v in all_vertices)],
        }
    else:
        ranges = {"x": [None, None], "y": [None, None], "z": [None, None]}

    opcode_counter = Counter()
    duplicate_mode = Counter()
    for r in records:
        for k, v in r["primitive_opcodes"].items():
            opcode_counter[k] += v
        duplicate_mode[r["duplicate_prefix_primitive_count"] > 0] += 1

    return {
        "file_size": len(buf),
        "record_count": len(records),
        "record_offsets": starts,
        "record_source": record_source,
        "pointer_table_confirmed_by_exe": pointer_table_valid,
        "pointer_table": pointer_table,
        "scanner_record_offsets": scanner_starts,
        "summary": {
            "records_by_group_count": dict(Counter(r["group_count"] for r in records)),
            "records_by_vertex_count": dict(Counter(r["vertex_count"] for r in records)),
            "records_by_polygon_count": dict(Counter(r["polygon_count"] for r in records)),
            "records_by_line_count": dict(Counter(r["line_count"] for r in records)),
            "total_vertices": sum(r["vertex_count"] for r in records),
            "total_primitive_groups": sum(len(r["groups"]) for r in records),
            "total_primitives": sum(r["total_primitives"] for r in records),
            "total_polygons": sum(r["polygon_count"] for r in records),
            "total_lines": sum(r["line_count"] for r in records),
            "total_degenerate": sum(r["degenerate_count"] for r in records),
            "primitive_opcodes": dict(opcode_counter),
            "records_with_duplicated_first_index_style": duplicate_mode.get(True, 0),
            "records_without_duplicated_first_index_style": duplicate_mode.get(False, 0),
            "all_records_parse_ok": all(r["parse_ok"] for r in records),
            "coordinate_ranges_signed_int16": ranges,
        },
        "records": records,
    }


# ----------------------------------------------------------------------------
# Export helpers
# ----------------------------------------------------------------------------

def iter_primitives(record: Dict[str, Any]) -> Iterable[Tuple[str, int, int, List[int], Dict[str, Any]]]:
    """Yield (kind, group_index, primitive_index, zero_based_indices, descriptor)."""
    vcount = record["vertex_count"]
    for gi, group in enumerate(record.get("groups", [])):
        descs = group.get("descriptors", [])
        for pi, prim in enumerate(group.get("primitives", [])):
            idxs0 = [i - 1 for i in prim.get("indices_1based_guess", []) if 1 <= i <= vcount]
            if not idxs0:
                continue
            desc = descs[pi] if pi < len(descs) else {}
            yield prim.get("kind", "unknown"), gi, pi, idxs0, desc


def transform_vertex(v: Sequence[float], scale: float, swizzle: str) -> Tuple[float, float, float]:
    x, y, z = (float(v[0]) * scale, float(v[1]) * scale, float(v[2]) * scale)
    mapping = {
        "xyz": (x, y, z),
        "xzy": (x, z, y),
        "yxz": (y, x, z),
        "yzx": (y, z, x),
        "zxy": (z, x, y),
        "zyx": (z, y, x),
    }
    return mapping[swizzle]


def write_mtl_file(path: Path, group_count: int) -> None:
    lines = ["# Simple grayscale materials generated by mw1989_tbl_to_mesh_v3.py"]
    for gi in range(max(1, group_count)):
        shade = 0.35 + 0.5 * (gi / max(1, group_count - 1))
        lines += [
            f"newmtl group_{gi:02d}",
            f"Kd {shade:.3f} {shade:.3f} {shade:.3f}",
            "Ka 0.000 0.000 0.000",
            "Ks 0.000 0.000 0.000",
            "d 1.0",
            "",
        ]
    path.write_text("\n".join(lines), encoding="utf-8")


def write_obj_record(path: Path, record: Dict[str, Any], scale: float, swizzle: str, write_mtl: bool = True) -> None:
    lines = [
        "# Experimental MechWarrior 1989/Dynamix candidate mesh export v3",
        f"# record_index={record['record_index']} offset=0x{record['offset']:04x}",
        f"# vertices={record['vertex_count']} polygons={record['polygon_count']} lines={record['line_count']} groups={record['group_count']}",
        "# WARNING: preview geometry; runtime transforms/materials are unresolved.",
    ]
    if write_mtl:
        lines.append(f"mtllib {path.stem}.mtl")
    lines.append(f"o record_{record['record_index']:03d}_off_{record['offset']:04x}")

    for v in record["vertices"]:
        x, y, z = transform_vertex(v, scale, swizzle)
        lines.append(f"v {x:.9g} {y:.9g} {z:.9g}")

    last_group = None
    for kind, gi, pi, idxs0, _desc in iter_primitives(record):
        if gi != last_group:
            lines.append(f"g record_{record['record_index']:03d}_group_{gi:02d}")
            if write_mtl:
                lines.append(f"usemtl group_{gi:02d}")
            last_group = gi
        idxs1 = [i + 1 for i in idxs0]
        if kind.startswith("polygon") and len(idxs1) >= 3:
            lines.append("f " + " ".join(map(str, idxs1)))
        elif kind == "line" and len(idxs1) >= 2:
            lines.append("l " + " ".join(map(str, idxs1)))

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    if write_mtl:
        write_mtl_file(path.with_suffix(".mtl"), int(record["group_count"]))


def write_obj_merged_grid(path: Path, records: Sequence[Dict[str, Any]], scale: float, swizzle: str, grid_spacing: float, write_mtl: bool = True) -> None:
    lines = [
        "# Experimental merged grid OBJ preview v2.",
        "# Records are translated apart for inspection; this is NOT the in-game assembled mech.",
    ]
    if write_mtl:
        lines.append(f"mtllib {path.stem}.mtl")

    base = 0
    cols = max(1, math.ceil(math.sqrt(len(records))))
    max_groups = 1
    for n, record in enumerate(records):
        row = n // cols
        col = n % cols
        dx = col * grid_spacing
        dz = row * grid_spacing
        max_groups = max(max_groups, int(record["group_count"]))

        lines.append(f"o record_{record['record_index']:03d}_off_{record['offset']:04x}")
        for v in record["vertices"]:
            x, y, z = transform_vertex(v, scale, swizzle)
            lines.append(f"v {x + dx:.9g} {y:.9g} {z + dz:.9g}")

        last_group = None
        for kind, gi, _pi, idxs0, _desc in iter_primitives(record):
            if gi != last_group:
                lines.append(f"g record_{record['record_index']:03d}_group_{gi:02d}")
                if write_mtl:
                    lines.append(f"usemtl group_{gi:02d}")
                last_group = gi
            idxs1 = [base + i + 1 for i in idxs0]
            if kind.startswith("polygon") and len(idxs1) >= 3:
                lines.append("f " + " ".join(map(str, idxs1)))
            elif kind == "line" and len(idxs1) >= 2:
                lines.append("l " + " ".join(map(str, idxs1)))

        base += record["vertex_count"]

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    if write_mtl:
        write_mtl_file(path.with_suffix(".mtl"), max_groups)


def write_ply_record(path: Path, record: Dict[str, Any], scale: float, swizzle: str) -> None:
    vertices = [transform_vertex(v, scale, swizzle) for v in record["vertices"]]
    faces = []
    edges = []
    for kind, _gi, _pi, idxs0, _desc in iter_primitives(record):
        if kind.startswith("polygon") and len(idxs0) >= 3:
            faces.append(idxs0)
        elif kind == "line" and len(idxs0) >= 2:
            edges.append(idxs0[:2])

    lines = [
        "ply",
        "format ascii 1.0",
        "comment Experimental candidate mesh export from MechWarrior 1989/Dynamix resource v2",
        f"element vertex {len(vertices)}",
        "property float x",
        "property float y",
        "property float z",
        f"element face {len(faces)}",
        "property list uchar int vertex_indices",
        f"element edge {len(edges)}",
        "property int vertex1",
        "property int vertex2",
        "end_header",
    ]
    for x, y, z in vertices:
        lines.append(f"{x:.9g} {y:.9g} {z:.9g}")
    for f in faces:
        lines.append(f"{len(f)} " + " ".join(map(str, f)))
    for e in edges:
        lines.append(f"{e[0]} {e[1]}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_step_record(path: Path, record: Dict[str, Any], scale: float, swizzle: str) -> None:
    """
    Experimental STEP faceted mesh writer. OBJ remains the reliable format.
    Line primitives are skipped because STEP B-Rep faces cannot represent them
    directly here.
    """
    vertices = [transform_vertex(v, scale, swizzle) for v in record["vertices"]]
    faces = [
        idxs0 for kind, _gi, _pi, idxs0, _desc in iter_primitives(record)
        if kind.startswith("polygon") and len(idxs0) >= 3
    ]

    lines = [
        "ISO-10303-21;",
        "HEADER;",
        "FILE_DESCRIPTION(('Experimental faceted mesh export from MechWarrior 1989 candidate model v2'),'2;1');",
        f"FILE_NAME('{path.name}','2026-01-01T00:00:00',('mw1989_tbl_to_mesh_v3.py'),(''), 'mw1989_tbl_to_mesh_v3.py','reverse-engineering','');",
        "FILE_SCHEMA(('CONFIG_CONTROL_DESIGN'));",
        "ENDSEC;",
        "DATA;",
    ]

    eid = 1
    point_ids = []
    for x, y, z in vertices:
        point_ids.append(eid)
        lines.append(f"#{eid}=CARTESIAN_POINT('',({x:.9g},{y:.9g},{z:.9g}));")
        eid += 1

    face_ids = []
    for idxs in faces:
        loop_id = eid
        pts = ",".join(f"#{point_ids[i]}" for i in idxs)
        lines.append(f"#{loop_id}=POLY_LOOP('',({pts}));")
        eid += 1
        bound_id = eid
        lines.append(f"#{bound_id}=FACE_OUTER_BOUND('',#{loop_id},.T.);")
        eid += 1
        face_id = eid
        lines.append(f"#{face_id}=FACE('',(#{bound_id}));")
        eid += 1
        face_ids.append(face_id)

    shell_id = eid
    lines.append(f"#{shell_id}=CLOSED_SHELL('',({','.join(f'#{i}' for i in face_ids)}));")
    eid += 1
    lines.append(f"#{eid}=FACETED_BREP('record_{record['record_index']:03d}',#{shell_id});")
    lines.append("ENDSEC;")
    lines.append("END-ISO-10303-21;")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


# ----------------------------------------------------------------------------
# Optional rendering
# ----------------------------------------------------------------------------

def render_record_triview(record: Dict[str, Any], out_path: Path, title_prefix: str = "", scale: float = 1.0) -> None:
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from matplotlib.patches import Polygon
    except Exception as exc:
        raise RuntimeError("Matplotlib is required for --render") from exc

    def draw_view(ax: Any, axes: Tuple[int, int], depth_axis: int, title: str) -> None:
        verts = record["vertices"]
        polys = []
        lines = []
        used = set()
        for kind, _gi, _pi, idxs0, desc in iter_primitives(record):
            used.update(idxs0)
            if kind.startswith("polygon") and len(idxs0) >= 3:
                depth = sum(verts[i][depth_axis] for i in idxs0) / len(idxs0)
                shade_bytes = desc.get("shade_or_mask", [])
                shade = 0.60
                if shade_bytes:
                    shade = 0.35 + (sum(shade_bytes) % 130) / 255.0
                polys.append((depth, idxs0, shade))
            elif kind == "line" and len(idxs0) >= 2:
                lines.append(idxs0)

        for _depth, idxs0, shade in sorted(polys, key=lambda x: x[0]):
            pts = [(verts[i][axes[0]] * scale, verts[i][axes[1]] * scale) for i in idxs0]
            ax.add_patch(Polygon(pts, closed=True, facecolor=str(max(0.25, min(0.9, shade))), edgecolor="black", linewidth=0.85))

        for idxs0 in lines:
            pts = [(verts[i][axes[0]] * scale, verts[i][axes[1]] * scale) for i in idxs0]
            ax.plot([p[0] for p in pts], [p[1] for p in pts], color="black", linewidth=1.1)

        xs = [verts[i][axes[0]] * scale for i in used]
        ys = [verts[i][axes[1]] * scale for i in used]
        if xs and ys:
            minx, maxx = min(xs), max(xs)
            miny, maxy = min(ys), max(ys)
            span = max(maxx - minx, maxy - miny)
            pad = span * 0.18 + 1
            ax.set_xlim(minx - pad, maxx + pad)
            ax.set_ylim(miny - pad, maxy + pad)

        ax.set_aspect("equal", "box")
        ax.axis("off")
        ax.set_title(title)

    fig, axes = plt.subplots(1, 3, figsize=(13, 4.8), facecolor="white")
    fig.suptitle(
        f"{title_prefix} record {record['record_index']:03d} "
        f"off=0x{record['offset']:04x}, g={record['group_count']}, "
        f"v={record['vertex_count']}, poly={record['polygon_count']}, line={record['line_count']}",
        fontsize=13,
    )
    draw_view(axes[0], (0, 1), 2, "Front: X/Y")
    draw_view(axes[1], (2, 1), 0, "Side: Z/Y")
    draw_view(axes[2], (0, 2), 1, "Top: X/Z")
    plt.tight_layout(rect=[0, 0, 1, 0.90])
    fig.savefig(out_path, dpi=220)
    plt.close(fig)


def render_contact_sheet(records: Sequence[Dict[str, Any]], out_path: Path, title_prefix: str = "", scale: float = 1.0) -> None:
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from matplotlib.patches import Polygon
    except Exception as exc:
        raise RuntimeError("Matplotlib is required for --render") from exc

    rows = len(records)
    fig, axes = plt.subplots(rows, 3, figsize=(10.5, max(1, rows) * 1.55), facecolor="white")
    if rows == 1:
        axes = [axes]  # type: ignore

    for row, record in enumerate(records):
        for col, (axes_pair, depth_axis, label) in enumerate([((0, 1), 2, "Front"), ((2, 1), 0, "Side"), ((0, 2), 1, "Top")]):
            ax = axes[row][col] if rows > 1 else axes[0][col]  # type: ignore
            # Minimal inline drawing to avoid recursive figure creation.
            verts = record["vertices"]
            polys = []
            lines = []
            used = set()
            for kind, _gi, _pi, idxs0, desc in iter_primitives(record):
                used.update(idxs0)
                if kind.startswith("polygon") and len(idxs0) >= 3:
                    depth = sum(verts[i][depth_axis] for i in idxs0) / len(idxs0)
                    polys.append((depth, idxs0))
                elif kind == "line" and len(idxs0) >= 2:
                    lines.append(idxs0)
            for _depth, idxs0 in sorted(polys, key=lambda x: x[0]):
                pts = [(verts[i][axes_pair[0]] * scale, verts[i][axes_pair[1]] * scale) for i in idxs0]
                ax.add_patch(Polygon(pts, closed=True, facecolor="0.65", edgecolor="black", linewidth=0.45))
            for idxs0 in lines:
                pts = [(verts[i][axes_pair[0]] * scale, verts[i][axes_pair[1]] * scale) for i in idxs0]
                ax.plot([p[0] for p in pts], [p[1] for p in pts], color="black", linewidth=0.55)
            xs = [verts[i][axes_pair[0]] * scale for i in used]
            ys = [verts[i][axes_pair[1]] * scale for i in used]
            if xs and ys:
                minx, maxx = min(xs), max(xs)
                miny, maxy = min(ys), max(ys)
                span = max(maxx - minx, maxy - miny)
                pad = span * 0.18 + 1
                ax.set_xlim(minx - pad, maxx + pad)
                ax.set_ylim(miny - pad, maxy + pad)
            ax.set_aspect("equal", "box")
            ax.axis("off")
            ax.set_title(f"{record['record_index']:02d} {label}", fontsize=7)

    fig.suptitle(f"{title_prefix} candidate records tri-view contact sheet", fontsize=11)
    plt.tight_layout(pad=0.35)
    fig.savefig(out_path, dpi=180)
    plt.close(fig)


# ----------------------------------------------------------------------------
# CLI
# ----------------------------------------------------------------------------

def parse_record_selection(selection: str, max_count: int) -> List[int]:
    if selection.strip().lower() == "all":
        return list(range(max_count))
    result = set()
    for part in selection.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            start, end = int(a), int(b)
            for i in range(start, end + 1):
                if 0 <= i < max_count:
                    result.add(i)
        else:
            i = int(part)
            if 0 <= i < max_count:
                result.add(i)
    return sorted(result)


def main(argv: Optional[Sequence[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="Unpack and export MechWarrior 1989/Dynamix PCK/TBL candidate 3D records, parser v3.")
    ap.add_argument("input", type=Path)
    ap.add_argument("-o", "--out-dir", type=Path, default=Path("mw1989_v3_export"))
    ap.add_argument("--formats", default="obj,json", help="Comma-separated: obj, ply, step, json")
    ap.add_argument("--records", default="all", help="Record selection: all, 4,38, 22-45")
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--swizzle", default="xyz", choices=["xyz", "xzy", "yxz", "yzx", "zxy", "zyx"])
    ap.add_argument("--save-unpacked", action="store_true")
    ap.add_argument("--merged-grid", action="store_true")
    ap.add_argument("--grid-spacing", type=float, default=5000.0)
    ap.add_argument("--no-mtl", action="store_true")
    ap.add_argument("--render", action="store_true", help="Also render tri-view PNGs using matplotlib")
    ap.add_argument("--render-contact-sheet", action="store_true", help="Render one contact sheet for selected records")
    ap.add_argument("--max-group-count", type=int, default=16)
    ap.add_argument("--max-vertex-count", type=int, default=128)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    src = args.input.read_bytes()
    unpacked, unpack_report = unpack_dynamix_block(src)
    parsed = parse_unpacked_model(unpacked, max_group_count=args.max_group_count, max_vertex_count=args.max_vertex_count)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    stem = args.input.stem

    if args.save_unpacked:
        (args.out_dir / f"{stem}.unpacked.bin").write_bytes(unpacked)

    formats = {f.strip().lower() for f in args.formats.split(",") if f.strip()}
    selected_indices = parse_record_selection(args.records, parsed["record_count"])
    selected = [parsed["records"][i] for i in selected_indices]

    report = {
        "input": str(args.input),
        "unpack_report": unpack_report,
        "parse_summary": {k: v for k, v in parsed.items() if k != "records"},
        "selected_records": selected_indices,
        "warning": "Experimental reverse-engineered preview format. Runtime transforms/materials/draw order are unresolved.",
    }

    if "json" in formats:
        (args.out_dir / f"{stem}.summary.v3.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
        (args.out_dir / f"{stem}.records.v3.json").write_text(json.dumps(parsed, indent=2), encoding="utf-8")

    if "obj" in formats:
        obj_dir = args.out_dir / "obj"
        obj_dir.mkdir(exist_ok=True)
        for record in selected:
            name = f"{stem}_record_{record['record_index']:03d}_off_{record['offset']:04x}_g{record['group_count']}_v{record['vertex_count']}_p{record['polygon_count']}_l{record['line_count']}.obj"
            write_obj_record(obj_dir / name, record, args.scale, args.swizzle, write_mtl=(not args.no_mtl))
        if args.merged_grid:
            write_obj_merged_grid(
                obj_dir / f"{stem}_selected_records_grid_v2.obj",
                selected,
                args.scale,
                args.swizzle,
                args.grid_spacing * args.scale,
                write_mtl=(not args.no_mtl),
            )

    if "ply" in formats:
        ply_dir = args.out_dir / "ply"
        ply_dir.mkdir(exist_ok=True)
        for record in selected:
            name = f"{stem}_record_{record['record_index']:03d}_off_{record['offset']:04x}_g{record['group_count']}_v{record['vertex_count']}_p{record['polygon_count']}_l{record['line_count']}.ply"
            write_ply_record(ply_dir / name, record, args.scale, args.swizzle)

    if "step" in formats or "stp" in formats:
        step_dir = args.out_dir / "step_experimental"
        step_dir.mkdir(exist_ok=True)
        for record in selected:
            name = f"{stem}_record_{record['record_index']:03d}_off_{record['offset']:04x}_g{record['group_count']}_v{record['vertex_count']}_p{record['polygon_count']}_l{record['line_count']}.step"
            write_step_record(step_dir / name, record, args.scale, args.swizzle)

    if args.render:
        render_dir = args.out_dir / "renders"
        render_dir.mkdir(exist_ok=True)
        for record in selected:
            name = f"{stem}_record_{record['record_index']:03d}_triview.png"
            render_record_triview(record, render_dir / name, title_prefix=stem, scale=args.scale)
        if args.render_contact_sheet:
            render_contact_sheet(selected, render_dir / f"{stem}_selected_contact_sheet.png", title_prefix=stem, scale=args.scale)

    if args.verbose:
        print("Input:", args.input)
        print("Unpack:", json.dumps(unpack_report, indent=2))
        print("Parse summary:", json.dumps(report["parse_summary"]["summary"], indent=2))
        print("Records found:", parsed["record_count"])
        print("Selected:", selected_indices)
        print("Output:", args.out_dir)

    print(f"Done. Found {parsed['record_count']} candidate records; exported {len(selected)} selected records to {args.out_dir}")
    if "step" in formats or "stp" in formats:
        print("Note: STEP export is experimental; OBJ/PLY are the reliable mesh exports.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
