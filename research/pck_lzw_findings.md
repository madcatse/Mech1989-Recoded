# PCK LZW and decoded index findings

Date: 2026-06-25.

## Confirmed outer format

Files `*PCK.TBL` and `TERPCK.GI` start with a 5-byte compression header:

- `u8 codec_id`: confirmed value `0x02`.
- `u32le decoded_size`: total byte size after decompression.

The payload is one or more LZW segments. The base stream matches the already known `BIN` codec 2 family:

- bit order: LSB-first;
- initial code width: 9 bits;
- maximum code width: 12 bits;
- byte literals: `0..255`;
- first dynamic dictionary code: `257`;
- in PCK streams, code `256` terminates the current segment;
- the final segment may end at EOF without code `256`.

Large PCK files contain multiple segments. A short byte gap may appear between segments, so the decoder searches a small window for the next valid LZW chain.

## Implemented CLI support

Implemented in `tools/mw_extract.py`:

- `map-pck`: maps compressed LZW segments.
- `decode-pck`: decodes `*PCK.TBL` / `TERPCK.GI` into one decoded binary stream.
- `map-pck-decoded`: maps the internal index and block candidates inside a decoded PCK stream.
- `extract-pck-decoded`: extracts internal decoded PCK blocks into separate files.
- `map-pck-shapes`: maps tentative PCK shape objects, parts, frame tables, and frame records.
- `map-pck-selected-frames`: selects one frame record per part using the observed game threshold rule.
- `render-pck-selected-probe`: renders a diagnostic selected-frame probe for a given choice/threshold.
- `map-pck-ref-byte-pairs`: maps selected `frame_type` 2/3 refs as byte-pair/local-ref candidates.
- `render-pck-ref-byte-pair-probe`: renders diagnostic placement maps for selected ref candidates.
- `map-pck-resolver-targets`: ranks structural addressing candidates for selected `frame_type` 2/3 refs.
- `render-pck-recursive-ref-probe`: follows selected refs into valid local frame records and renders a diagnostic graph.
- `render-pck-type0-probe`: renders tentative point-cloud probes for `frame_type == 0` records.

Regression coverage is in `tests/test_mw_extract.py`.

Verification:

- `python -m py_compile .\tools\mw_extract.py .\tests\test_mw_extract.py`
- `python -m unittest .\tests\test_mw_extract.py`

Current result: 25 tests OK.

## Generated artifacts

Outer LZW outputs:

- `research/extract/decoded_pck/`
- `research/extract/pck_maps/`
- `research/analysis/pck_lzw_segments.csv`
- `research/analysis/pck_decoded_structure_probe.csv`

Decoded internal maps and extracted blocks:

- `research/extract/pck_decoded_maps/`
- `research/extract/pck_decoded_blocks/`
- `research/analysis/pck_decoded_index_entries.csv`
- `research/analysis/pck_decoded_blocks.csv`
- `research/analysis/pck_decoded_map_summary.csv`
- `research/analysis/pck_shape_command_probe.csv`

Shape-object maps derived from the BTECH disassembly:

- `research/extract/pck_shape_maps/`
- `research/analysis/pck_shape_objects.csv`
- `research/analysis/pck_shape_parts.csv`
- `research/analysis/pck_shape_frame_tables.csv`
- `research/analysis/pck_shape_frame_records.csv`
- `research/analysis/pck_shape_frame_records_renderable.csv`
- `research/analysis/pck_shape_map_summary.csv`
- `research/analysis/pck_shape_renderable_summary.csv`

Tentative `frame_type == 0` probe renders:

- `research/extract/rendered_pck_type0_probe/`
- `research/analysis/pck_type0_probe_contact_sheet.png`

Selected-frame diagnostic probes:

- `research/extract/rendered_pck_selected_probe/`
- `research/analysis/pck_selected_probe_loc_mar_contact_sheet.png`

Referenced shape candidates from `frame_type` 2/3:

- `research/analysis/pck_shape_ref_candidates.csv`

Selected byte-pair/local-ref diagnostics:

- `research/extract/pck_ref_byte_pair_maps/`
- `research/extract/pck_ref_byte_pair_maps_all/`
- `research/extract/pck_ref_byte_pair_maps_jenpck/`
- `research/extract/rendered_pck_ref_byte_pair_probe/`
- `research/analysis/pck_ref_byte_pair_selected_all_choice0.csv`
- `research/analysis/pck_ref_byte_pair_selected_all_choice0_groups.csv`
- `research/analysis/pck_ref_byte_pair_selected_all_choice0_summary.csv`
- `research/analysis/pck_ref_byte_pair_jenpck_contact_sheet.png`
- `research/analysis/pck_ref_byte_pair_jenpck_ref_threshold_summary.csv`
- `research/analysis/pck_resolver_targets_jenpck_choice0_top.csv`
- `research/analysis/pck_resolver_targets_jenpck_choice0_summary.csv`
- `research/analysis/pck_recursive_ref_jenpck_object0004_contact_sheet.png`
- `research/analysis/pck_recursive_ref_jenpck_choice0_min0_contact_sheet.png`
- `research/analysis/pck_recursive_ref_jenpck_choice0_frame_type_summary.csv`

Jenner visual validation targets:

- `research/analysis/jenner_visual_targets.md`
- `research/analysis/jenner_screenshot_targets.csv`
- `research/analysis/jenner_screenshot_targets_contact_sheet.png`

## Decoded internal layout candidate

The start of a decoded PCK stream is an index candidate:

- `row_count = u16le(decoded[2:4])`;
- each row is 16 bytes;
- each row contains 4 entries;
- each entry is `u16 selector`, `u16 offset_word`;
- block offset is `offset_word * 16`;
- `offset_word == 0` is treated as a null slot.

This rule maps all 11 decoded PCK streams cleanly:

| File | Rows | Index entries | Blocks |
| --- | ---: | ---: | ---: |
| `BMAPCK_TBL.decoded.bin` | 21 | 84 | 84 |
| `HAMPCK_TBL.decoded.bin` | 21 | 84 | 84 |
| `JENPCK_TBL.decoded.bin` | 30 | 120 | 120 |
| `LOCPCK_TBL.decoded.bin` | 21 | 84 | 84 |
| `MARPCK_TBL.decoded.bin` | 21 | 84 | 84 |
| `OTHPCK_TBL.decoded.bin` | 6 | 24 | 23 |
| `PHAPCK_TBL.decoded.bin` | 30 | 120 | 120 |
| `RIFPCK_TBL.decoded.bin` | 21 | 84 | 84 |
| `SHAPCK_TBL.decoded.bin` | 30 | 120 | 120 |
| `TERPCK_GI.decoded.bin` | 7 | 28 | 28 |
| `TERPCK_TBL.decoded.bin` | 7 | 28 | 28 |

Total extracted internal blocks: 859.

## Current block classification

The block mapper is intentionally conservative. It records:

- estimated size, based on the next referenced block offset;
- reference count from index entries;
- first local `0x80` byte within the first 32 bytes;
- first local `0xFF` byte within the first 32 bytes;
- first 32 bytes as hex;
- entropy.

Most mech PCK blocks are classified as `prefixed_shape_command_candidate`: a short byte prefix appears before a likely command marker. Some start directly with the likely marker and are classified as `shape_command_candidate`.

`TERPCK.GI` differs from mech PCK files: many blocks are currently `unknown_pck_block` or `ff_delimited_command_candidate`, so terrain/ground data probably uses a different internal command layout.

`pck_shape_command_probe.csv` records a focused byte window around the first local `0x80` marker for every extracted block. In `OTHPCK_TBL`, useful small samples show patterns such as:

- direct start: `80 00 3D 00 00 00 ...`;
- one-byte prefix: `FF 80 00 95 01 ...`;
- short literal-like prefix followed by marker: `4C 4D 4E 4F FF 80 00 50 00 ...`.

These windows are the best starting point for the next command-stream decoder.

## Shape object layout candidate

The BTECH disassembly around `FUN_2000_c422` supports the following tentative object layout at the first local `0x80` marker:

- `u8 flags`;
- `u8 palette_or_kind`;
- `u16 base_id`;
- `u16 unknown_04`;
- `u16 unknown_06`;
- `u16 unknown_08`;
- `u16 part_count` at offset `+10`;
- `u16 header_word_0c` at offset `+12`, role still unknown.

Important correction: the part table starts at `object_start + 0x0E`. Earlier notes treated the word at `+0x0C` as `parts_offset`; that was wrong for multi-part combat objects such as LOCPCK/MARPCK candidates and produced bogus frame counts.

Each part record is 8 bytes:

- `u8 transform_index`;
- `u8 palette_or_kind`;
- `u16 coord_table_offset`;
- `u16 frame_choice_count`;
- `u16 frame_table_offset`.

The coordinate record is selected as `object_start + coord_table_offset + transform_index * 6`.
Frame table entries are 4 bytes: `u16 frame_count`, `u16 frames_offset`.
Frame records are 8 bytes. Byte 1 selects the draw routine observed in the disassembly:

- `0`: direct draw helper;
- `1`: `FUN_2000_bfea`;
- `2`: `FUN_2000_c1c2`;
- `3`: `FUN_2000_c2b6`.

The full forensic frame table keeps every decoded record. The renderable subset keeps only records from non-issue parts/tables where `frame_type` is `0..3`.

Renderable frame records found after the part-table correction: 8537.

`TERPCK.GI` still maps to zero shape objects with this layout. Treat it as a separate terrain/GI format.

## Type 0 probe renderer

`frame_type == 0` records are currently interpreted as short vector/model records, not as direct bitmap sprites. The probe renderer reads the three signed 16-bit payload values as a point candidate and draws three orthographic panels:

- XY;
- XZ;
- YZ.

This is deliberately a diagnostic render, not the final game renderer. It is useful because dense objects produce repeatable silhouettes that can be compared with combat screenshots and cockpit/object references.

The latest pass rendered 127 unique PNG probes from decoded PCK files:

| File | Probe images |
| --- | ---: |
| `BMAPCK_TBL.decoded.bin` | 12 |
| `HAMPCK_TBL.decoded.bin` | 19 |
| `JENPCK_TBL.decoded.bin` | 29 |
| `LOCPCK_TBL.decoded.bin` | 8 |
| `MARPCK_TBL.decoded.bin` | 10 |
| `OTHPCK_TBL.decoded.bin` | 1 |
| `PHAPCK_TBL.decoded.bin` | 32 |
| `RIFPCK_TBL.decoded.bin` | 7 |
| `SHAPCK_TBL.decoded.bin` | 1 |
| `TERPCK_GI.decoded.bin` | 0 |
| `TERPCK_TBL.decoded.bin` | 8 |

All manifest paths are unique after fixing image filename generation for source names containing dots, such as `BMAPCK_TBL.decoded.bin`.

## Selected-frame and shape-ref findings

The game loop does not draw every frame record in a frame table. It selects one frame record per part by skipping records while `record.threshold < c8da`; if it reaches the end, it uses the last record. This means all-frame point clouds are useful forensic maps, but they are not what the game draws for one combat view.

`map-pck-selected-frames` and `render-pck-selected-probe` implement this selection rule for a chosen `choice_index` and `threshold`. The selected probe is still diagnostic only: it draws direct `frame_type == 0`/`1` values and marks `frame_type == 2`/`3` references, but it does not yet render the referenced shape payloads.

The combat screenshots confirm that visible BattleMechs are filled bitmap/vector shapes, not the sparse type0 point clouds. The next blocking item is resolving `frame_type` 2/3 references:

- `frame_type 2`: payload bytes are interpreted as `dx`, `dy`, `shape_ref`, `extra_word`.
- `frame_type 3`: payload bytes are interpreted as `dx`, `dy`, `shape_ref`, `transform_index`, `extra_byte`.
- `pck_shape_ref_candidates.csv` currently records 1209 type2/type3 reference records and 333 distinct `shape_ref` values.

Some frequent references look like high unsigned values, for example `0xFF03`, `0xFF04`, and `0xFF05`. Treat these as unresolved handles or signed/local identifiers until `FUN_1000_ae56` is understood; do not assume they are simple decoded-file offsets.

`probe-pck-shape-refs` now tests address hypotheses for every `frame_type` 2/3 `shape_ref`. It writes per-file reports under `research/extract/pck_shape_ref_probes/` and aggregate reports under `research/analysis/`:

- `pck_shape_ref_offset_probe.csv`: all tested candidates.
- `pck_shape_ref_probe_by_source_candidate.csv`: high-score counts by source and addressing hypothesis.
- `pck_shape_ref_probe_by_ref.csv`: high-score counts by `shape_ref`.
- `pck_shape_ref_probe_high_confidence.csv`: top structural matches.

Current aggregate probe totals:

- 1209 `frame_type` 2/3 records.
- 10881 addressing candidates tested.
- 1486 candidates scored `>= 30`.

The strongest repeated match is `shape_ref == 0x0000`, usually through `object_plus_unsigned` / `object_plus_signed`, which points back to the current object header. This is useful, but it also means many high-score rows are self-references rather than newly discovered standalone sprite payloads.

Small signed values such as `0xFF04` and `0xFF05` often land on nearby frame records with `frame_plus_signed` or `part_plus_signed`. Treat those as evidence for a local/relative command encoding, not as proof that the visible mech art has been located.

Locust/Marauder status after adding the new combat screenshots:

- `LOCPCK_TBL` has 153 tested candidates and only 10 high-score structural matches.
- `MARPCK_TBL` has 972 tested candidates and 93 high-score structural matches.
- The diagnostic renders still do not reproduce the filled BattleMech silhouettes from `image0033.png` or `marauder.jpg`.
- The likely missing layer is the `FUN_1000_ae56` resource lookup / drawable-record resolver used before `func_0x00014299`.

See also `btech_combat_render_chain.md` for the current function-chain notes around PCK selection, `FUN_1000_b157`, `FUN_1000_ae56`, and `func_0x00014299`.

`pck_shape_ref_byte_pair_findings.md` adds a stronger hypothesis for many non-offset refs: treat `shape_ref` as two compact bytes. The probe found 182 records with byte-pair score 10, 84 with score 8, and 171 with score 6. `0x0A03` appears in both `LOCPCK_TBL` and `MARPCK_TBL`, making it a useful Locust/Marauder resolver target.

`map-pck-ref-byte-pairs` now applies selected-frame filtering before grouping refs. On `choice_index 0`, `MARPCK_TBL` yields selected refs such as `0x0009`, `0x0808`, `0x0408`, `0x1312`, and `0x1413`; `JENPCK_TBL` yields strong targets around `pck_object_0004` plus `pck_object_0110` / `0x03FF`; `LOCPCK_TBL` yields no selected type2/type3 refs for the tested thresholds. This suggests Locust matching needs a selector/path audit before renderer work.

The Jenner screenshots `image0061.png` through `image0065.png` now provide a better controlled visual target than the earlier internet combat references. A `JENPCK_TBL` scan over `choice_index 0..31` only selected refs for `choice_index 0`; the side/rear facing visible in the screenshots is therefore likely selected outside the current choice-index parameter.

`render-pck-recursive-ref-probe` confirms that some selected Jenner refs do resolve into valid local frame records, mostly short type0/type1 branches. This improves the candidate filter but still does not produce the filled BattleMech silhouette. Continue treating `FUN_1000_ae56` / drawable-record resolution as the missing render layer.

## Next reverse-engineering step

The next extractor layer should focus on the internal shape command stream:

1. Resolve `FUN_1000_ae56` / resource-handle lookup for `shape_ref` values used by `frame_type` 2 and 3.
2. Use the Jenner screenshot set (`image0061.png` through `image0065.png`) and `JENPCK_TBL` as the first controlled visual target; keep `image0033.png`/`LOCPCK_TBL` and `marauder.jpg`/`MARPCK_TBL` as parallel validation targets.
3. Add a real renderer branch for referenced shapes once their payload table is located.
4. Keep selected-frame probes around as diagnostics for threshold/facing behavior.
5. Keep `TERPCK.GI` separate until its terrain/GI layout is understood.

Do not treat the command stream as final until at least one visual match is confirmed.
