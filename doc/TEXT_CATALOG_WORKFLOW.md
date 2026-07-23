# MW_MAIN text catalog workflow

This workflow keeps all text identification data outside `Original/MW_MAIN.EXE`.

## Files

- `tools/text_catalog_builder.py` - command-line catalog builder.
- `tools/text_catalog_viewer.html` - local browser viewer/editor for assigning ids.
- `research/analysis/mw_main_text_catalog.json` - generated machine-readable catalog.
- `research/analysis/mw_main_text_catalog.csv` - generated spreadsheet-friendly index.

## Build the initial catalog

```powershell
python tools/text_catalog_builder.py
```

Default input:

```text
Original/MW_MAIN.EXE
```

Default outputs:

```text
research/analysis/mw_main_text_catalog.json
research/analysis/mw_main_text_catalog.csv
```

## Assign ids

Open `tools/text_catalog_viewer.html` in a browser, load `Original/MW_MAIN.EXE`, then assign stable ids in the `Assigned ID` field.

The suggested ids use the file offset, for example:

```text
mw_main.news.01ead2
mw_main.story.013842
mw_main.pm.0183ca
```

These ids are stable as long as the original executable bytes stay the same. Semantic ids can be assigned manually when a clearer engine-facing name is useful.

## Export annotations

Use `Export annotations` from the viewer. The exported JSON can later be merged back into a regenerated catalog:

```powershell
python tools/text_catalog_builder.py --annotations path\to\mw_main_text_annotations.json
```

## Engine usage

Engine code should reference `assigned_id` when present. If `assigned_id` is empty, use `suggested_id`.

Large game text blocks must stay sourced from `Original/MW_MAIN.EXE` at runtime instead of being copied into engine code as C++ string literals. Use the catalog id as the engine-facing reference, then store the verified `file_offset_hex`, byte length, and expected line count beside that reference. The renderer should read the bytes from `MW_MAIN.EXE`, split original `0x0D` line separators, and validate the block shape before drawing it.

Current proven pattern:

```text
id: mw_main.endgame.020c12
file_offset_hex: 0x020C12
length: 271
line_count: 7
source: Original/MW_MAIN.EXE
engine use: campaign intro message after authorization
```

NEWS NET first-slice pattern:

```text
source: Original/MW_MAIN.EXE
engine use: NEWS NET articles loaded by verified offset/length metadata
current rule: unlock by campaign date, display only the last seven messages
confirmed range: early article set through APRIL 3025
```

Personal messages are cataloged near the same text region, but do not yet use
the simple article-date rule. For example, birthday messages are present in the
catalog and original EXE, but are intentionally not wired into NEWS NET until
their original delivery conditions are confirmed.

After adding or replacing a large text block, run `.\build.ps1` and verify that the replacement executable does not contain the prose as an embedded literal. The original executable should contain the phrase at the catalog offset; `mw_main_recomp.exe` should only contain the catalog id/reference metadata.

Every block preserves:

- `file_offset_hex` - offset in `MW_MAIN.EXE`;
- `loaded_offset_hex` - offset after subtracting the MZ header;
- `category` - current best grouping;
- `title`, `tags`, `notes` - editable catalog metadata;
- `text` - extracted ASCII text with original control characters preserved.

## Review notes

Most long narrative records are clean `0x0D`-separated ASCII blocks. Some dense UI tables, especially around the mech complex, contain display/control bytes adjacent to text. Keep those records in the catalog, but mark them reviewed and add notes before treating them as final engine strings.

Current high-level categories include:

```text
loader_prompt, combat_warning, intro_text, mission_result, credits_auth,
company_status, mechlab_ui, contract_ui, starmap_planet, mission_name,
crew_recruitment, pilot_bio, ui_save_hire, rumor, main_story,
personal_message, newsnet, reputation_flavor, brief_headline,
newsnet_endgame, endgame
```
