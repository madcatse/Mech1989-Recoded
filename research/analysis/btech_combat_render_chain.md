# BTECH combat render chain notes

Status: working notes for the PCK graphics extractor. Do not treat names as final until confirmed against code or a visual match.

## Confirmed call chain candidates

Combat PCK selection:

- `FUN_1000_270a` writes current cockpit/combat state fields and calls `func_0x00015ee2(0x1000, *(u16 *)(*(int *)0x1e86 * 2 + -0x2e04))`.
- The `-0x2e04` expression is now mapped as the runtime PAL handle table at `0xd1fc`: index `0` is `desert.pal`, index `1` is `tropic.pal`, and index `2` is `arctic.pal`. `research/terrain_palette_runtime_xrefs.md` records the direct xrefs.
- `btech_pck_filename_pointer_candidates.csv` records a nearby filename pointer candidate for the eight mech PCK base names.

Frame dispatch:

- `frame_type 2` calls `FUN_2000_c1c2`.
- `frame_type 3` calls `FUN_2000_c2b6`.
- Both call `FUN_1000_ae56(0x1000, shape_ref)` and then pass the returned pointer to `func_0x00014299`.
- `func_0x00014299` is also used for ordinary bitmap resources, so PCK rendering should be modeled as "resolve drawable, then draw drawable", not as direct frame payload pixels.

Drawable object allocation:

- `FUN_1000_b157(param_1, param_2)` allocates a 0x4A-byte drawable/object record from a pool at `0x54CC`.
- It stores `param_1` at record `+2`.
- If `param_1 >= 0`, it calls `FUN_1000_c888(0x1000, param_1)` and stores the returned far pointer pieces at `+4` and `+6`.
- If `param_2` is present, it copies six words to record `+8..+13`.
- It registers the record in tables at `-0x3840` and `0x7762`.

This explains why `FUN_2000_c1c2` / `FUN_2000_c2b6` use fields around `iVar4 + 6` and `iVar4 + 8` for clipping: they receive a resolved drawable record, not raw PCK frame bytes.

Related constructors:

- `FUN_1000_5998(param_1, param_2)` maps a param through tables at `0x232E`, `0xE72`, `0xE74`, and `0xE6E`, then calls `FUN_1000_b157`.
- `FUN_1000_58cd(param_1, param_2, param_3)` creates a moving combat object through `FUN_1000_5998`.
- `FUN_1000_ac03(param_1, param_2)` computes a six-word coordinate/transform record, often used as `param_2` for drawable creation.

`map-btech-combat-tables` now exports these candidate tables to `btech_combat_tables.csv/json`. For some tables the swapped/byte-oriented value is more plausible than the raw little-endian word; keep both interpretations until the resolver is confirmed.

## Current interpretation

The visible BattleMech silhouettes in combat are probably not stored as direct `frame_type 0` point lists. The type2/type3 records likely contain small draw commands that reference drawable records resolved through `FUN_1000_ae56` and the object pools/tables above.

`shape_ref == 0x0000` often resolves structurally to the current object header in the probe reports. This is useful for mapping, but it is not sufficient to render a mech by itself.

Small signed refs such as `0xFF04` and `0xFF05` often land on nearby frame records. Treat them as evidence for local command-relative encoding until the resolver is implemented.

`pck_shape_ref_byte_pair_findings.md` adds a second resolver hypothesis: many non-offset refs may be packed byte pairs. Example: `0x0A03` appears in both `LOCPCK_TBL` and `MARPCK_TBL` and matches byte-oriented BTECH table values.

The Jenner screenshot set gives a cleaner controlled target for the next pass. `JENPCK_TBL` selected refs only for `choice_index 0` in the tested range `0..31`, so the visible side/rear view is probably selected outside the simple frame-choice parameter. The current high-value resolver targets are `pck_object_0004` refs `0x091D`, `0x1C0D`, `0x1D1C`, `0x1B1C`, `0x2D1E`, plus `pck_object_0110` ref `0x03FF` with `transform_index 18`.

`map-pck-resolver-targets` confirms that several Jenner refs land on exact local frame records under signed relative hypotheses. This makes a recursive/local frame resolver the next best implementation target before trying to reproduce the final filled silhouette. Weak refs such as `0x1C0D` and `0x03FF` should stay marked as unresolved command-handle candidates until another base table is found.

`render-pck-recursive-ref-probe` now follows those exact local frame-record hits, but only if the target record has a dispatch-supported `frame_type 0..3`. The resulting Jenner graphs are still sparse: `pck_object_0004` reaches only 3-4 short local edges, while `pck_object_0110` / `0x03FF` has no accepted recursive edge. This is useful negative evidence: the filled combat mech art is probably beyond the local PCK frame table and requires the drawable lookup path.

## Next extractor target

Implement a mapper for the drawable/object pool layout implied by `FUN_1000_b157`, then connect `shape_ref` values to:

1. a PCK-local object or frame command;
2. a generated drawable record;
3. the final `func_0x00014299` draw input.

Visual validation targets:

- `JENPCK_TBL` against `screenshots/image0061.png` through `screenshots/image0065.png` for Jenner.
- `LOCPCK_TBL` against `screenshots/image0033.png` for Locust.
- `MARPCK_TBL` against `screenshots/marauder.jpg` for Marauder.
