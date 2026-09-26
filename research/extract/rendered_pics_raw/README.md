# Rendered raw PICS records

These files are generated research artifacts from `mw_extract render-pics-raw` and `scan-pics-raw`.

Confirmed/selected renders:
- `MW_PICS` `0x00000280`: 320x200 -> `research/extract/rendered_pics_raw/MW_PICS/raw_00000280_320x200.ppm`
- `MW_PICS` `0x00007F89`: 320x200 -> `research/extract/rendered_pics_raw/MW_PICS/raw_00007F89_320x200.ppm`
- `MW_PICS` `0x0000FC92`: 320x200 -> `research/extract/rendered_pics_raw/MW_PICS/raw_0000FC92_320x200.ppm`
- `MW_PICS` `0x0001F6A5`: 320x200 -> `research/extract/rendered_pics_raw/MW_PICS/raw_0001F6A5_320x200.ppm`
- `MW_PICS_selected` `0x00000280`: 320x200 -> `research/extract/rendered_pics_raw/MW_PICS_selected/raw_00000280_320x200.ppm`
- `MW_PICS_selected` `0x00007F89`: 320x200 -> `research/extract/rendered_pics_raw/MW_PICS_selected/raw_00007F89_320x200.ppm`
- `MW_PICS_selected` `0x0000FC92`: 320x200 -> `research/extract/rendered_pics_raw/MW_PICS_selected/raw_0000FC92_320x200.ppm`
- `MW_PICS_selected` `0x0001799B`: 196x28 -> `research/extract/rendered_pics_raw/MW_PICS_selected/raw_0001799B_196x28_hflag_8000.ppm`
- `MW_PICS_selected` `0x0001F6A5`: 320x200 -> `research/extract/rendered_pics_raw/MW_PICS_selected/raw_0001F6A5_320x200.ppm`
- `MW_PICS_selected` `0x0002A320`: 273x16 -> `research/extract/rendered_pics_raw/MW_PICS_selected/raw_0002A320_273x16.ppm`
- `MW_PICS_selected` `0x0002A470`: 273x16 -> `research/extract/rendered_pics_raw/MW_PICS_selected/raw_0002A470_273x16.ppm`
- `MW_PICS_selected` `0x0002A5C0`: 273x16 -> `research/extract/rendered_pics_raw/MW_PICS_selected/raw_0002A5C0_273x16.ppm`

Candidate scans:
- `MW_PICS_candidates.csv`: broad heuristic scan output; contains false positives.
- `MW_PICS_candidates_interesting.csv`: filtered candidate subset for manual review.

Interesting candidate rows: 118

Known caveat:
The scan is heuristic. Any candidate must be visually checked or connected to runtime code before it is treated as a confirmed resource entry.
