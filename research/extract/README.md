# Extractor outputs

This directory contains generated extractor maps, decoded payloads, rendered
graphics previews, and synthetic smoke-test output.

- `maps/`: per-file JSON/CSV maps generated from the user's local `Original` directory.
- `probe/`: small probe maps for selected original files used while validating parser behavior.
- `summary/`: aggregate CSV/Markdown summaries generated from the per-file maps.
- `synthetic_smoke/`: synthetic test data created to verify extraction without copying original game resources.
- `btech_bin_samples/`: small decoded samples and classification notes for BTECH tagged resources.
- `decoded_pics_bin/`: decoded MW_*PICS.BIN nibble-RLE streams.
- `pics_bin_entries/`: extracted mapped entries from MW_*PICS.BIN files.
- `rendered_btech_bmp_png_display/`: current PNG previews for BTECH tagged BMP resources.
- `rendered_btech_scr_png_display/`: current PNG previews for BTECH tagged SCR resources.
- `rendered_fnt_png_display/`: current PNG glyph sheets for raw/tagged FNT resources.
- `fnt_export/`: current full FNT exports: payload copies, manifests, glyph CSV/JSONL tables, and glyph sheets.
- `rendered_pics_bin_png_display/`: current PNG previews for mapped MW_*PICS.BIN entries.
- `rendered_pics_raw_png_display/`: current PNG previews for raw/full-screen PICS candidates.
- `palettes/`: generated JSON/CSV descriptions and swatches for tagged `.PAL` files.
- `palette_render_probe/`: generated comparison renders for checking PAL:EGA bank behavior.
- `_obsolete/`: archived temporary outputs superseded by current renderer results.

Do not commit extracted original game payloads here. Use `mw_extract extract-tagged` only on local user-supplied files and keep extracted payloads out of source control.

Useful commands:

```powershell
python .\tools\mw_extract.py map .\Original --out .\research\extract\maps
python .\tools\mw_extract.py summarize-maps .\research\extract\maps --out .\research\extract\summary
python .\tools\mw_extract.py scan .\Original\BAT.BMP --json out.json --csv out.csv --no-heuristics
python .\tools\mw_extract.py extract --map .\research\extract\synthetic_smoke\synthetic.map.json --out .\research\extract\synthetic_smoke\out_from_map
python .\tools\mw_extract.py render-bmp .\Original\BAT.BMP --out .\research\extract\rendered_btech_bmp_png_display --image-format png --display-scale
python .\tools\mw_extract.py render-pics-bin .\Original\MW_CPICS.BIN --out .\research\extract\rendered_pics_bin_png_display --image-format png --display-scale
python .\tools\mw_extract.py describe-pal .\Original\DESERT.PAL --json .\research\extract\palettes\DESERT.palette.json --csv .\research\extract\palettes\DESERT.palette.csv --swatches .\research\extract\palettes\swatches --image-format png
python .\tools\mw_extract.py render-bmp .\Original\BAT.BMP --out .\research\extract\palette_render_probe\DESERT_bank1 --palette .\Original\DESERT.PAL --palette-bank 1 --image-format png --display-scale
python .\tools\mw_extract.py render-fnt .\Original\8X8B.FNT --out .\research\extract\rendered_fnt_png_display\8X8B --image-format png --glyph-scale 6
python .\tools\export_fnt.py ".\Sorted Original Files\FNT" --out .\research\extract\fnt_export --image-format png --force
python .\tools\analyze_palette_screenshot_fit.py
python .\tools\match_btech_combat_templates.py --screenshot image0028.png --screenshot image0029.png --screenshot image0030.png --screenshot image0031.png --screenshot image0032.png --screenshot image0033.png --top 5 --min-area 60000 --max-area 64000 --out .\research\analysis\btech_combat_scr_matches.csv
python .\tools\check_graphics_coverage.py --screenshot MW1FinalPic.jpg --top 40 --out .\research\analysis\mw1_finalpic_coverage_matches.csv
python .\tools\build_resource_catalog.py
```

`extract` defaults to `tagged_chunk` entries only. Use explicit `--kind` filters or `--all` for reviewed heuristic maps.
