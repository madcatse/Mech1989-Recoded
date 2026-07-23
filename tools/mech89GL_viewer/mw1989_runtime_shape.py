#!/usr/bin/env python3
"""
Runtime-oriented parser for MechWarrior 1989 JENPCK/LOCPCK/MARPCK resources.

This module follows the pointer chain used by BTECH.EXE draw_shape_record more
closely than the legacy preview parser:

  record pointer table entry -> record segment:offset
  record+0x0c -> near pointer to part descriptors in the record segment
  part+0x02 -> shared vertex table near pointer
  part+0x06 -> variant table near pointer
  variant entry -> command list pointer
  type-0 command -> primitive descriptor table and FF-terminated index lists

The legacy mesh parser is kept elsewhere because its 4-byte shifted vertex view
is still a useful visual preview. This runtime parser is the workbench for a
byte-compatible renderer.
"""
from __future__ import annotations

from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple
import argparse
import json
import struct

import mw1989_tbl_to_mesh_v3 as legacy

Vec3i = Tuple[int, int, int]


def u16(buf: bytes, off: int) -> int:
    return struct.unpack_from("<H", buf, off)[0]


def s16(buf: bytes, off: int) -> int:
    return struct.unpack_from("<h", buf, off)[0]


@dataclass
class RuntimePrimitive:
    part_index: int
    command_index: int
    primitive_index: int
    opcode: int
    shade_bytes: Tuple[int, int, int, int]
    material_or_flags: int
    index_list_offset: int
    raw_indices: List[int]
    normalized_indices: List[int]
    kind: str


@dataclass
class RuntimeCommand:
    part_index: int
    variant_index: int
    command_index: int
    offset: int
    distance_threshold: int
    command_type: int
    raw_hex: str
    primitive_count: int = 0
    primitive_descriptor_ptr: int = 0
    group_word: int = 0
    primitives: List[RuntimePrimitive] = None  # type: ignore[assignment]


@dataclass
class RuntimePart:
    index: int
    descriptor_offset: int
    start_vertex: int
    vertex_count: int
    vertex_table_ptr: int
    variant_count: int
    variant_table_ptr: int
    sort_vertex: Vec3i
    commands: List[RuntimeCommand]


@dataclass
class RuntimeRecord:
    resource_name: str
    record_index: int
    record_offset: int
    record_segment: int
    segment_base: int
    flags: int
    scale_shift: int
    extent_or_radius: int
    part_count: int
    part_descriptor_ptr: int
    vertices: List[Vec3i]
    parts: List[RuntimePart]

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)


def _classify(opcode: int, idxs: List[int]) -> str:
    if opcode == 0x80 or len(idxs) == 2:
        return "line"
    if opcode == 0x81 and len(idxs) >= 3:
        return "polygon"
    if len(idxs) >= 3:
        return "polygon_candidate"
    return "degenerate"


def _normalize_indices(raw: List[int]) -> List[int]:
    # JEN/MAR records often duplicate the first index as N,N,... . The draw code
    # uses the list as a closed polygon seed; for mesh display we drop the dup.
    if len(raw) >= 2 and raw[0] == raw[1]:
        return raw[1:]
    return raw


def _parse_type0_primitives(buf: bytes, segbase: int, cmd_abs: int, part_index: int, variant_index: int, command_index: int) -> List[RuntimePrimitive]:
    primitive_count = u16(buf, cmd_abs + 2)
    desc_ptr = u16(buf, cmd_abs + 4)
    desc_abs = segbase + desc_ptr
    q = desc_abs + primitive_count * 8
    out: List[RuntimePrimitive] = []
    for pi in range(primitive_count):
        d = desc_abs + pi * 8
        opcode = buf[d]
        shade = tuple(buf[d + 1:d + 5])  # type: ignore[assignment]
        mat = buf[d + 5]
        list_ptr = u16(buf, d + 6)
        raw: List[int] = []
        # Next reconstruction step: follow the explicit per-primitive list
        # pointer, not merely the sequential stream position. In the current
        # PCKs both usually match, but the EXE stores this pointer and the
        # exact renderer should respect it.
        start = segbase + list_ptr
        q2 = start
        while q2 < len(buf) and buf[q2] != 0xFF:
            raw.append(buf[q2])
            q2 += 1
        norm = _normalize_indices(raw)
        out.append(RuntimePrimitive(
            part_index=part_index,
            command_index=command_index,
            primitive_index=pi,
            opcode=opcode,
            shade_bytes=shade,
            material_or_flags=mat,
            index_list_offset=list_ptr,
            raw_indices=raw,
            normalized_indices=norm,
            kind=_classify(opcode, norm),
        ))
    return out


def parse_runtime_records(path: Path) -> List[RuntimeRecord]:
    src = path.read_bytes()
    buf, _report = legacy.unpack_dynamix_block(src)
    pt = legacy.parse_pointer_table(buf)
    records: List[RuntimeRecord] = []
    for entry in pt["entries"]:
        if entry.get("offset", 0) == 0 and entry.get("segment", 0) == 0:
            continue
        rec_abs = int(entry["linear"])
        rec_seg = int(entry["segment"])
        segbase = rec_seg * 16
        flags = buf[rec_abs]
        scale_shift = buf[rec_abs + 1]
        extent = u16(buf, rec_abs + 2)
        part_count = u16(buf, rec_abs + 10)
        part_desc_ptr = u16(buf, rec_abs + 12)
        part_desc_abs = segbase + part_desc_ptr

        # Vertex table is shared by all observed PCK parts. Read it from the
        # first descriptor, as draw_shape_record does via descriptor+2.
        first_vcnt = buf[part_desc_abs + 1]
        first_vptr = u16(buf, part_desc_abs + 2)
        vertex_abs = segbase + first_vptr
        vertices = [
            (s16(buf, vertex_abs + i * 6), s16(buf, vertex_abs + i * 6 + 2), s16(buf, vertex_abs + i * 6 + 4))
            for i in range(first_vcnt)
        ]

        parts: List[RuntimePart] = []
        for pi in range(part_count):
            d = part_desc_abs + pi * 8
            start_vertex = buf[d]
            vcnt = buf[d + 1]
            vptr = u16(buf, d + 2)
            variant_count = u16(buf, d + 4)
            variant_table_ptr = u16(buf, d + 6)
            sort_vertex = vertices[start_vertex] if 0 <= start_vertex < len(vertices) else (0, 0, 0)
            commands: List[RuntimeCommand] = []
            vt_abs = segbase + variant_table_ptr
            for vi in range(variant_count):
                cmd_count = u16(buf, vt_abs + vi * 4)
                cmd_list_ptr = u16(buf, vt_abs + vi * 4 + 2)
                cmd_abs = segbase + cmd_list_ptr
                for ci in range(cmd_count):
                    c = cmd_abs + ci * 8
                    ctype = buf[c + 1]
                    cmd = RuntimeCommand(
                        part_index=pi,
                        variant_index=vi,
                        command_index=ci,
                        offset=c - segbase,
                        distance_threshold=buf[c],
                        command_type=ctype,
                        raw_hex=buf[c:c + 8].hex(),
                        primitives=[],
                    )
                    if ctype == 0:
                        cmd.primitive_count = u16(buf, c + 2)
                        cmd.primitive_descriptor_ptr = u16(buf, c + 4)
                        cmd.group_word = s16(buf, c + 6)
                        cmd.primitives = _parse_type0_primitives(buf, segbase, c, pi, vi, ci)
                    commands.append(cmd)
            parts.append(RuntimePart(
                index=pi,
                descriptor_offset=d - segbase,
                start_vertex=start_vertex,
                vertex_count=vcnt,
                vertex_table_ptr=vptr,
                variant_count=variant_count,
                variant_table_ptr=variant_table_ptr,
                sort_vertex=sort_vertex,
                commands=commands,
            ))
        records.append(RuntimeRecord(
            resource_name=path.stem,
            record_index=int(entry["index"]),
            record_offset=int(entry["offset"]),
            record_segment=rec_seg,
            segment_base=segbase,
            flags=flags,
            scale_shift=scale_shift,
            extent_or_radius=extent,
            part_count=part_count,
            part_descriptor_ptr=part_desc_ptr,
            vertices=vertices,
            parts=parts,
        ))
    return records


def iter_runtime_primitives(record: RuntimeRecord, *, index_mode: str = "raw") -> Iterable[Tuple[RuntimePrimitive, List[int]]]:
    """Yield primitives with zero-based vertex indices.

    index_mode:
      - "raw": interpret byte N as vertex N, matching a literal vertex table.
      - "minus1": interpret byte N as vertex N-1, matching legacy OBJ export.
    """
    for part in record.parts:
        for cmd in part.commands:
            for prim in cmd.primitives or []:
                if index_mode == "minus1":
                    idxs = [i - 1 for i in prim.normalized_indices]
                else:
                    idxs = list(prim.normalized_indices)
                yield prim, [i for i in idxs if 0 <= i < len(record.vertices)]


def main() -> int:
    ap = argparse.ArgumentParser(description="Decode MW1989 PCK/TBL records using the runtime pointer chain.")
    ap.add_argument("tbl_path", type=Path)
    ap.add_argument("--record", type=int, default=None)
    ap.add_argument("-o", "--output", type=Path, default=None)
    args = ap.parse_args()
    records = parse_runtime_records(args.tbl_path)
    if args.record is not None:
        data: Any = records[args.record].to_dict()
    else:
        data = {
            "path": str(args.tbl_path),
            "record_count": len(records),
            "records": [r.to_dict() for r in records],
        }
    text = json.dumps(data, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(text, encoding="utf-8")
        print(f"wrote {args.output}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
