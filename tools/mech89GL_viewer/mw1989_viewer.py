#!/usr/bin/env python3
"""
mw1989_viewer.py

GPU-rendered model viewer for reverse-engineered MechWarrior (1989)
Dynamix/Sierra *PCK.TBL resources. It uses the companion
mw1989_tbl_to_mesh_v3.py parser to unpack/parse records, then renders the
extracted polygon/line preview geometry through an OpenGL VBO/EBO viewport.

This is intentionally a preview/inspection engine, not a byte-perfect clone of
MechWarrior's runtime renderer. Known limitations are listed in README.md.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import traceback
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

try:
    import tkinter as tk
    from tkinter import filedialog, messagebox
except Exception as exc:  # pragma: no cover - depends on local Python build
    raise SystemExit("Tkinter is required. On Linux install python3-tk, then rerun.") from exc

# The package ships with a copy of the parser/converter from the research step.
import mw1989_tbl_to_mesh_v3 as tbl
import mw1989_runtime_shape as runtime_shape
from mw1989_gpu import GpuRenderer, OpenGLViewport

Vec3 = Tuple[float, float, float]


@dataclass
class Primitive:
    kind: str                  # "polygon" or "line"
    indices: List[int]         # zero-based vertex indices
    group_index: int
    primitive_index: int       # stable draw-order number inside part/group
    opcode: int = 0
    shade_level: Optional[float] = None
    material_or_flags: int = 0


@dataclass
class LoadedRecord:
    resource_name: str
    path: Path
    record_index: int
    raw: Dict
    vertices: List[Vec3]
    primitives: List[Primitive]
    group_sort_vertices: Dict[int, int]
    bbox_min: Vec3
    bbox_max: Vec3
    center: Vec3
    radius: float

    @property
    def label(self) -> str:
        custom = self.raw.get("display_label")
        if custom:
            return str(custom)
        return (
            f"{self.resource_name}  #{self.record_index:03d}   "
            f"v={len(self.vertices)}  poly={self.raw.get('polygon_count', 0)}  "
            f"line={self.raw.get('line_count', 0)}  parts={self.raw.get('group_count', 0)}"
        )


@dataclass
class LoadedResource:
    path: Path
    unpack_report: Dict
    parsed: Dict
    records: List[LoadedRecord]


@dataclass
class AnimationFrame:
    frame_index: int
    record_indices: List[int]
    duration_ms: int = 180


class ResourceLoadError(RuntimeError):
    pass


def _bbox(vertices: Sequence[Vec3]) -> Tuple[Vec3, Vec3, Vec3, float]:
    if not vertices:
        zero = (0.0, 0.0, 0.0)
        return zero, zero, zero, 1.0
    xs = [v[0] for v in vertices]
    ys = [v[1] for v in vertices]
    zs = [v[2] for v in vertices]
    mn = (min(xs), min(ys), min(zs))
    mx = (max(xs), max(ys), max(zs))
    center = ((mn[0] + mx[0]) * 0.5, (mn[1] + mx[1]) * 0.5, (mn[2] + mx[2]) * 0.5)
    radius = max(
        math.sqrt((x - center[0]) ** 2 + (y - center[1]) ** 2 + (z - center[2]) ** 2)
        for x, y, z in vertices
    )
    return mn, mx, center, max(radius, 1.0)


def build_loaded_record(path: Path, stem: str, raw_record: Dict, *, scale: float, swizzle: str) -> LoadedRecord:
    vertices = [tbl.transform_vertex(v, scale, swizzle) for v in raw_record.get("vertices", [])]
    primitives: List[Primitive] = []
    for kind, group_index, primitive_index, indices, desc in tbl.iter_primitives(raw_record):
        if kind.startswith("polygon") and len(indices) >= 3:
            p_kind = "polygon"
        elif kind == "line" and len(indices) >= 2:
            p_kind = "line"
        else:
            continue
        shade = desc.get("shade_or_mask")
        if isinstance(shade, list) and shade:
            shade_level = sum(int(x) for x in shade[:4]) / min(len(shade), 4)
        else:
            shade_level = None
        primitives.append(
            Primitive(
                kind=p_kind,
                indices=list(indices),
                group_index=group_index,
                primitive_index=primitive_index,
                opcode=int(desc.get("opcode", 0) or 0),
                shade_level=shade_level,
                material_or_flags=int(desc.get("byte_5", 0) or 0),
            )
        )
    group_sort_vertices: Dict[int, int] = {}
    for gi, desc in enumerate(raw_record.get("header_descriptors", []) or []):
        vi = int(desc.get("start_vertex_hint", 0) or 0)
        if 0 <= vi < len(vertices):
            group_sort_vertices[gi] = vi
    mn, mx, center, radius = _bbox(vertices)
    return LoadedRecord(
        resource_name=stem,
        path=path,
        record_index=int(raw_record.get("record_index", 0)),
        raw=raw_record,
        vertices=vertices,
        primitives=primitives,
        group_sort_vertices=group_sort_vertices,
        bbox_min=mn,
        bbox_max=mx,
        center=center,
        radius=radius,
    )



def build_loaded_record_runtime(path: Path, stem: str, runtime_record: runtime_shape.RuntimeRecord, *, scale: float, swizzle: str, index_mode: str) -> LoadedRecord:
    vertices = [tbl.transform_vertex(list(v), scale, swizzle) for v in runtime_record.vertices]
    primitives: List[Primitive] = []
    poly_count = 0
    line_count = 0
    for prim, indices in runtime_shape.iter_runtime_primitives(runtime_record, index_mode=index_mode):
        if prim.kind.startswith("polygon") and len(indices) >= 3:
            p_kind = "polygon"
            poly_count += 1
        elif prim.kind == "line" and len(indices) >= 2:
            p_kind = "line"
            line_count += 1
        else:
            continue
        shade_level = sum(int(x) for x in prim.shade_bytes[:4]) / 4.0 if prim.shade_bytes else None
        primitives.append(Primitive(
            kind=p_kind,
            indices=list(indices),
            group_index=prim.part_index,
            primitive_index=prim.command_index * 1000 + prim.primitive_index,
            opcode=prim.opcode,
            shade_level=shade_level,
            material_or_flags=prim.material_or_flags,
        ))
    group_sort_vertices = {part.index: part.start_vertex for part in runtime_record.parts if 0 <= part.start_vertex < len(vertices)}
    mn, mx, center, radius = _bbox(vertices)
    raw = {
        "runtime_mode": True,
        "offset": runtime_record.record_segment * 16 + runtime_record.record_offset,
        "record_index": runtime_record.record_index,
        "group_count": runtime_record.part_count,
        "polygon_count": poly_count,
        "line_count": line_count,
        "size": 0,
    }
    return LoadedRecord(
        resource_name=stem + ":runtime",
        path=path,
        record_index=runtime_record.record_index,
        raw=raw,
        vertices=vertices,
        primitives=primitives,
        group_sort_vertices=group_sort_vertices,
        bbox_min=mn,
        bbox_max=mx,
        center=center,
        radius=radius,
    )

def load_resource(path: Path, *, scale: float = 1.0, swizzle: str = "xzy", runtime: bool = False, runtime_index_mode: str = "raw") -> LoadedResource:
    try:
        if runtime:
            runtime_records = runtime_shape.parse_runtime_records(path)
            records = [build_loaded_record_runtime(path, path.stem, rec, scale=scale, swizzle=swizzle, index_mode=runtime_index_mode)
                       for rec in runtime_records]
            parsed = {
                "summary": {
                    "total_polygons": sum(r.raw.get("polygon_count", 0) for r in records),
                    "total_lines": sum(r.raw.get("line_count", 0) for r in records),
                    "mode": "runtime",
                }
            }
            if not records:
                raise ResourceLoadError(f"No runtime records found in {path}")
            return LoadedResource(path=path, unpack_report={}, parsed=parsed, records=records)

        src = path.read_bytes()
        unpacked, unpack_report = tbl.unpack_dynamix_block(src)
        parsed = tbl.parse_unpacked_model(unpacked)
        records = [build_loaded_record(path, path.stem, rec, scale=scale, swizzle=swizzle)
                   for rec in parsed.get("records", [])]
        if not records:
            raise ResourceLoadError(f"No model records found in {path}")
        return LoadedResource(path=path, unpack_report=unpack_report, parsed=parsed, records=records)
    except Exception as exc:
        raise ResourceLoadError(f"Failed to load {path}: {exc}") from exc


FrameSig = Tuple[int, int, int]
FrameRun = Tuple[int, int, FrameSig]


def frame_signature(rec: LoadedRecord) -> FrameSig:
    return (
        int(rec.raw.get("group_count", 0) or 0),
        int(rec.raw.get("polygon_count", 0) or 0),
        int(rec.raw.get("line_count", 0) or 0),
    )


def find_frame_runs(records: Sequence[LoadedRecord], *, min_length: int = 2) -> List[FrameRun]:
    runs: List[FrameRun] = []
    if not records:
        return runs
    start = 0
    prev = frame_signature(records[0])
    for i in range(1, len(records) + 1):
        sig = frame_signature(records[i]) if i < len(records) else None
        if sig != prev:
            if i - start >= min_length:
                runs.append((start, i - 1, prev))
            start = i
            if sig is not None:
                prev = sig
    return runs


def _format_record_range(records: Sequence[LoadedRecord]) -> str:
    if not records:
        return ""
    if len(records) == 1:
        return f"#{records[0].record_index:03d}"
    return f"#{records[0].record_index:03d}..#{records[-1].record_index:03d}"


def build_composite_record(
    source_records: Sequence[LoadedRecord],
    *,
    display_label: str,
    assembly_group: str,
    frame_number: int = 1,
    frame_count: int = 1,
) -> LoadedRecord:
    vertices: List[Vec3] = []
    primitives: List[Primitive] = []
    group_sort_vertices: Dict[int, int] = {}
    vertex_offset = 0
    group_offset = 0
    poly_count = 0
    line_count = 0

    for component_index, rec in enumerate(source_records):
        vertices.extend(rec.vertices)
        max_group = -1
        for prim in rec.primitives:
            rebased_group = group_offset + prim.group_index
            max_group = max(max_group, prim.group_index)
            if prim.kind == "polygon":
                poly_count += 1
            elif prim.kind == "line":
                line_count += 1
            primitives.append(Primitive(
                kind=prim.kind,
                indices=[vertex_offset + i for i in prim.indices],
                group_index=rebased_group,
                primitive_index=component_index * 1000000 + prim.primitive_index,
                opcode=prim.opcode,
                shade_level=prim.shade_level,
                material_or_flags=prim.material_or_flags,
            ))
        for gi, vi in rec.group_sort_vertices.items():
            group_sort_vertices[group_offset + gi] = vertex_offset + vi
            max_group = max(max_group, gi)
        vertex_offset += len(rec.vertices)
        declared_groups = int(rec.raw.get("group_count", 0) or 0)
        group_offset += max(declared_groups, max_group + 1, 1)

    mn, mx, center, radius = _bbox(vertices)
    first = source_records[0]
    raw: Dict[str, Any] = {
        "runtime_mode": True,
        "composite_mode": True,
        "display_label": display_label,
        "assembly_group_id": assembly_group,
        "assembly_frame_number": frame_number,
        "assembly_frame_count": frame_count,
        "source_records": [rec.record_index for rec in source_records],
        "offset": 0,
        "record_index": first.record_index,
        "group_count": group_offset,
        "polygon_count": poly_count,
        "line_count": line_count,
        "size": 0,
    }
    return LoadedRecord(
        resource_name=first.path.stem + ":assembled",
        path=first.path,
        record_index=first.record_index,
        raw=raw,
        vertices=vertices,
        primitives=primitives,
        group_sort_vertices=group_sort_vertices,
        bbox_min=mn,
        bbox_max=mx,
        center=center,
        radius=radius,
    )


def build_auto_assemblies_for_resource(resource: LoadedResource) -> List[LoadedRecord]:
    """Heuristic game-object assembly from adjacent component frame runs.

    Observed PCK files start with a small static component set, then store
    precomputed frame runs for individual components. Adjacent runs with the
    same frame count are assembled frame-by-frame.
    """
    records = resource.records
    if not records:
        return []

    assemblies: List[LoadedRecord] = []
    runs = find_frame_runs(records, min_length=2)
    long_runs = [run for run in runs if run[1] - run[0] + 1 >= 3]
    stem = resource.path.stem

    if long_runs and long_runs[0][0] > 0:
        prefix = records[:long_runs[0][0]]
        label = (
            f"{stem}:assembled static {_format_record_range(prefix)}   "
            f"v={sum(len(r.vertices) for r in prefix)}  "
            f"poly={sum(int(r.raw.get('polygon_count', 0) or 0) for r in prefix)}  "
            f"components={len(prefix)}"
        )
        assemblies.append(build_composite_record(
            prefix,
            display_label=label,
            assembly_group=f"{stem}:static",
        ))

    i = 0
    group_index = 0
    while i < len(long_runs):
        start, end, _sig = long_runs[i]
        frame_count = end - start + 1
        grouped = [long_runs[i]]
        j = i + 1
        while j < len(long_runs):
            n_start, n_end, _n_sig = long_runs[j]
            n_count = n_end - n_start + 1
            if n_start == grouped[-1][1] + 1 and n_count == frame_count:
                grouped.append(long_runs[j])
                j += 1
            else:
                break

        if len(grouped) >= 2:
            group_index += 1
            ranges = " + ".join(_format_record_range(records[a:b + 1]) for a, b, _s in grouped)
            assembly_group = f"{stem}:anim:{group_index}:{ranges}"
            for frame in range(frame_count):
                components = [records[a + frame] for a, _b, _s in grouped]
                label = (
                    f"{stem}:assembled {ranges} frame {frame + 1}/{frame_count}   "
                    f"v={sum(len(r.vertices) for r in components)}  "
                    f"poly={sum(int(r.raw.get('polygon_count', 0) or 0) for r in components)}  "
                    f"components={len(components)}"
                )
                assemblies.append(build_composite_record(
                    components,
                    display_label=label,
                    assembly_group=assembly_group,
                    frame_number=frame + 1,
                    frame_count=frame_count,
                ))
        i = max(j, i + 1)

    return assemblies


def build_auto_assemblies(resources: Sequence[LoadedResource]) -> List[LoadedRecord]:
    assemblies: List[LoadedRecord] = []
    for resource in resources:
        assemblies.extend(build_auto_assemblies_for_resource(resource))
    return assemblies


def write_loaded_obj(path: Path, rec: LoadedRecord) -> None:
    with path.open("w", encoding="utf-8") as f:
        f.write(f"# {rec.label}\n")
        for x, y, z in rec.vertices:
            f.write(f"v {x:.6f} {y:.6f} {z:.6f}\n")
        for prim in rec.primitives:
            if prim.kind == "polygon" and len(prim.indices) >= 3:
                idxs = " ".join(str(i + 1) for i in prim.indices)
                f.write(f"f {idxs}\n")
            elif prim.kind == "line" and len(prim.indices) >= 2:
                idxs = " ".join(str(i + 1) for i in prim.indices)
                f.write(f"l {idxs}\n")


class ViewerApp:
    def __init__(self, root: tk.Tk, initial_paths: Sequence[Path], *, scale: float, swizzle: str, runtime: bool, runtime_index_mode: str, backend: str = "gpu") -> None:
        self.root = root
        self.scale = scale
        self.swizzle = swizzle
        self.runtime = runtime
        self.runtime_index_mode = runtime_index_mode
        self.resources: List[LoadedResource] = []
        self.raw_records: List[LoadedRecord] = []
        self.assembled_records: List[LoadedRecord] = []
        self.records: List[LoadedRecord] = []
        self.show_assembled = False
        self.display_mode = "model"  # model | animation
        self.current_index = 0
        self.auto_rotate = False
        self.auto_frames = False
        self.frame_tick = 0
        self.frame_sequences: List[Tuple[int, int, Tuple[Any, ...]]] = []
        self.animation_frames: List[AnimationFrame] = []
        self.animation_preview_record: Optional[LoadedRecord] = None
        self.animation_preview_frame = 0
        self.animation_playing = False
        self.animation_next_time = 0.0
        self.status_text = tk.StringVar(value="Ready")
        self.anim_frame_var = tk.StringVar(value="0")
        self.anim_duration_var = tk.StringVar(value="180")
        self.anim_records_var = tk.StringVar(value="")
        self.drag_last: Optional[Tuple[int, int, str]] = None

        self.root.title("MechWarrior 1989 TBL GPU Model Viewer")
        self.root.geometry("1180x760")
        self._build_ui()
        self.renderer = GpuRenderer(self.canvas)
        self.renderer.render_backend = "gpu-vbo-ebo"
        self._bind_events()

        found_paths = list(initial_paths)
        if found_paths:
            self.load_paths([found_paths[0]])
        else:
            self.redraw()
        self._tick()

    def _build_ui(self) -> None:
        self.root.rowconfigure(0, weight=1)
        self.root.columnconfigure(1, weight=1)

        side = tk.Frame(self.root, padx=8, pady=8)
        side.grid(row=0, column=0, sticky="ns")
        side.rowconfigure(6, weight=1)

        tk.Label(side, text="TBL records", font=("TkDefaultFont", 11, "bold")).grid(row=0, column=0, sticky="w")
        tk.Button(side, text="Open TBL...", command=self.open_files).grid(row=1, column=0, sticky="ew", pady=(8, 2))
        tk.Button(side, text="Export current OBJ...", command=self.export_current_obj).grid(row=2, column=0, sticky="ew", pady=2)
        self.view_mode_button = tk.Button(side, text="Show animation", command=self.toggle_record_mode, relief=tk.RAISED)
        self.view_mode_button.grid(row=3, column=0, sticky="ew", pady=2)
        tk.Button(side, text="Reset view", command=self.reset_view).grid(row=4, column=0, sticky="ew", pady=2)

        self.summary_label = tk.Label(side, text="No resources loaded", justify="left", anchor="w", width=34)
        self.summary_label.grid(row=5, column=0, sticky="ew", pady=(8, 4))

        self.listbox = tk.Listbox(side, width=42, exportselection=False)
        self.listbox.grid(row=6, column=0, sticky="nsew")
        self.listbox.bind("<<ListboxSelect>>", self.on_list_select)
        scrollbar = tk.Scrollbar(side, command=self.listbox.yview)
        scrollbar.grid(row=6, column=1, sticky="ns")
        self.listbox.configure(yscrollcommand=scrollbar.set)

        tk.Button(side, text="Add to animation list", command=self.add_selected_model_to_animation).grid(row=7, column=0, columnspan=2, sticky="ew", pady=(6, 0))

        editor = tk.LabelFrame(side, text="MWA animation", padx=6, pady=6)
        editor.grid(row=8, column=0, columnspan=2, sticky="ew", pady=(8, 0))
        editor.columnconfigure(1, weight=1)

        tk.Label(editor, text="Frame").grid(row=0, column=0, sticky="w")
        self.anim_frame_spin = tk.Spinbox(editor, from_=0, to=999, width=6, textvariable=self.anim_frame_var, command=self.on_animation_frame_change)
        self.anim_frame_spin.grid(row=0, column=1, sticky="ew", padx=(4, 0))
        tk.Label(editor, text="ms").grid(row=0, column=2, sticky="e", padx=(6, 0))
        tk.Entry(editor, width=6, textvariable=self.anim_duration_var).grid(row=0, column=3, sticky="ew", padx=(4, 0))

        tk.Label(editor, text="Records").grid(row=1, column=0, sticky="w", pady=(4, 0))
        tk.Entry(editor, textvariable=self.anim_records_var).grid(row=1, column=1, columnspan=3, sticky="ew", padx=(4, 0), pady=(4, 0))

        self.component_listbox = tk.Listbox(editor, height=7, selectmode=tk.EXTENDED, exportselection=False)
        self.component_listbox.grid(row=2, column=0, columnspan=4, sticky="ew", pady=(6, 0))
        self.component_listbox.bind("<<ListboxSelect>>", self.on_component_select)

        tk.Button(editor, text="Preview/apply", command=self.apply_animation_frame).grid(row=3, column=0, columnspan=2, sticky="ew", pady=(6, 0))
        tk.Button(editor, text="Add frame", command=self.add_animation_frame).grid(row=3, column=2, columnspan=2, sticky="ew", padx=(4, 0), pady=(6, 0))
        tk.Button(editor, text="Delete", command=self.delete_animation_frame).grid(row=4, column=0, sticky="ew", pady=(4, 0))
        self.anim_play_button = tk.Button(editor, text="Play MWA", command=self.toggle_animation_playback)
        self.anim_play_button.grid(row=4, column=1, sticky="ew", padx=(4, 0), pady=(4, 0))
        tk.Button(editor, text="Save", command=self.save_animation).grid(row=4, column=2, sticky="ew", padx=(4, 0), pady=(4, 0))
        tk.Button(editor, text="Load", command=self.load_animation).grid(row=4, column=3, sticky="ew", padx=(4, 0), pady=(4, 0))

        self.status = tk.Label(self.root, textvariable=self.status_text, anchor="w")
        self.status.grid(row=1, column=0, columnspan=2, sticky="ew")

        self.canvas = OpenGLViewport(self.root)
        self.canvas.grid(row=0, column=1, sticky="nsew")

    def _bind_events(self) -> None:
        self.canvas.bind("<Configure>", lambda _e: self.redraw())
        self.canvas.bind("<ButtonPress-1>", lambda e: self._drag_start(e, "rotate"))
        self.canvas.bind("<B1-Motion>", self._drag_move)
        self.canvas.bind("<ButtonPress-2>", lambda e: self._drag_start(e, "pan"))
        self.canvas.bind("<B2-Motion>", self._drag_move)
        self.canvas.bind("<ButtonPress-3>", lambda e: self._drag_start(e, "pan"))
        self.canvas.bind("<B3-Motion>", self._drag_move)
        self.canvas.bind("<MouseWheel>", self._on_wheel)       # Windows/macOS
        self.canvas.bind("<Button-4>", lambda e: self._zoom(1.10))  # X11
        self.canvas.bind("<Button-5>", lambda e: self._zoom(1 / 1.10))
        self.root.bind("<Key>", self._on_key)

    def _drag_start(self, event: tk.Event, mode: str) -> None:
        self.drag_last = (int(event.x), int(event.y), mode)
        self.canvas.focus_set()

    def _drag_move(self, event: tk.Event) -> None:
        if self.drag_last is None:
            return
        x0, y0, mode = self.drag_last
        x1, y1 = int(event.x), int(event.y)
        dx, dy = x1 - x0, y1 - y0
        self.drag_last = (x1, y1, mode)
        if mode == "pan":
            self.renderer.pan_x += dx
            self.renderer.pan_y += dy
        else:
            self.renderer.yaw += dx * 0.009
            self.renderer.pitch += dy * 0.009
            self.renderer.pitch = max(-math.pi * 0.49, min(math.pi * 0.49, self.renderer.pitch))
        self.redraw()

    def _on_wheel(self, event: tk.Event) -> None:
        if getattr(event, "delta", 0) > 0:
            self._zoom(1.10)
        else:
            self._zoom(1 / 1.10)

    def _zoom(self, factor: float) -> None:
        self.renderer.zoom = max(0.05, min(40.0, self.renderer.zoom * factor))
        self.redraw()

    def _on_key(self, event: tk.Event) -> None:
        key = event.keysym.lower()
        if key in ("n", "pagedown", "down", "right"):
            self.select_delta(1)
        elif key in ("b", "p", "pageup", "up", "left") and key != "p":
            self.select_delta(-1)
        elif key == "p":
            self.renderer.perspective = not self.renderer.perspective
            self.redraw()
        elif key == "w":
            self.renderer.solid = not self.renderer.solid
            self.redraw()
        elif key == "g":
            self.renderer.group_sort_mode = not self.renderer.group_sort_mode
            self.redraw()
        elif key == "l":
            self.renderer.show_lines = not self.renderer.show_lines
            self.redraw()
        elif key == "s":
            order = ["stored", "lit", "ega"]
            self.renderer.shade_mode = order[(order.index(self.renderer.shade_mode) + 1) % len(order)]
            self.redraw()
        elif key == "c":
            order = ["off", "nz", "pz", "screen_cw", "screen_ccw"]
            self.renderer.cull_mode = order[(order.index(self.renderer.cull_mode) + 1) % len(order)]
            self.redraw()
        elif key == "d":
            self.renderer.depth_order = "near" if self.renderer.depth_order == "far" else "far"
            self.redraw()
        elif key == "z":
            self.renderer.render_backend = "gpu-vbo-ebo"
            self.status_text.set("GPU VBO/EBO backend is active")
            self.redraw()
        elif key in ("bracketleft", "comma"):
            self.select_frame_delta(-1)
        elif key in ("bracketright", "period"):
            self.select_frame_delta(1)
        elif key == "f":
            self.auto_frames = not self.auto_frames
            self.status_text.set(f"Frame animation {'on' if self.auto_frames else 'off'}")
        elif key == "e":
            self.export_current_obj()
        elif key == "o":
            self.open_files()
        elif key == "t":
            self.toggle_record_mode()
        elif key == "a" or key == "space":
            self.auto_rotate = not self.auto_rotate
            self.status_text.set(f"Auto-rotate {'on' if self.auto_rotate else 'off'}")
        elif key in ("plus", "equal", "kp_add"):
            self._zoom(1.15)
        elif key in ("minus", "kp_subtract"):
            self._zoom(1 / 1.15)
        elif key == "r":
            self.reset_view()
        elif key == "1":
            self.renderer.apply_view_preset("game")
            self.redraw()
        elif key == "2":
            self.renderer.apply_view_preset("side")
            self.redraw()
        elif key == "3":
            self.renderer.apply_view_preset("front")
            self.redraw()
        elif key == "v":
            self.renderer.show_vertices = not self.renderer.show_vertices
            self.redraw()
        elif key == "x":
            self.renderer.show_axes = not self.renderer.show_axes
            self.redraw()
        elif key == "escape":
            self.root.quit()

    def open_files(self) -> None:
        name = filedialog.askopenfilename(
            title="Open MechWarrior 1989 PCK/TBL resources",
            filetypes=[("TBL resources", "*.TBL"), ("All files", "*.*")],
        )
        if name:
            self.load_paths([Path(name)])

    def load_paths(self, paths: Sequence[Path]) -> None:
        paths = list(paths[:1])
        self.status_text.set("Loading resources...")
        self.root.update_idletasks()
        loaded: List[LoadedResource] = []
        errors: List[str] = []
        for path in paths:
            try:
                loaded.append(load_resource(path, scale=self.scale, swizzle=self.swizzle, runtime=self.runtime, runtime_index_mode=self.runtime_index_mode))
            except Exception as exc:
                errors.append(str(exc))
                traceback.print_exc()
        if loaded:
            self.resources = loaded
            self.raw_records = [r for res in loaded for r in res.records]
            self.assembled_records = build_auto_assemblies(loaded) if self.runtime else []
            self.animation_playing = False
            self.animation_preview_record = None
            self._init_default_animation()
            self.show_assembled = False
            self.display_mode = "model"
            self._apply_record_mode(reset_index=True)
            self.current_index = 0
            self._update_summary()
            self.root.title(f"MechWarrior 1989 TBL GPU Model Viewer - {loaded[0].path.name}")
            self.renderer.reset_view()
            self.select_index(0)
        if errors:
            messagebox.showwarning("Some files could not be loaded", "\n\n".join(errors))
        if not loaded:
            self.status_text.set("No resources loaded")
            self.redraw()

    def _apply_record_mode(self, *, reset_index: bool = False) -> None:
        self.records = self.raw_records
        if self.display_mode == "animation":
            self.view_mode_button.configure(text="Show model", relief=tk.SUNKEN)
        else:
            self.view_mode_button.configure(text="Show animation", relief=tk.RAISED)
        if reset_index:
            self.current_index = 0
        elif self.records:
            self.current_index = max(0, min(self.current_index, len(self.records) - 1))
        self._rebuild_record_list()
        self._rebuild_frame_sequences()

    def toggle_record_mode(self) -> None:
        self.display_mode = "animation" if self.display_mode == "model" else "model"
        self.animation_preview_record = None
        self.animation_playing = False
        self.anim_play_button.configure(text="Play MWA")
        self._apply_record_mode(reset_index=False)
        self._update_summary()
        self.status_text.set(f"Showing {'animation frame' if self.display_mode == 'animation' else 'model object'}")
        self.fit_current_record()
        self.redraw()

    def _refresh_animation_list(self) -> None:
        self.component_listbox.delete(0, tk.END)
        frame = self._animation_frame_by_index(self._current_editor_frame_index())
        indices = frame.record_indices if frame is not None else self._parse_record_indices_safe(self.anim_records_var.get())
        by_index = {rec.record_index: rec for rec in self.raw_records}
        for idx in indices:
            rec = by_index.get(idx)
            if rec is None:
                self.component_listbox.insert(tk.END, f"#{idx:03d}  missing")
                continue
            self.component_listbox.insert(
                tk.END,
                f"#{rec.record_index:03d}  v={len(rec.vertices)} poly={rec.raw.get('polygon_count', 0)} "
                f"parts={rec.raw.get('group_count', 0)}",
            )

    def _init_default_animation(self) -> None:
        self.animation_frames = []
        self._set_editor_frame(0)

    def _current_editor_frame_index(self) -> int:
        try:
            return int(self.anim_frame_var.get())
        except ValueError:
            return 0

    def _parse_record_indices_safe(self, text: str) -> List[int]:
        try:
            return self._parse_record_indices(text)
        except ValueError:
            return []

    def _parse_record_indices(self, text: str) -> List[int]:
        out: List[int] = []
        cleaned = text.replace(";", ",").replace("+", ",").replace(" ", ",")
        for token in [t for t in cleaned.split(",") if t.strip()]:
            token = token.strip().lstrip("#")
            if "-" in token:
                a, b = token.split("-", 1)
                start = int(a)
                end = int(b)
                step = 1 if end >= start else -1
                out.extend(range(start, end + step, step))
            else:
                out.append(int(token))
        seen = set()
        unique = []
        valid = {rec.record_index for rec in self.raw_records}
        for idx in out:
            if idx in valid and idx not in seen:
                unique.append(idx)
                seen.add(idx)
        return unique

    def _format_record_indices(self, indices: Sequence[int]) -> str:
        return ", ".join(str(int(i)) for i in indices)

    def _animation_frame_by_index(self, frame_index: int) -> Optional[AnimationFrame]:
        for frame in self.animation_frames:
            if frame.frame_index == frame_index:
                return frame
        return None

    def _set_editor_frame(self, frame_index: int) -> None:
        frame = self._animation_frame_by_index(frame_index)
        if frame is None:
            self.anim_frame_var.set(str(frame_index))
            self.anim_duration_var.set("180")
            self.anim_records_var.set("")
            self._refresh_animation_list()
            return
        self.anim_frame_var.set(str(frame.frame_index))
        self.anim_duration_var.set(str(frame.duration_ms))
        self.anim_records_var.set(self._format_record_indices(frame.record_indices))
        self._refresh_animation_list()

    def _select_component_records(self, record_indices: Sequence[int]) -> None:
        wanted = {int(i) for i in record_indices}
        self.component_listbox.selection_clear(0, tk.END)
        for row, rec in enumerate(self.raw_records):
            if rec.record_index in wanted:
                self.component_listbox.selection_set(row)
                self.component_listbox.see(row)

    def on_animation_frame_change(self) -> None:
        try:
            frame_index = int(self.anim_frame_var.get())
        except ValueError:
            return
        self._set_editor_frame(frame_index)
        self.animation_preview_record = None
        if self.display_mode == "animation":
            self.fit_current_record()
            self.redraw()

    def on_component_select(self, _event: tk.Event) -> None:
        return

    def add_selected_model_to_animation(self) -> None:
        sel = self.listbox.curselection()
        if not sel or not self.records:
            self.status_text.set("Select a model object first")
            return
        rec = self.records[int(sel[0])]
        frame_index = self._current_editor_frame_index()
        try:
            duration = max(1, int(self.anim_duration_var.get()))
        except ValueError:
            duration = 180
            self.anim_duration_var.set(str(duration))
        frame = self._animation_frame_by_index(frame_index)
        if frame is None:
            frame = AnimationFrame(frame_index=frame_index, record_indices=[], duration_ms=duration)
            self.animation_frames.append(frame)
            self.animation_frames.sort(key=lambda f: f.frame_index)
        frame.duration_ms = duration
        if rec.record_index not in frame.record_indices:
            frame.record_indices.append(rec.record_index)
        self.anim_records_var.set(self._format_record_indices(frame.record_indices))
        self.animation_preview_record = None
        self._refresh_animation_list()
        self._update_summary()
        self.status_text.set(f"Added #{rec.record_index:03d} to MWA frame {frame.frame_index}")
        if self.display_mode == "animation":
            self.fit_current_record()
            self.redraw()

    def _raw_records_for_indices(self, indices: Sequence[int]) -> List[LoadedRecord]:
        by_index = {rec.record_index: rec for rec in self.raw_records}
        return [by_index[i] for i in indices if i in by_index]

    def _build_animation_preview_record(self, frame: AnimationFrame) -> Optional[LoadedRecord]:
        components = self._raw_records_for_indices(frame.record_indices)
        if not components:
            return None
        stem = self.resources[0].path.stem if self.resources else "MWA"
        label = (
            f"{stem}:MWA frame {frame.frame_index:03d}   "
            f"v={sum(len(r.vertices) for r in components)}  "
            f"poly={sum(int(r.raw.get('polygon_count', 0) or 0) for r in components)}  "
            f"components={len(components)}  ms={frame.duration_ms}"
        )
        return build_composite_record(
            components,
            display_label=label,
            assembly_group=f"{stem}:mwa:{frame.frame_index}",
            frame_number=frame.frame_index,
            frame_count=max(1, len(self.animation_frames)),
        )

    def apply_animation_frame(self) -> None:
        try:
            frame_index = int(self.anim_frame_var.get())
            duration = max(1, int(self.anim_duration_var.get()))
            indices = self._parse_record_indices(self.anim_records_var.get())
        except ValueError as exc:
            messagebox.showerror("MWA frame error", f"Bad frame data: {exc}")
            return
        if not indices:
            messagebox.showwarning("MWA frame error", "Select at least one source record.")
            return
        frame = self._animation_frame_by_index(frame_index)
        if frame is None:
            frame = AnimationFrame(frame_index=frame_index, record_indices=indices, duration_ms=duration)
            self.animation_frames.append(frame)
            self.animation_frames.sort(key=lambda f: f.frame_index)
        else:
            frame.record_indices = indices
            frame.duration_ms = duration
        preview = self._build_animation_preview_record(frame)
        if preview is None:
            return
        self.animation_preview_record = preview
        self.animation_preview_frame = frame.frame_index
        self.display_mode = "animation"
        self._apply_record_mode(reset_index=False)
        self._set_editor_frame(frame.frame_index)
        self.fit_current_record()
        self.status_text.set(f"Preview MWA frame {frame.frame_index}: {self._format_record_indices(indices)}")
        self.redraw()

    def add_animation_frame(self) -> None:
        next_index = (max((f.frame_index for f in self.animation_frames), default=-1) + 1)
        try:
            duration = max(1, int(self.anim_duration_var.get()))
        except ValueError:
            duration = 180
        self.animation_frames.append(AnimationFrame(next_index, [], duration))
        self.animation_frames.sort(key=lambda f: f.frame_index)
        self._set_editor_frame(next_index)
        self.animation_preview_record = None
        self._update_summary()
        self.status_text.set(f"Added empty MWA frame {next_index}")
        if self.display_mode == "animation":
            self.redraw()

    def delete_animation_frame(self) -> None:
        try:
            frame_index = int(self.anim_frame_var.get())
        except ValueError:
            return
        frame = self._animation_frame_by_index(frame_index)
        selected_rows = {int(i) for i in self.component_listbox.curselection()}
        if frame is not None and selected_rows:
            frame.record_indices = [idx for row, idx in enumerate(frame.record_indices) if row not in selected_rows]
            if frame.record_indices:
                self.anim_records_var.set(self._format_record_indices(frame.record_indices))
            else:
                self.animation_frames = [f for f in self.animation_frames if f.frame_index != frame_index]
                self.anim_records_var.set("")
            self.animation_preview_record = None
            self._refresh_animation_list()
            self._update_summary()
            if self.display_mode == "animation":
                self.fit_current_record()
                self.redraw()
            return
        self.animation_frames = [f for f in self.animation_frames if f.frame_index != frame_index]
        self.animation_preview_record = None
        next_frame = self.animation_frames[0].frame_index if self.animation_frames else 0
        self._set_editor_frame(next_frame)
        self._update_summary()
        self.redraw()

    def toggle_animation_playback(self) -> None:
        playable = [f for f in sorted(self.animation_frames, key=lambda f: f.frame_index) if f.record_indices]
        if not playable:
            self.status_text.set("MWA has no non-empty frames")
            return
        if self.animation_playing:
            self.animation_playing = False
            self.anim_play_button.configure(text="Play MWA")
            self.status_text.set("MWA playback stopped")
            return
        current = self._animation_frame_by_index(self._current_editor_frame_index())
        if current is None or not current.record_indices:
            current = playable[0]
            self._set_editor_frame(current.frame_index)
        preview = self._build_animation_preview_record(current)
        if preview is None:
            return
        self.display_mode = "animation"
        self._apply_record_mode(reset_index=False)
        self.animation_preview_record = preview
        self.animation_preview_frame = current.frame_index
        self.fit_current_record()
        self.redraw()
        self.animation_playing = True
        self.anim_play_button.configure(text="Stop MWA")
        self.animation_next_time = time.perf_counter() + max(1, current.duration_ms) / 1000.0

    def _advance_animation_playback(self) -> None:
        if not self.animation_frames:
            self.animation_playing = False
            self.anim_play_button.configure(text="Play MWA")
            return
        current = self._animation_frame_by_index(self.animation_preview_frame)
        ordered = [f for f in sorted(self.animation_frames, key=lambda f: f.frame_index) if f.record_indices]
        if not ordered:
            self.animation_playing = False
            self.anim_play_button.configure(text="Play MWA")
            return
        if current in ordered:
            pos = ordered.index(current)
            frame = ordered[(pos + 1) % len(ordered)]
        else:
            frame = ordered[0]
        self._set_editor_frame(frame.frame_index)
        preview = self._build_animation_preview_record(frame)
        if preview is not None:
            self.animation_preview_record = preview
            self.animation_preview_frame = frame.frame_index
            self.fit_current_record()
            self.redraw()
        self.animation_next_time = time.perf_counter() + max(1, frame.duration_ms) / 1000.0

    def save_animation(self) -> None:
        if not self.resources:
            return
        if self.anim_records_var.get().strip():
            try:
                frame_index = int(self.anim_frame_var.get())
                duration = max(1, int(self.anim_duration_var.get()))
                indices = self._parse_record_indices(self.anim_records_var.get())
                frame = self._animation_frame_by_index(frame_index)
                if frame is None and indices:
                    self.animation_frames.append(AnimationFrame(frame_index, indices, duration))
                elif frame is not None:
                    frame.record_indices = indices
                    frame.duration_ms = duration
                self.animation_frames.sort(key=lambda f: f.frame_index)
            except ValueError:
                messagebox.showerror("MWA save failed", "Bad current frame data.")
                return
        data = {
            "format": "MWA",
            "version": 1,
            "model": self.resources[0].path.name,
            "frames": [
                {
                    "frame": frame.frame_index,
                    "duration_ms": frame.duration_ms,
                    "records": frame.record_indices,
                }
                for frame in sorted(self.animation_frames, key=lambda f: f.frame_index)
            ],
        }
        default = f"{self.resources[0].path.stem}.mwa"
        name = filedialog.asksaveasfilename(
            title="Save MechWarrior animation",
            initialfile=default,
            defaultextension=".mwa",
            filetypes=[("MechWarrior animation", "*.mwa"), ("JSON text", "*.json"), ("All files", "*.*")],
        )
        if not name:
            return
        Path(name).write_text(json.dumps(data, indent=2), encoding="utf-8")
        self.status_text.set(f"Saved MWA: {name}")

    def load_animation(self) -> None:
        name = filedialog.askopenfilename(
            title="Load MechWarrior animation",
            filetypes=[("MechWarrior animation", "*.mwa"), ("JSON text", "*.json"), ("All files", "*.*")],
        )
        if not name:
            return
        try:
            data = json.loads(Path(name).read_text(encoding="utf-8"))
            frames = []
            for item in data.get("frames", []):
                frames.append(AnimationFrame(
                    frame_index=int(item.get("frame", len(frames))),
                    duration_ms=max(1, int(item.get("duration_ms", 180))),
                    record_indices=[int(i) for i in item.get("records", [])],
                ))
        except Exception as exc:
            messagebox.showerror("MWA load failed", str(exc))
            return
        if data.get("model") and self.resources and data.get("model") != self.resources[0].path.name:
            messagebox.showwarning("MWA model mismatch", f"Animation is for {data.get('model')}, current file is {self.resources[0].path.name}.")
        self.animation_frames = sorted(frames, key=lambda f: f.frame_index)
        frame_index = self.animation_frames[0].frame_index if self.animation_frames else 0
        self._set_editor_frame(frame_index)
        if self.animation_frames:
            self.apply_animation_frame()
        else:
            self.animation_preview_record = None
            self.redraw()
        self._update_summary()
        self.status_text.set(f"Loaded MWA: {name}")

    def _rebuild_record_list(self) -> None:
        self.listbox.delete(0, tk.END)
        for rec in self.records:
            self.listbox.insert(tk.END, rec.label)

    def _update_summary(self) -> None:
        mode = "animation" if self.display_mode == "animation" else "model"
        lines = [
            f"Resources: {len(self.resources)}",
            f"Mode: {mode}",
            f"Shown: {len(self.records)}",
            f"Raw records: {len(self.raw_records)}",
        ]
        lines.append(f"MWA frames: {len(self.animation_frames)}")
        for res in self.resources:
            summ = res.parsed.get("summary", {})
            lines.append(
                f"{res.path.name}: {len(res.records)} rec, "
                f"poly={summ.get('total_polygons', '?')}, line={summ.get('total_lines', '?')}"
            )
        if self.frame_sequences:
            lines.append(f"Frame sequences: {len(self.frame_sequences)}")
        self.summary_label.configure(text="\n".join(lines))

    def _frame_signature(self, rec: LoadedRecord) -> Tuple[int, int, int]:
        return frame_signature(rec)

    def _rebuild_frame_sequences(self) -> None:
        self.frame_sequences = []
        if not self.records:
            return
        start = 0
        prev: Tuple[Any, ...] = self._record_sequence_signature(self.records[0])
        for i in range(1, len(self.records) + 1):
            sig = self._record_sequence_signature(self.records[i]) if i < len(self.records) else None
            same_resource = i < len(self.records) and self.records[i].path == self.records[i - 1].path
            if sig != prev or not same_resource:
                if i - start >= 2:
                    self.frame_sequences.append((start, i - 1, prev))
                start = i
                if sig is not None:
                    prev = sig

    def _record_sequence_signature(self, rec: LoadedRecord) -> Tuple[Any, ...]:
        assembly_group = rec.raw.get("assembly_group_id")
        if assembly_group:
            return ("assembly", assembly_group)
        return ("raw",) + frame_signature(rec)

    def _current_frame_sequence(self) -> Optional[Tuple[int, int, Tuple[Any, ...]]]:
        for seq in self.frame_sequences:
            if seq[0] <= self.current_index <= seq[1]:
                return seq
        return None

    def select_frame_delta(self, delta: int) -> None:
        seq = self._current_frame_sequence()
        if seq is None:
            self.select_delta(delta)
            return
        start, end, _sig = seq
        span = end - start + 1
        self.select_index(start + ((self.current_index - start + delta) % span))


    def on_list_select(self, _event: tk.Event) -> None:
        sel = self.listbox.curselection()
        if sel:
            self.select_index(int(sel[0]))

    def select_delta(self, delta: int) -> None:
        if not self.records:
            return
        self.select_index((self.current_index + delta) % len(self.records))

    def select_index(self, index: int) -> None:
        if not self.records:
            self.current_index = 0
            self.redraw()
            return
        if not self.animation_playing:
            self.animation_preview_record = None
        self.current_index = max(0, min(index, len(self.records) - 1))
        self.listbox.selection_clear(0, tk.END)
        self.listbox.selection_set(self.current_index)
        self.listbox.see(self.current_index)
        self.status_text.set(self.records[self.current_index].label)
        self.fit_current_record()
        self.redraw()

    def current_record(self) -> Optional[LoadedRecord]:
        if self.display_mode == "animation":
            frame_index = self._current_editor_frame_index()
            frame = self._animation_frame_by_index(frame_index)
            if frame is None and self.anim_records_var.get().strip():
                indices = self._parse_record_indices_safe(self.anim_records_var.get())
                if indices:
                    try:
                        duration = max(1, int(self.anim_duration_var.get()))
                    except ValueError:
                        duration = 180
                    frame = AnimationFrame(frame_index, indices, duration)
            if frame is None or not frame.record_indices:
                return None
            if self.animation_preview_record is not None and self.animation_preview_frame == frame.frame_index:
                return self.animation_preview_record
            return self._build_animation_preview_record(frame)
        if not self.records:
            return None
        return self.records[self.current_index]

    def reset_view(self) -> None:
        self.renderer.reset_view()
        self.fit_current_record()
        self.redraw()

    def fit_current_record(self) -> None:
        rec = self.current_record()
        if rec is None:
            return
        width = max(self.canvas.winfo_width(), 320)
        height = max(self.canvas.winfo_height(), 240)
        self.renderer.fit_record(rec, width, height)

    def export_current_obj(self) -> None:
        rec = self.current_record()
        if rec is None:
            messagebox.showwarning("OBJ export", "Nothing to export in the current view.")
            return
        if self.display_mode == "animation":
            default = f"{rec.resource_name}_frame_{self._current_editor_frame_index():03d}.obj"
        else:
            default = f"{rec.resource_name}_record_{rec.record_index:03d}.obj"
        name = filedialog.asksaveasfilename(
            title="Export current preview mesh as OBJ",
            initialfile=default,
            defaultextension=".obj",
            filetypes=[("Wavefront OBJ", "*.obj"), ("All files", "*.*")],
        )
        if not name:
            return
        try:
            if rec.raw.get("runtime_mode"):
                write_loaded_obj(Path(name), rec)
            else:
                tbl.write_obj_record(Path(name), rec.raw, scale=self.scale, swizzle=self.swizzle, write_mtl=True)
            self.status_text.set(f"Exported OBJ: {name}")
        except Exception as exc:
            messagebox.showerror("OBJ export failed", str(exc))

    def redraw(self) -> None:
        rec = self.current_record()
        overlays: List[str] = []
        if self.auto_rotate:
            overlays.append("Auto-rotate on")
        if self.auto_frames:
            overlays.append("Frame animation on")
        if self.animation_preview_record is not None:
            overlays.append(f"MWA frame {self.animation_preview_frame}")
        seq = self._current_frame_sequence()
        if seq is not None and self.animation_preview_record is None:
            start, end, sig = seq
            if self.records[self.current_index].raw.get("composite_mode"):
                overlays.append(
                    f"assembled frame {self.current_index - start + 1}/{end - start + 1}"
                )
            else:
                overlays.append(
                    f"frame-seq #{self.records[start].record_index:03d}-#{self.records[end].record_index:03d} "
                    f"frame {self.current_index - start + 1}/{end - start + 1} sig=parts/poly/line {sig[1:] if sig and sig[0] == 'raw' else sig}"
                )
        self.renderer.render(rec, overlay=" | ".join(overlays))

    def _tick(self) -> None:
        dirty = False
        if self.auto_rotate:
            self.renderer.yaw += 0.012
            dirty = True
        if self.auto_frames:
            self.frame_tick += 1
            if self.frame_tick >= 10:
                self.frame_tick = 0
                self.select_frame_delta(1)
                dirty = False
        if self.animation_playing and time.perf_counter() >= self.animation_next_time:
            self._advance_animation_playback()
            dirty = False
        if dirty:
            self.redraw()
        self.root.after(16, self._tick)


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Interactive MechWarrior 1989 PCK/TBL model preview viewer.")
    parser.add_argument("paths", nargs="*", type=Path, help="JENPCK.TBL / LOCPCK.TBL / MARPCK.TBL files")
    parser.add_argument("--scale", type=float, default=1.0, help="Coordinate scale before rendering/exporting")
    parser.add_argument("--swizzle", default="xzy", choices=["xyz", "xzy", "yxz", "yzx", "zxy", "zyx"],
                        help="Axis swizzle passed through to the parser/exporter (default: xzy, current game-like starting hypothesis)")
    parser.add_argument("--runtime", action="store_true", default=True,
                        help="Use the runtime-structure parser (default; kept for old launch scripts)")
    parser.add_argument("--legacy", action="store_true",
                        help="Use the old preview parser instead of runtime assembly")
    parser.add_argument("--runtime-index-mode", default="raw", choices=["raw", "minus1"],
                        help="Runtime primitive index hypothesis: raw or minus1 (default: raw)")
    parser.add_argument("--backend", default="gpu", choices=["gpu"],
                        help="Render backend. The GPU VBO/EBO path is used by this viewer.")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    root = tk.Tk()
    ViewerApp(root, args.paths, scale=args.scale, swizzle=args.swizzle, runtime=(args.runtime and not args.legacy), runtime_index_mode=args.runtime_index_mode, backend=args.backend)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
