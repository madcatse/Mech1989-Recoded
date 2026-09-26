# Terrain palette/biome signal notes

Date: 2026-08-10.

Scope: Phase 5 terrain/battlefield resource import only. This note records the
current evidence for selecting desert/green/snow combat terrain presentation
without changing terrain parsing, `mw_legacy3d::GpuBatch`, or Python golden
validators.

## Current conclusion

`SNARIO.DAT` does not yet contain a proven palette, biome, or map-scale field.
The active terrain records prove scenario tile ids and terrain mode only. The
best current lead for combat biome selection is the normalized campaign/combat
environment id, not the active `SNARIO.DAT` record.

This should keep the C++ terrain tools conservative:

- keep `debug` and `flat` as explicit inspection modes;
- keep `desert`, `green`, and `snow` as provisional user-selected presentation
  modes;
- do not auto-select a palette/biome from `SNARIO.DAT` until a runtime handoff
  or decompile xref proves it;
- do not use unresolved `SNARIO.DAT` bytes as map scale.

## Evidence

### BTECH loads and selects combat palette resources

`research/analysis/btech_resource_usage.md` records these BTECH combat asset
names:

```text
0x027071 desert.pal
0x02707C arctic.pal
0x027087 tropic.pal
0x027092 dmgpal.pal
```

The files exist as terrain-specific palette containers:

```text
Original/DESERT.PAL
Original/ARCTIC.PAL
Original/TROPIC.PAL
Original/DMGPAL.PAL
Sorted Original Files/PAL/*.PAL
```

`research/analysis/pal_ega_findings.md` and
`Sorted Original Files/PAL/pal_extract/README.md` describe their shared
`PAL:`/`EGA:`/`CGA:` tagged structure. The EGA payload is organized as four
16-color banks. Current screenshot evidence still uses standard EGA RGB colors
after capture/downscale, so the exact terrain PAL bank/runtime remap remains
unproven.

`research/terrain_palette_runtime_xrefs.md` records the current BTECH runtime
xrefs. Battle initialization loads all four PAL files into handles, then
`FUN_1000_270a` applies:

```text
*(u16 *)(0xd1fc + 2 * environment_id)
```

where the handle table resolves as:

```text
0 -> DESERT.PAL
1 -> TROPIC.PAL
2 -> ARCTIC.PAL
```

BTECH derives `environment_id` from byte `2` of the combat context and clamps
values above `2` to `2`. This means the combat context must contain a normalized
environment id, not the raw planet terrain code.

### Campaign planet records already have environment mapping

`docs/STARMAP_INTEGRATION.md` documents the planet terrain-code mapping:

```text
terrain_code % 3 == 1 -> Desert
terrain_code % 3 == 2 -> Tropical
terrain_code % 3 == 0 -> Ice
```

The current C++ campaign UI mirrors that mapping in `src/main.cpp`:

```text
environmentForTerrain(terrainCode) -> Desert / Tropical / Ice
environmentLabel(...) -> DESERT / TROPICAL / ICE
```

This is now the strongest source for the combat biome/palette choice: when
BTECH is launched for a contract, the mission context likely passes the current
planet environment as the normalized combat-context byte `2`.

### Active SNARIO tail bytes do not correlate cleanly

`screenshots/terrain/generated/snario_scale055/terrain_snario_signal.tsv`
records the active eight terrain scenarios:

```text
scenario 2  mode 3  tail 2,3,0,0,3,1,0,3,3,2,4,4
scenario 3  mode 1  tail 0,2,0,0,1,2,3,3,3,0,8,4
scenario 4  mode 3  tail 0,2,3,0,3,3,3,2,3,2,8,8
scenario 5  mode 1  tail 0,2,2,0,1,1,3,3,3,0,8,4
scenario 15 mode 2  tail 1,3,3,3,2,2,2,0,0,1,4,8
scenario 17 mode 1  tail 1,0,0,0,1,0,3,3,1,2,8,4
scenario 18 mode 1  tail 2,0,3,2,1,3,3,0,0,0,8,8
scenario 19 mode 2  tail 1,3,3,0,2,0,0,0,1,3,4,8
```

Byte 8 is the proven terrain mode. Bytes 14 and 15 narrow to `4|8` in the
active records, but the combinations recur across different terrain modes and
do not identify desert/tropical/ice or map scale. The remaining bytes are still
unresolved.

## Working mapping for provisional tools

The dump/viewer presentation modes should remain explicit:

```text
debug   raw-GRD inspection colors
flat    uniform ground inspection
desert  screenshot-informed desert/sand presentation
green   screenshot-informed tropical/green presentation
snow    screenshot-informed ice/snow presentation
```

If a future combat-launch path passes planet environment into the 3D engine, the
runtime-backed mapping is:

```text
Desert   -> DESERT.PAL -> terrain-color desert
Tropical -> TROPIC.PAL -> terrain-color green
Ice      -> ARCTIC.PAL -> terrain-color snow
Damage   -> DMGPAL.PAL -> damage/status remap, not a biome
```

The environment-to-PAL-file mapping is now supported by BTECH xrefs. The exact
PAL bank and runtime palette application still need screenshot/decompile
confirmation.

## Next proof targets

1. Trace how the current planet/environment is passed from campaign state into
   the BTECH combat context byte `2`.
2. Compare clean desert, tropical, and ice combat screenshots against PAL bank
   variants to identify the exact terrain remap.
3. Keep `SNARIO.DAT` tail bytes in the report, but avoid assigning palette or
   scale semantics until a direct use is found.
