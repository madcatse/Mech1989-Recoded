# `.GAM` screenshot-to-save mapping notes

Source save: `C:/Games/MSDOS/MWDOS/1.GAM`

Compared against repository sample: `Sorted Original Files/GAM/1.GAM`

The attached save differs from the repository `1.GAM` in only 13 bytes:

- `0x003B..0x0043`: `G. BRAVER` changed to `MADCAT`
- `0x0049..0x004C`: money changed from `2,900,147` to `29,001,470`

This makes the screenshots very useful because they show which screens react to those two edits.

## Confirmed fields

| Offset | Value in attached save | Screenshot evidence | Meaning |
| --- | ---: | --- | --- |
| `0x001D` | `1` | Command screen shows `REPUTATION: WORTH WATCHING` | Raw reputation/rank value. Not a direct string index: another save has `2` and still displays `WORTH WATCHING`. |
| `0x0021` | `0x76` | Main location screen shows `KESAI IV` | Likely current planet index. `MW_MAIN.EXE` planet table has `KESAI IV` immediately after record id `0x77`, so the save appears to use a zero-based index. |
| `0x002B` | `0xAB` | Matches `KESAI IV` planet-table coordinate/data byte in `MW_MAIN.EXE` | Current planet/location metadata. |
| `0x002D` | `0x67` | Matches `KESAI IV` planet-table coordinate/data byte in `MW_MAIN.EXE` | Current planet/location metadata. |
| `0x0033` | `6` | Main location screen shows `JULY, 3027` | Current month, zero-based. `6` means July. |
| `0x0035` | `3027` | Main location screen shows `JULY, 3027` | Current year. |
| `0x003B..0x0045` | `MADCAT` | Mech screen shows `JENNER : MADCAT` | Mech label/callsign, not the commander's displayed name. |
| `0x0047` | `21` | Command screen shows `AGE: 21` | Commander age. |
| `0x0049..0x004C` | `29,001,470` | Command screen shows `WEALTH: 29,001,470` | Wealth / C-bills, little-endian `uint32`. |
| `0x004D` | `2` | Command screen shows `KURITA : POSITIVE` | Kurita attitude score. |
| `0x004F` | `0` | Command screen shows `STEINER: NEUTRAL` | Steiner attitude score. |
| `0x0051` | `0` | Command screen shows `MARIK  : NEUTRAL` | Marik attitude score. |
| `0x0053` | `-6` | Command screen shows `LIAO   : NEGATIVE` | Liao attitude score. |
| `0x0055` | `10` | Command screen shows `DAVION : CONFIDENT` | Davion attitude score. |

## Important correction

The earlier report labeled `0x003B` as commander name because every original sample contained `G. BRAVER` there. The edited save proves that label was too broad:

- `0x003B = MADCAT`
- Command screen still shows `COMMANDER: G BRAVER`
- Crew screen still shows pilot name `G BRAVER`
- Mech list shows `JENNER : MADCAT`

So `0x003B` is a mech label/callsign or the display-name field attached to the owned Jenner, not the global commander/pilot name shown on the command and crew screens.

## Screen observations not yet fully mapped

These are visible in screenshots but not yet tied to exact offsets with enough confidence:

- Crew list: pilot name `G BRAVER`
- Crew list: `GUNRY AVERAGE`
- Crew list: `PILOT AVERAGE`
- Crew list: assigned mech `JENNER`
- Mech list: mech type `JENNER`

The pilot name may be derived from a pilot index into a built-in name table rather than stored as text in the save. `BTECH.EXE` and `MW_MAIN.EXE` both contain pilot/mech string tables, including `G. BRAVER` and `JENNER`.

## Attached save known-field dump

```text
Mech label: MADCAT
Age: 21
Reputation: Worth Watching (1)
Money: 29001470
Year: 3027
Month: 7
Planet: likely KESAI IV (`0x0021 = 0x76`)
Family attitudes:
  Kurita:  2  -> Positive
  Steiner: 0  -> Neutral
  Marik:   0  -> Neutral
  Liao:   -6  -> Negative
  Davion: 10  -> Confident
```
