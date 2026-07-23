# Template matching notes

Template matching is an exploratory aid for connecting extracted sprites/widgets to DOSBox screenshots.

## Scripts

- `tools/match_graphics_templates.py`
  - scans extracted unscaled PPM templates against screenshots downscaled to 320x200;
  - applies the same EGA source-to-game palette remap used by PNG preview rendering;
  - writes candidate coordinates in both 320x200 and 1600x1200 coordinate spaces.
- `tools/build_template_match_sheet.py`
  - builds a visual sheet from a candidate CSV;
  - shows the screenshot crop next to the extracted template.

## Generated files

- `research/analysis/template_match_candidates.csv`
- `research/analysis/template_match_portrait_candidates.csv`
- `research/analysis/template_match_portrait_sheet.png`

## Current confirmed/strong candidate

The portrait matcher found one exact low-score match in the current screenshot set:

| screenshot | template | x_320 | y_320 | score |
|---|---|---:|---:|---:|
| `screenshots/image0009.png` | `research/extract/rendered_pics_bin/MW_CPICS/entry_022_68x68.ppm` | 44 | 0 | 0.0000 |

This aligns with the pilot portrait visible in the roster/grid screen.

## Limitations

- General matching is noisy because many UI border pieces and icons are repeated across screens.
- Coarse matching uses a reduced grid for speed, so it is intended for triage, not final proof.
- Runtime text, dialog boxes, and animation states cause many false positives.
- Portrait matching currently works best when the screenshot contains the extracted portrait at the same scale and without heavy overlay.

## Recommended next refinement

Split templates into resource classes before matching:

- portraits: `MW_CPICS`, mostly 68x68;
- large backgrounds: 320x200 raw/full-screen candidates;
- side icons and buttons: `MW_GPICS`;
- UI frames/borders: thin repeated widgets, handled separately or ignored for early matching;
- BTECH combat UI: codec 2 BMP/SCR resources once decoded.
