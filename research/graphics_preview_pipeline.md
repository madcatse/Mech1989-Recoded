# Graphics preview pipeline

This note documents how current extracted graphics are converted into visual previews that can be compared with DOSBox screenshots.

## Inputs

- `screenshots/*.png`: 17 DOSBox screenshots supplied as 1600x1200 PNG files.
- `graphics_converter/ppm_game_convert.py`: reference converter for 320x200 PPM assets.
- `research/extract/rendered_pics_bin/*.ppm`: previously rendered PICS archive entries.
- `research/extract/rendered_pics_raw/*.ppm`: previously rendered raw `MW_PICS.BIN` candidates.

## Converter behavior adopted in `mw_extract`

`mw_extract` render commands now support:

- `--image-format ppm|png|bmp|all`
- `--game-palette-remap`
- `--display-scale`
- `--xscale`, default `5`
- `--yscale`, default `6`

The defaults preserve the older behavior: render commands still write unscaled PPM unless new options are supplied.

The game-palette remap follows the mapping observed in `graphics_converter/ppm_game_convert.py`:

```text
source EGA index -> game EGA index
0 -> 5
1 -> 0
2 -> 15
3 -> 7
4 -> 8
5 -> 11
6 -> 9
7 -> 1
8 -> 12
9 -> 4
10 -> 10
11 -> 3
12 -> 2
13 -> 14
14 -> 6
15 -> 13
```

`--display-scale` uses nearest-neighbor scaling. With defaults, a 320x200 render becomes 1600x1200, matching the supplied DOSBox screenshots.

## Generated preview artifacts

- `research/analysis/screenshots_inventory.csv`
- `research/analysis/screenshots_contact_sheet.png`
- `research/analysis/rendered_preview_inventory.csv`
- `research/analysis/rendered_pics_bin_contact_sheet.png`
- `research/analysis/rendered_pics_raw_contact_sheet.png`
- `research/analysis/screenshot_fullscreen_matches.csv`
- `research/extract/rendered_pics_bin_png_display/`
- `research/extract/rendered_pics_raw_png_display/`

## Current matching method

`screenshot_fullscreen_matches.csv` compares each DOSBox screenshot against full-screen extracted previews with simple RGB RMSE.

This is useful for triage but not a proof of identity:

- UI overlays, cursor state, text, and animation frames change many pixels.
- Some screenshots are composite game screens rather than pure background images.
- Smaller extracted sprites are not included in the full-frame RMSE pass.
- Exact pixel equality is not expected unless the extracted frame is the whole screen with the same palette and no runtime overlay.

The next stronger comparison should be region-based:

- manually or heuristically crop stable screen regions;
- compare cropped screenshots against extracted background candidates;
- compare small sprites through template matching rather than full-frame RMSE.
