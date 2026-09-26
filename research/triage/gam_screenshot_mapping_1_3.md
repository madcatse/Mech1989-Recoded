# `.GAM` screenshot mapping notes for `1-3.GAM`

Source save: `C:/Games/MSDOS/MWDOS/1-3.GAM`

Compared primarily against: `C:/Games/MSDOS/MWDOS/1-2.GAM`

Visible screens:

- Command screen: `AGE: 22`, `REPUTATION: WORTH WATCHING`, `WEALTH: 21,740,726`
- Crew screen: two pilots:
  - `G BRAVER`, `GUNRY AVERAGE`, `PILOT AVERAGE`, `MECH SHADOW`
  - `T. RICH`, `GUNRY AVERAGE`, `PILOT POOR`, `WAGE 600`, `MECH SHADOW`
- Main location screen: `BENJAMIN`, `JUNE, 3028`

## Confirmed / strengthened fields

| Offset | `1-2.GAM` | `1-3.GAM` | Screenshot evidence | Meaning |
| --- | ---: | ---: | --- | --- |
| `0x0021` | `0` | `13` | `LUTHIEN` changed to `BENJAMIN`; EXE planet table has Benjamin after record id `0x0E` | Current planet index, stored zero-based. |
| `0x002B` | `0x85` | `0x7D` | Matches Benjamin planet-table coordinate/data byte in `MW_MAIN.EXE` | Current planet metadata copied from the planet table. |
| `0x002D` | `0x4E` | `0x60` | Matches Benjamin planet-table coordinate/data byte in `MW_MAIN.EXE` | Current planet metadata copied from the planet table. |
| `0x0033` | `3` | `5` | `APRIL` changed to `JUNE` | Current month, zero-based. |
| `0x0035` | `3028` | `3028` | Main screen shows `JUNE, 3028` | Current year. |
| `0x0037` | `9` | `13` | Not shown directly | Likely day/tick within month or travel/date subfield. |
| `0x0047` | `21` | `22` | Command screen shows `AGE: 22` | Raw/cached age or start-age-related byte; in this save it now matches displayed age. Keep read-only. |
| `0x0049..0x004C` | `27,273,822` | `21,740,726` | Command screen shows `WEALTH: 21,740,726` | Wealth / C-bills, little-endian `uint32`. |
| `0x004D` | `2` | `4` | Kurita remains `POSITIVE` | Kurita attitude score; both `2` and `4` map to positive. |
| `0x004F` | `0` | `0` | `STEINER: NEUTRAL` | Steiner attitude score. |
| `0x0051` | `0` | `0` | `MARIK: NEUTRAL` | Marik attitude score. |
| `0x0053` | `-6` | `-6` | `LIAO: NEGATIVE` | Liao attitude score. |
| `0x0055` | `10` | `10` | `DAVION: CONFIDENT` | Davion attitude score. |

Money delta from `1-2.GAM` to `1-3.GAM`: `5,533,096` C-bills. This likely includes buying/equipping the second Shadow Hawk, hiring `T. RICH`, travel, and/or other expenses between the two saves.

## Pilot table finding

`MW_MAIN.EXE` contains the pilot table entry for `T. RICH`:

```text
... 0C 01 00 54 2E 20 52 49 43 48 20 20 20 00 ...
              T. RICH
```

The bytes immediately before the name are likely pilot metadata. The pair `01 00` lines up well with the crew screen:

- `01` -> `GUNRY AVERAGE`
- `00` -> `PILOT POOR`

Nearby table entries support this:

```text
T.D. BENTE  prefix: 0C 00 01
T. RICH     prefix: 0C 01 00
JOHN ZOE    prefix: 0D 01 01
KELLY T.    prefix: 0D 01 02
```

This suggests the save does not store the second pilot's name as ASCII. It probably stores roster membership / selected pilot IDs, while the UI resolves names, portraits, skills, and wage from built-in tables.

## Roster / mech-state region

The transition from one Shadow Hawk/pilot to two Shadow Hawks/pilots changes the suspected roster/mech block strongly:

| Range | Observation |
| --- | --- |
| `0x0500..0x0524` | Many small values change after adding `T. RICH` and a second Shadow Hawk. This is a strong roster/pilot/mech-state candidate. |
| `0x0556..0x0560` | Six larger values changed from `340,620,160,350,630,170` to `350,490,170,330,620,160`. Likely armor/internal/repair-related values for owned mechs. |
| `0x057A..0x0584` | Six larger values changed from `1360,2490,670,1400,2540,700` to `1420,1970,690,1320,2490,660`. Likely cost/repair/armor-derived values. |
| `0x0586..0x059C` | Repeated values changed from `13` to `15`. Likely condition/damage/ammo-related arrays. |
| `0x059E` | Changed from `5` to `4`. Candidate active mech/roster status value, not yet enough to label. |

The save still does not contain visible ASCII for `T. RICH` or `SHADOW`; those are resolved from EXE tables.

## Known-field dump

```text
Mech label: G. BRAVER
Displayed age: 22
Raw age/start-age byte: 22
Raw reputation: 2 -> WORTH WATCHING on screen
Money: 21,740,726
Year: 3028
Month: 6
Planet: BENJAMIN (`0x0021 = 13`)
Family attitudes:
  Kurita:  4  -> Positive
  Steiner: 0  -> Neutral
  Marik:   0  -> Neutral
  Liao:   -6  -> Negative
  Davion: 10  -> Confident
Roster shown by UI:
  Slot 0: G BRAVER, average/average, Shadow Hawk
  Slot 1: T. RICH, average/poor, wage 600, Shadow Hawk
```
