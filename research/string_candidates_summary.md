# String candidates

Curated pass from direct ASCII scanning of suitable files in `Original`.
Rows are candidates for reverse engineering, not final string-table definitions.

Candidate rows: 525
Files with candidates: 50

By kind:
- `resource chunk tag`: 92
- `loader/error message`: 91
- `ui/menu/economy message`: 66
- `installer/script text`: 64
- `resource filename`: 59
- `installer file reference`: 37
- `source art filename`: 36
- `world/faction/planet text`: 35
- `contract/mission text`: 31
- `save/player text`: 14

By extension:
- `.EXE`: 330
- `.BAT`: 110
- `.BMP`: 51
- `.GAM`: 14
- `.PAL`: 12
- `.SCR`: 5
- `.FNT`: 2
- `.SND`: 1

Top files:
- `Original/MW_MAIN.EXE`: 179
- `Original/INSTALL.BAT`: 110
- `Original/BTECH.EXE`: 90
- `Original/MW_CPICS.EXE`: 46
- `Original/MW.EXE`: 5
- `Original/MW_EGA.EXE`: 5
- `Original/MW_TANDY.EXE`: 5
- `Original/ARCTIC.PAL`: 3
- `Original/BAT.BMP`: 3
- `Original/COCKPIT.BMP`: 3
- `Original/DESERT.PAL`: 3
- `Original/DIGITS.BMP`: 3
- `Original/DMGPAL.PAL`: 3
- `Original/DOTS.BMP`: 3
- `Original/HUD_CYAN.BMP`: 3
- `Original/HUD_NUMS.BMP`: 3
- `Original/JEN.BMP`: 3
- `Original/LOC.BMP`: 3
- `Original/MAR.BMP`: 3
- `Original/PHO.BMP`: 3
- `Original/POINT.BMP`: 3
- `Original/PROMPT.BMP`: 3
- `Original/RIF.BMP`: 3
- `Original/SHA.BMP`: 3
- `Original/SM_MECHS.BMP`: 3
- `Original/STRUTS.BMP`: 3
- `Original/TROPIC.PAL`: 3
- `Original/WAR.BMP`: 3
- `Original/1.GAM`: 1
- `Original/286WI2.GAM`: 1

Selection policy:
- Scan all suitable files directly from bytes and preserve offsets.
- Keep file-name-like references, installer file references, source art paths, and resource chunk tags.
- Keep explicit loader/runtime/error text.
- Keep compact UI, save/load, repair/economy, contract/mission, house/planet strings.
- Keep likely printable save/player names from `.GAM` files.
- Avoid bulk story/dialogue text unless it is a compact anchor label or location/date line.

Storage and candidate kind are triage labels, not final format definitions.
