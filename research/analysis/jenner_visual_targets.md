# Jenner visual targets

Status: visual validation targets for the PCK graphics extractor.

## Inputs

The screenshots `screenshots/image0061.png` through `screenshots/image0065.png` show Jenner combat views from the side and rear-side angles. They are now tracked as a dedicated visual target set, separate from the older Locust and Marauder references.

Generated target artifacts:

- `jenner_screenshot_targets.csv`
- `jenner_screenshot_targets_contact_sheet.png`
- `jenner_screenshot_targets/`

The CSV records one crop/mask/overlay triplet per screenshot:

| Screenshot | Crop size | Target pixels |
| --- | ---: | ---: |
| `image0061.png` | 324 x 434 | 19227 |
| `image0062.png` | 354 x 428 | 41427 |
| `image0063.png` | 409 x 464 | 84129 |
| `image0064.png` | 299 x 356 | 63108 |
| `image0065.png` | 284 x 332 | 44099 |

The contact sheet is intended for quick visual comparison against extractor probe renders. The crops are not final segmentation masks; they preserve enough surrounding pixels to keep stance, facing, and screen scale visible.

## JENPCK selected reference pass

`JENPCK_TBL.decoded.bin` was scanned with `map-pck-ref-byte-pairs` for `choice_index` values `0..31` and thresholds `0/32/64/96/128/160/192/224`.

Only `choice_index 0` produced selected `frame_type` 2/3 references in this scan. Choices `1..31` produced zero selected refs for the tested thresholds. This means the visible side/rear rotation in the screenshots is probably controlled by another selector or by code before the PCK frame-choice lookup; do not treat `choice_index` as the final facing selector yet.

Choice `0` selected-reference counts:

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

Unique selected references:

| Object | Type | `shape_ref` | Key | Placement | Thresholds |
| --- | ---: | --- | --- | --- | --- |
| `pck_object_0004` | 2 | `0x0000` | `00:00` | `dx=-127, dy=0` | `0;32` |
| `pck_object_0004` | 2 | `0x0707` | `07:07` | `dx=-127, dy=7` | `0;32;64;96;128;160` |
| `pck_object_0004` | 2 | `0x091D` | `1D:09` | `dx=41, dy=41` | `0;32;64;96;128;160;192;224` |
| `pck_object_0004` | 2 | `0x1C0D` | `0D:1C` | `dx=35, dy=35` | `0;32;64;96;128;160;192;224` |
| `pck_object_0004` | 2 | `0x1D1C` | `1C:1D` | `dx=27, dy=27` | `0;32;64;96;128;160;192;224` |
| `pck_object_0004` | 2 | `0x1B1C` | `1C:1B` | `dx=32, dy=32` | `64;96;128;160;192;224` |
| `pck_object_0004` | 2 | `0x2D1E` | `1E:2D` | `dx=44, dy=44` | `192;224` |
| `pck_object_0038` | 3 | `0x0000` | `00:00` | `dx=-12, dy=1, transform=0` | `160;192;224` |
| `pck_object_0042` | 3 | `0x0000` | `00:00` | `dx=-12, dy=1, transform=0` | `160;192;224` |
| `pck_object_0110` | 3 | `0x03FF` | `FF:03` | `dx=9, dy=0, transform=18` | `32;64;96;128;160;192;224` |

Generated JENPCK probe artifacts:

- `pck_ref_byte_pair_choice_threshold_scan_jenpck.csv`
- `pck_ref_byte_pair_selected_jenpck.csv`
- `pck_ref_byte_pair_selected_jenpck_groups.csv`
- `pck_ref_byte_pair_jenpck_threshold_transitions.csv`
- `pck_ref_byte_pair_jenpck_ref_threshold_summary.csv`
- `pck_ref_byte_pair_jenpck_contact_sheet.png`
- `research/extract/rendered_pck_ref_byte_pair_probe/JENPCK_TBL/`

Generated resolver-target artifacts:

- `pck_resolver_targets_jenpck_choice0_top.csv`
- `pck_resolver_targets_jenpck_choice0_summary.csv`
- `research/extract/pck_resolver_targets/JENPCK_TBL/`

Generated recursive-ref probe artifacts:

- `pck_recursive_ref_jenpck_object0004_contact_sheet.png`
- `pck_recursive_ref_jenpck_object0004_summary.csv`
- `pck_recursive_ref_jenpck_choice0_min0_contact_sheet.png`
- `pck_recursive_ref_jenpck_choice0_min0_summary.csv`
- `pck_recursive_ref_jenpck_choice0_frame_type_summary.csv`
- `research/extract/rendered_pck_recursive_ref_probe/JENPCK_TBL/`
- `research/extract/rendered_pck_recursive_ref_probe/JENPCK_TBL_min0/`

Top structural candidates from `pck_resolver_targets_jenpck_choice0_summary.csv`:

| Object | Type | `shape_ref` | Best candidate | Target | Score | Evidence |
| --- | ---: | --- | --- | --- | ---: | --- |
| `pck_object_0004` | 2 | `0x0000` | `object_plus_signed` | `0x0005E6` | 75 | object header/self marker |
| `pck_object_0004` | 2 | `0x0707` | `frame_plus_signed` | `0x000F75` | 35 | exact frame record |
| `pck_object_0004` | 2 | `0x091D` | `block_plus_signed` | `0x000EFD` | 35 | exact frame record |
| `pck_object_0004` | 2 | `0x1B1C` | `object_plus_signed` | `0x002102` | 45 | exact frame record plus drawable-like extents |
| `pck_object_0004` | 2 | `0x1C0D` | `block_plus_signed` | `0x0021ED` | 5 | weak frame-prefix-only match |
| `pck_object_0004` | 2 | `0x1D1C` | `object_plus_signed` | `0x002302` | 40 | exact frame record |
| `pck_object_0004` | 2 | `0x2D1E` | `frame_plus_signed` | `0x00359C` | 30 | exact frame record |
| `pck_object_0038` | 3 | `0x0000` | `object_plus_signed` | `0x00295C` | 105 | object/frame self marker |
| `pck_object_0042` | 3 | `0x0000` | `object_plus_signed` | `0x00340C` | 105 | object/frame self marker |
| `pck_object_0110` | 3 | `0x03FF` | `block_plus_signed` | `0x00884F` | 5 | weak frame-prefix-only match |

## Resolver targets

The next renderer pass should focus on these JENPCK records first:

1. `pck_object_0004`, `frame_type 2`, refs `0x091D`, `0x1C0D`, `0x1D1C`, `0x1B1C`, and `0x2D1E`.
2. `pck_object_0110`, `frame_type 3`, ref `0x03FF`, `transform_index 18`.
3. Late-threshold type3 records in `pck_object_0038` and `pck_object_0042`.

The byte-pair diagnostic image is intentionally sparse and does not yet draw the Jenner silhouette. Its current value is narrowing the set of resolver inputs that need to become real drawable records.

The resolver-target report suggests a practical split: implement recursive/local frame-record resolution first for the medium/high-score `pck_object_0004` refs, then investigate weak refs such as `0x1C0D` and `0x03FF` as possible command handles rather than direct offsets.

## Recursive local-ref probe

`render-pck-recursive-ref-probe` follows selected `frame_type` 2/3 refs when the best structural candidate lands on another valid frame record (`frame_type 0..3`). This deliberately avoids unsupported frame types, because a false exact-offset match to another record boundary is not enough to render.

For `JENPCK_TBL` / `pck_object_0004`, the recursive probe produces short chains:

| Thresholds | Nodes | Resolved edges | Notes |
| --- | ---: | ---: | --- |
| `0;32` | 8 | 3 | selected type2 roots plus short type0/type1 branches |
| `64;96;128;160` | 9 | 4 | one extra local frame branch appears |
| `192;224` | 8 | 3 | one earlier branch is no longer accepted after filtering unsupported target types |

With `min_score 0`, late thresholds also show `pck_object_0038` and `pck_object_0042` branches, but they resolve into four short type0 branches each. `pck_object_0110` still has no accepted recursive edge, so `0x03FF` remains a likely command/table handle rather than a direct local frame offset.

Conclusion: local frame-record recursion is real and useful for pruning false candidates, but it still does not produce a filled Jenner silhouette. The missing layer is probably the drawable/object resolver behind `FUN_1000_ae56` / `func_0x00014299`, not just another local frame-record chain.
