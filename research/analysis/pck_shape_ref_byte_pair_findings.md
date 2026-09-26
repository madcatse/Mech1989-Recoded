# PCK shape_ref byte-pair findings

Status: hypothesis, not final format.

## Inputs

- `pck_shape_ref_candidates.csv`: 1209 `frame_type` 2/3 records.
- `btech_combat_tables.csv`: BTECH combat table candidates, including byte-oriented interpretations of tables around DS `0x232E`, `0x2342`, and `0x0E6E`.

Generated reports:

- `pck_shape_ref_byte_pair_probe.csv`
- `pck_shape_ref_byte_pair_summary.csv`
- `pck_ref_byte_pair_selected_all_choice0.csv`
- `pck_ref_byte_pair_selected_all_choice0_groups.csv`
- `pck_ref_byte_pair_selected_all_choice0_summary.csv`
- `pck_ref_byte_pair_selected_loc_mar.csv`
- `pck_ref_byte_pair_selected_loc_mar_groups.csv`
- `pck_ref_byte_pair_choice_threshold_scan_loc_mar.csv`
- `pck_ref_byte_pair_choice_threshold_scan_jenpck.csv`
- `pck_ref_byte_pair_selected_jenpck.csv`
- `pck_ref_byte_pair_selected_jenpck_groups.csv`
- `pck_ref_byte_pair_jenpck_ref_threshold_summary.csv`
- `pck_resolver_targets_jenpck_choice0_top.csv`
- `pck_resolver_targets_jenpck_choice0_summary.csv`

## Why this matters

Many `shape_ref` values do not behave like simple 16-bit offsets. Frequent values such as `0x0A03`, `0x0808`, `0x0408`, `0xFF04`, and `0xFF05` look more like compact command fields or byte pairs.

The byte-pair probe compares:

- low byte of `shape_ref`;
- high byte of `shape_ref`;
- swapped/byte-oriented values from BTECH tables;
- drawable record candidate indexes.

This does not prove the final layout, but it gives a stronger model than direct offset probing.

## Current results

Score distribution across 1209 refs:

| Byte-pair score | Records |
| ---: | ---: |
| 10 | 182 |
| 8 | 84 |
| 6 | 171 |
| 5 | 216 |
| 4 | 62 |
| 3 | 102 |
| 2 | 111 |
| 1 | 177 |
| 0 | 104 |

Strong examples:

| `shape_ref` | Count | Notes |
| --- | ---: | --- |
| `0x0000` | 145 | likely self/current-object marker in many contexts |
| `0x0004` | 15 | strong byte-pair/table match |
| `0x0F0F` | 7 | strong byte-pair/table match |
| `0x0404` | 4 | strong byte-pair/table match |
| `0x0A0A` | 4 | strong byte-pair/table match |
| `0x0A03` | 2 | appears in `LOCPCK_TBL` and `MARPCK_TBL`; useful Locust/Marauder target |

For `LOCPCK_TBL`, `pck_object_0051` has `frame_type 3`, `shape_ref 0x0A03`, low byte `3`, high byte `10`, score `8`.

For `MARPCK_TBL`, `shape_ref 0x0A03` also appears, along with repeated `0x0004`, `0x0404`, `0x0008`, `0x0408`, and `0x0605` patterns.

## Selected-frame byte-pair pass

`map-pck-ref-byte-pairs` applies the same frame-table threshold selection used by `map-pck-selected-frames`, then keeps only selected `frame_type` 2/3 records.

For `choice_index 0`, thresholds `0/64/128/192`, selected refs appear in:

| Source | Selected refs |
| --- | ---: |
| `BMAPCK_TBL.decoded.bin` | 10 |
| `HAMPCK_TBL.decoded.bin` | 10 |
| `JENPCK_TBL.decoded.bin` | 34 |
| `MARPCK_TBL.decoded.bin` | 25 |
| `PHAPCK_TBL.decoded.bin` | 10 |
| `RIFPCK_TBL.decoded.bin` | 4 |
| `SHAPCK_TBL.decoded.bin` | 28 |
| `TERPCK_TBL.decoded.bin` | 4 |

`LOCPCK_TBL` has no selected `frame_type` 2/3 refs for `choice_index 0` across the tested thresholds. This does not mean LOCPCK has no refs; it means the current selected-frame model is not selecting them. Locust visual matching needs either a different selector or a different object path.

For `MARPCK_TBL`, selected refs include:

- `0x0009`
- `0x0808`
- `0x0408`
- `0x1312`
- `0x1413`
- `0x0008`

The diagnostic renderer `render-pck-ref-byte-pair-probe` outputs colored `dx/dy` placements for selected refs. Current Marauder contact sheet: `pck_ref_byte_pair_marpck_contact_sheet.png`. These renders are sparse placement maps, not mech silhouettes.

## Jenner pass

The newer Jenner combat screenshots `image0061.png` through `image0065.png` were converted into a visual target set:

- `jenner_screenshot_targets.csv`
- `jenner_screenshot_targets_contact_sheet.png`
- `jenner_screenshot_targets/`

`JENPCK_TBL` was scanned for `choice_index` values `0..31` and thresholds `0/32/64/96/128/160/192/224`. Only `choice_index 0` produced selected `frame_type` 2/3 refs in this pass; choices `1..31` produced zero. The side/rear rotation in the screenshots is therefore probably controlled by another selector or by code before the current frame-choice model.

For `choice_index 0`, `JENPCK_TBL` selected:

| Threshold | Selected refs | Groups |
| ---: | ---: | ---: |
| 0 | 5 | 5 |
| 32 | 7 | 6 |
| 64 | 7 | 6 |
| 96 | 7 | 6 |
| 128 | 7 | 6 |
| 160 | 15 | 7 |
| 192 | 15 | 7 |
| 224 | 15 | 7 |

The strongest immediate resolver targets are:

- `pck_object_0004`, `frame_type 2`, refs `0x091D`, `0x1C0D`, `0x1D1C`, `0x1B1C`, and `0x2D1E`;
- `pck_object_0110`, `frame_type 3`, ref `0x03FF`, `transform_index 18`;
- late-threshold `frame_type 3` records in `pck_object_0038` and `pck_object_0042`.

Current Jenner contact sheet: `pck_ref_byte_pair_jenpck_contact_sheet.png`. Like the Marauder byte-pair probe, it is a diagnostic placement map, not the final mech renderer.

`map-pck-resolver-targets` now ranks structural addressing candidates for selected refs. For `JENPCK_TBL`, the best current candidates include exact frame-record targets for `0x0707`, `0x091D`, `0x1B1C`, `0x1D1C`, and `0x2D1E`. Refs `0x1C0D` and `0x03FF` currently have weak frame-prefix-only candidates, so they are likely command handles or need another resolver base.

`render-pck-recursive-ref-probe` follows exact local frame-record hits for selected refs. The accepted Jenner chains are short and sparse; unsupported target frame types are filtered out. This confirms that direct local recursion is not enough to reconstruct the visible mech silhouette, but it gives a cleaner boundary for the next resolver stage.

## Working hypothesis

`shape_ref` is probably not one scalar pointer. It may pack two small fields, for example:

- low byte: drawable kind/table selector;
- high byte: resource variant, frame group, range bucket, or local command parameter.

Negative-looking words such as `0xFF04` may still be signed/local commands or sentinel forms. They should be handled separately from ordinary byte-pair refs.

## Next extractor step

Build a resolver probe that treats `shape_ref` as bytes and groups selected frame records by:

- source PCK;
- object id;
- frame type;
- low byte;
- high byte;
- transform index for `frame_type 3`;
- `dx`/`dy` placement.

Then render a diagnostic composition for candidate byte-pair groups instead of plotting only raw type0 points.

Next practical target: implement a local recursive resolver for selected refs whose best candidates land on exact frame records, starting with `JENPCK_TBL` / `pck_object_0004`. After that, trace command-handle cases such as Jenner `0x03FF` and Marauder refs through the constructor/resolver that turns them into drawable records. `LOCPCK_TBL` still needs selector investigation before visual matching.
