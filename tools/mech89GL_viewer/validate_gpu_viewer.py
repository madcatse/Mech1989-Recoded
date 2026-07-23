#!/usr/bin/env python3
"""Headless validation for the GPU MechWarrior 1989 viewer."""
from __future__ import annotations

import argparse
import math
from pathlib import Path
from typing import Sequence

import numpy as np

import mw1989_viewer as viewer
from mw1989_gpu import GpuBatch, triangulate_record


def _default_resource() -> Path:
    here = Path(__file__).resolve()
    repo = here.parents[1]
    return repo / "Sorted Original Files" / "TBL" / "viewer8" / "JENPCK.TBL"


def _find_renderable(records: Sequence[viewer.LoadedRecord]) -> viewer.LoadedRecord:
    for rec in records:
        if rec.vertices and any(p.kind == "polygon" and len(p.indices) >= 3 for p in rec.primitives):
            return rec
    raise AssertionError("no renderable polygon record found")


def _check_batch(batch: GpuBatch, label: str) -> None:
    assert batch.vertices.ndim == 2 and batch.vertices.shape[1] == 6, f"{label}: VBO rows must be xyz+rgb"
    assert batch.vertices.dtype == np.float32, f"{label}: VBO must be float32"
    assert batch.triangle_indices.dtype == np.uint32, f"{label}: triangle EBO must be uint32"
    assert batch.line_indices.dtype == np.uint32, f"{label}: line EBO must be uint32"
    assert batch.triangle_indices.size % 3 == 0, f"{label}: triangle index count is not divisible by 3"
    assert batch.line_indices.size % 2 == 0, f"{label}: line index count is not divisible by 2"
    assert batch.triangle_count > 0, f"{label}: expected at least one GPU triangle"
    if batch.vertices.size:
        assert np.isfinite(batch.vertices).all(), f"{label}: VBO contains non-finite values"
        colors = batch.vertices[:, 3:6]
        assert ((0.0 <= colors) & (colors <= 1.0)).all(), f"{label}: colors must be normalized RGB"
        max_index = max(
            int(batch.triangle_indices.max(initial=0)),
            int(batch.line_indices.max(initial=0)),
        )
        assert max_index < len(batch.vertices), f"{label}: EBO references a missing VBO vertex"
    assert batch.group_matrices, f"{label}: expected part/group transform matrices"
    for group, matrix in batch.group_matrices.items():
        assert matrix.shape == (4, 4), f"{label}: group {group} matrix is not 4x4"
        assert np.isfinite(matrix).all(), f"{label}: group {group} matrix contains non-finite values"
        assert math.isclose(float(matrix[3, 3]), 1.0), f"{label}: group {group} matrix has invalid homogeneous row"


def validate(path: Path) -> None:
    assert path.exists(), f"resource not found: {path}"
    resource = viewer.load_resource(path, runtime=True, runtime_index_mode="raw")
    assert resource.records, "import produced no records"
    record = _find_renderable(resource.records)

    stored = triangulate_record(record, shade_mode="stored", show_edges=True, show_lines=True)
    _check_batch(stored, "stored colors")

    wire = triangulate_record(record, shade_mode="stored", show_edges=True, edge_rgb=(0.86, 0.86, 0.86))
    _check_batch(wire, "wireframe colors")
    assert np.any(np.all(np.isclose(wire.vertices[:, 3:6], (0.86, 0.86, 0.86)), axis=1)), "wireframe edge color was not uploaded"

    lit = triangulate_record(record, shade_mode="lit", yaw=-1.1, pitch=0.1, roll=0.0)
    _check_batch(lit, "lit colors")

    ega = triangulate_record(record, shade_mode="ega")
    _check_batch(ega, "ega colors")

    components = resource.records[: min(3, len(resource.records))]
    composite = viewer.build_composite_record(
        components,
        display_label="validation MWA frame",
        assembly_group="validation:mwa",
        frame_number=0,
        frame_count=1,
    )
    anim_batch = triangulate_record(composite, shade_mode="stored", show_edges=True)
    _check_batch(anim_batch, "animation composite")
    assert composite.raw.get("composite_mode"), "animation preview record did not preserve composite metadata"
    assert len(composite.raw.get("source_records", [])) == len(components), "animation source records were not retained"

    print("import: ok")
    print(f"record: #{record.record_index:03d} vertices={len(record.vertices)} primitives={len(record.primitives)}")
    print(f"triangulation: ok triangles={stored.triangle_count} line_segments={stored.line_count}")
    print(f"colors: ok stored/lit/ega/wire color sets={stored.color_count}/{lit.color_count}/{ega.color_count}/{wire.color_count}")
    print(f"part transforms: ok matrices={len(stored.group_matrices)}")
    print(f"animation gpu batch: ok triangles={anim_batch.triangle_count} sources={len(components)}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the headless GPU viewer pipeline.")
    parser.add_argument("path", nargs="?", type=Path, default=_default_resource(), help="PCK/TBL resource to validate")
    args = parser.parse_args()
    validate(args.path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
