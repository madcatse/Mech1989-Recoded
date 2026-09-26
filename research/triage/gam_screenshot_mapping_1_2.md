# `.GAM` screenshot mapping notes for `1-2.GAM`

Source save: `C:/Games/MSDOS/MWDOS/1-2.GAM`

Visible screens:

- Main location screen: `LUTHIEN`, `APRIL, 3028`
- Command screen: `AGE: 22`, `REPUTATION: WORTH WATCHING`, `WEALTH: 27,273,822`
- Crew screen: `G BRAVER`, `GUNRY AVERAGE`, `PILOT AVERAGE`, `MECH SHADOW`
- Mech status screen: `TYPE: SHADOW HAWK`, `FUNCTIONAL`, `REPAIR COST: 0`, `55 TONS`, `85 KPH`, `90 M`, ammo rows `AC 5-PKS: 12`, `LRM 5-PKS: 16`, `SRM 2-PKS: 42`

## Confirmed / strengthened fields

| Offset | Value | Screenshot evidence | Meaning |
| --- | ---: | --- | --- |
| `0x001D` | `2` | Command screen shows `REPUTATION: WORTH WATCHING` | Raw reputation/rank value. This is not a direct text index, because value `1` also displays `WORTH WATCHING` in the earlier save. |
| `0x0021` | `0` | Main screen shows `LUTHIEN`; EXE planet table starts with Luthien record id `1` | Likely current planet index, stored zero-based. |
| `0x002B` | `0x85` | Matches Luthien planet-table coordinate/data byte in `MW_MAIN.EXE` | Current planet metadata copied from the planet table. |
| `0x002D` | `0x4E` | Matches Luthien planet-table coordinate/data byte in `MW_MAIN.EXE` | Current planet metadata copied from the planet table. |
| `0x0033` | `3` | Main screen shows `APRIL, 3028` | Current month, zero-based. |
| `0x0035` | `3028` | Main screen shows `APRIL, 3028` | Current year. |
| `0x003B..0x0045` | `G. BRAVER` | Earlier edited save proved this appears on mech list as mech label/callsign | Mech label/callsign; should not be edited casually. |
| `0x0049..0x004C` | `27,273,822` | Command screen shows `WEALTH: 27,273,822` | Wealth / C-bills, little-endian `uint32`. |
| `0x004D` | `2` | Command screen shows `KURITA : POSITIVE` | Kurita attitude score. |
| `0x004F` | `0` | Command screen shows `STEINER: NEUTRAL` | Steiner attitude score. |
| `0x0051` | `0` | Command screen shows `MARIK  : NEUTRAL` | Marik attitude score. |
| `0x0053` | `-6` | Command screen shows `LIAO   : NEGATIVE` | Liao attitude score. |
| `0x0055` | `10` | Command screen shows `DAVION : CONFIDENT` | Davion attitude score. |

## Important age correction

The displayed age is not simply byte `0x0047`.

- In the earlier 3027 save, `0x0047 = 21` and the screen showed `AGE: 21`.
- In `1-2.GAM`, `0x0047` is still `21`, but the screen shows `AGE: 22`.
- The visible age matches `year - 3006`: `3027 - 3006 = 21`, `3028 - 3006 = 22`.

So `0x0047` is probably a start-age/birth-date-related value or stale cached value, not the authoritative displayed age. The editor now treats age as derived/read-only.

## Combat crash / mech label hypothesis

The earlier edited save that used `MADCAT` in `0x003B..0x0045` loaded in `MW_MAIN.EXE`, but reportedly broke when launching combat. The current safe save keeps `G. BRAVER` in the same field.

Strong hypothesis:

- `0x003B..0x0045` is not merely cosmetic.
- The campaign UI can display an arbitrary edited label, but the combat handoff to `BTECH.EXE` may expect the original pilot/mech label or may copy this field into a fixed roster/combat structure with stricter assumptions.
- Therefore money editing is safe, but renaming this field should be treated as unsafe until proven otherwise.

The editor was updated accordingly: mech label is now read-only by default. A deliberately named `--unsafe-set-mech-label` option exists only for experiments.

## Mech type and status: current candidates

Visible values from screenshots:

- Crew row: `MECH SHADOW`
- Status row: `TYPE: SHADOW HAWK`
- Status row: `CONDITION: FUNCTIONAL`
- Status row: `REPAIR COST: 0`
- Static-looking stats: `55 TONS`, `85 KPH`, `90 M`
- Ammo rows: `AC 5-PKS: 12`, `LRM 5-PKS: 16`, `SRM 2-PKS: 42`

The exact save offsets for mech type/status are not yet fully confirmed. The likely region is `0x0500..0x05B8`, which changes heavily between the Jenner save and this Shadow Hawk save and is already suspected to be roster/mech/combat-transfer state.

Interesting candidates in `1-2.GAM`:

| Offset | Value | Note |
| --- | ---: | --- |
| `0x0500` | `4` | Changed from `5` in the earlier Jenner save; may be count/selection/state rather than type. |
| `0x0556..0x0560` | `340, 620, 160, 350, 630, 170` | Six larger values, likely armor/internal/repair-related values. |
| `0x0562..0x0578` | repeated `3` | Changed from repeated `7/8` in the earlier save; likely condition/damage enums or component states. |
| `0x057A..0x0584` | `1360, 2490, 670, 1400, 2540, 700` | Six larger values, likely costs/armor/part values. |
| `0x0586..0x059C` | repeated `13` | Changed from repeated `30/35`; likely condition/damage/ammo-related values. |
| `0x059E` | `5` | Changed from `2`; possible mech type/slot candidate, but not proven. |
| `0x05B2..0x05B8` | `130, 3, 3, 127` | Small end-of-roster/status cluster; changed from `190, 8, 9, 173`. |

The mech screen numbers may be derived from an EXE mech definition table after the game resolves the mech type. To confirm exact offsets, the next best controlled tests are:

1. Same save, buy/switch only from Shadow Hawk to another mech.
2. Same save, damage one component only.
3. Same save, reload one ammo type only.

## Known-field dump

```text
Mech label: G. BRAVER
Displayed age: 22
Raw age/start-age byte: 21
Raw reputation: 2 -> WORTH WATCHING on screen
Money: 27,273,822
Year: 3028
Month: 4
Planet: likely LUTHIEN (`0x0021 = 0`)
Family attitudes:
  Kurita:  2  -> Positive
  Steiner: 0  -> Neutral
  Marik:   0  -> Neutral
  Liao:   -6  -> Negative
  Davion: 10  -> Confident
```
