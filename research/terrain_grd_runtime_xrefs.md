# Terrain GRD Runtime Xrefs

Phase 5 status note for the one-byte `TILE*.GRD` battlefield grid.

## Confirmed direct GRD buffer accesses

The decompiled `BTECH.EXE` currently exposes all direct accesses to the combined battlefield GRD buffer through the same DS-relative storage window. The meaningful xrefs are:

| Role | Decompiled location | Observed behavior |
| --- | --- | --- |
| Load four GRD chunks | `research/disassembly/BTECH.EXE/BTECH.EXE.c:8139` / `FUN_1000_242c` | For each of four scenario tile slots, reads `0x57` runs of `0x2f` bytes into `(tileBaseX + column) * 0x5e + tileBaseY - 0x7fda`. This proves an 87 x 47 one-byte tile stored into a 174 x 94 combined grid. |
| Terrain occupancy probe | `BTECH.EXE.c:19043`, `19244`, `19440`, `19557`, `19651` | Converts world coordinates with `(worldX + 0xac80) / 0x200` and `(24000 - worldZ) / 0x200`, then scans a 5 x 5 cell neighborhood for `rawValue != 0`. These probes return as soon as any non-zero GRD cell is found. |
| Terrain overlay raster | `BTECH.EXE.c:31498` and equivalent expanded block near `33042` | Reads the raw byte for screen-space ground raster cells. If non-zero, uses `rawValue >> 5`, clamped to `0..3`, as an index into a terrain palette/material table at `terrainPaletteBase + band * 2 + 0x1f60`. |
| Sampled line-height lookup | `BTECH.EXE.c:11950..11988` / `FUN_1000_5559`; EXEPACK-unpacked raw body `0x5565..0x565e` | Uses strict bounds and the coordinate constants (`0xac80`, `24000`, `0x200`) to convert a world point into grid-space. Raw instructions `0x561d..0x5632`, omitted by the current decompile, read `gridX * 0x5e + gridZ - 0x7fda`, zero-extend the GRD byte and return `rawValue << 4`. `FUN_1000_5206` compares that result with its interpolated line Y. |

The occupancy probe family around `BTECH.EXE.c:19081..19680` has several
near-duplicate entry points (`FUN_1000_972a`, `FUN_1000_97a0`,
`FUN_1000_98c0`, `FUN_1000_9939`). They all do the same essential terrain
test after world-to-grid conversion:

```text
grid_x = (worldX + 0xac80) / 0x200
grid_y = (24000 - worldZ) / 0x200
scan grid_x-2..grid_x+2 and grid_y-2..grid_y+2
return true on first rawValue != 0
```

Some variants store the first hit's local 5 x 5 offset in stack locals before
returning. None of the sampled variants separates `rawValue & 0x1f` or uses
the raw byte magnitude as a height/tilt value.

The raw xref list also contains apparent calls to these functions near
`BTECH.EXE.c:35748` and `40188..40319`. Those are a decompiler artifact /
signature collision: the surrounding code is keyboard/BIOS-style command
handling and expanded far-call glue, not terrain probing. Treat the direct
GRD reads above, plus the `0xac80` / `24000` / `0x200` coordinate constants, as
the reliable terrain evidence.

## Caller classification

The useful direct callers currently split into two independent terrain roles:

```text
SNARIO/TILE loader
  -> fills combined 174 x 94 GRD byte buffer

3D ground overlay raster
  -> reads raw byte per projected ground cell
  -> non-zero cell draws a dotted/hatched terrain overlay
  -> visual band = min(3, raw >> 5)

movement / object-space probes
  -> projects world point or path samples into GRD grid
  -> scans a 5 x 5 neighborhood
  -> returns blocked/occupied on raw != 0
```

`5206 -> 5559` proves one height use of the complete raw byte: sampled line
clearance compares `rawValue << 4` with interpolated Y. No confirmed caller
yet derives camera pitch or mech pitch from that value. If the screenshot-
observed grade effect is real, it is produced by another path which still
needs naming.

## Current interpretation

The old extractor's `possible_fixed_records record_size=3` guess is not a runtime semantic. The game treats each GRD sample as one byte.

The upper three bits have confirmed behavior:

```text
band = min(3, rawValue >> 5)
```

That band is used by the original raster path, but current side-by-side cockpit captures show the C++ viewer should not turn it into separate visible MFD blobs or standalone clean-ground overlays. Treat it as a diagnostic render-band signal until the surrounding raster conditions are understood.

The lower five bits do not yet have a separately named material/slope meaning.
They nevertheless contribute numerically to the proved `rawValue << 4`
height used by `5206 -> 5559`. Collision-style probes still test only
`rawValue != 0`, while rendering uses `rawValue >> 5`.

## Scenario-2 screenshot hypothesis

User captures for scenario 2 suggest a visible cockpit pitch/grade effect around non-zero GRD regions: one side increases visible sky and the other side increases visible ground. This keeps "non-zero GRD patch participates in original grade/pitch behavior" as a gameplay hypothesis without implying that the patch itself is normal visible ground art.

However, the exact formula has not been found yet. Do not convert the C++ terrain mesh into a guessed heightmap and do not apply camera/mech pitch from `detailBits` until another xref, a controlled runtime capture, or a stronger decompile pass proves the grade formula.

`terrain_grd_patch_signal.tsv` now makes those screenshot targets repeatable at
the patch level. For scenario 2, the strongest lower-right patch is:

```text
patch_id=0 cells=582 bbox=118,54..142,87 centroid=129.6,71.1 raw=1..153
```

The earlier probe `124,69` is inside this connected patch.

## Safe C++ behavior for now

- Keep `TerrainMesh` flat.
- Preserve `rawValue`, `colorBand`, and `detailBits` in mesh vertices.
- Keep `debug` color mode as a signal visualization, not as a claimed original raster.
- Treat `rawValue != 0` as the proven 5 x 5 movement occupancy signal and
  `rawValue << 4` as the proven sampled `5206` line-height operand.
- Treat `rawValue >> 5` as the only proven visual band signal.
