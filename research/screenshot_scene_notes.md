# Screenshot scene notes

These notes are visual anchors for comparing DOSBox screenshots with extracted resources. They are intentionally marked as candidates, not final format proof.

## Stable Backgrounds

- `screenshots/image0002.png`, `image0003.png`, and `image0008.png` visually align with `MW_PICS/raw_00000280_320x200`: desert/Oshika city scene.
- `screenshots/image0015.png` and `image0017.png` visually align with `MW_PICS/raw_00007F89_320x200`: mountain/Benjamin city scene.
- `screenshots/image0011.png` and `image0012.png` visually align with `MW_PICS/raw_0001F6A5_320x200`: bar scene.
- `screenshots/image0004.png`, `image0005.png`, `image0006.png`, and `image0014.png` visually align with `MW_1PICS/entry_002_320x200`: mech complex/hangar scene.
- `screenshots/image0001.png` visually aligns with `MW_TPICS/entry_002_320x200`: cockpit/launch confirmation scene.
- `screenshots/image0013.png` visually aligns with `MW_1PICS/entry_001_320x200`: planet/map screen family.

## Composite UI Screens

The following screenshots appear to be assembled from smaller UI widgets, fonts, portraits, icons, and runtime text rather than a single raw background:

- `screenshots/image0007.png`: mech status screen
- `screenshots/image0009.png`: pilot/mech roster grid
- `screenshots/image0010.png`: news terminal
- `screenshots/image0016.png`: Kurita contract terminal

These should be handled with region or template matching instead of full-frame RMSE.

## Added Screenshots 2026-06-25

- `screenshots/image0018.png`: Activision splash.
- `screenshots/image0019.png`: intro text.
- `screenshots/image0020.png`: MechWarrior title screen.
- `screenshots/image0021.png`: cockpit/launch character screen without dialog.
- `screenshots/image0022.png` and `image0025.png`: Kearny IV city scene family.
- `screenshots/image0023.png` and `image0024.png`: mech status screens.
- `screenshots/image0026.png`: mech complex/hangar scene family.
- `screenshots/image0027.png`: mission briefing/next mission screen.
- `screenshots/image0028.png` through `image0033.png`: combat HUD for player in a light mech.
- `screenshots/image0034.png`: post-mission/result screen.
- `screenshots/MW1FinalPic.jpg`: final ending screen with unique illustration
  and ending text.

## BTECH Combat Anchors

- `screenshots/image0028.png` through `image0033.png` match `LIGHT.SCR` as
  cockpit base. See `research/analysis/btech_combat_scr_matches.csv`.
- Coarse template matching finds `DIGITS`, `HUD_CYAN`, `COCKPIT`, and
  `SM_MECHS` candidates in combat screenshots. See
  `research/analysis/btech_combat_template_matches_small.csv`.

## Missing / Not Yet Extracted

- `screenshots/MW1FinalPic.jpg`: final illustration is not present among current
  rendered PICS/BTECH/FNT outputs. The text is located in `MW_MAIN.EXE`, but the
  image still needs resource-format investigation.

## Next Matching Step

Build a region-based matcher that can:

- downscale DOSBox screenshots from 1600x1200 to 320x200 using nearest-neighbor sampling;
- compare stable background regions separately from dialog/menu regions;
- scan extracted small sprites and UI widgets against screenshots as templates;
- record candidate matches with coordinates, score, and source resource path.
