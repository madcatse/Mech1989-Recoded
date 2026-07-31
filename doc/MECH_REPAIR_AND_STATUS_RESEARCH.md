# Mech repair and status research

This note collects the current evidence for the `MW_MAIN.EXE` mech status,
repair, armor, jump jet, and weapon-status screens. It is intentionally split
between confirmed data, screenshot observations, and open hypotheses.

Canonical sources:

- `Original/MW_MAIN.EXE` for campaign UI strings and campaign-side tables.
- `Original/MW_PICS.BIN` for mech status artwork.
- `screenshots/image0190.png` through `screenshots/image0199.png` and
  `screenshots/image0219.png` for repair/status visual targets.
- Save-analysis notes in `research/triage/gam_screenshot_mapping*.md` for the
  current `.GAM` state layout candidates.

## Addressing convention

Existing MW_MAIN research uses:

```text
file_offset = 0x008D00 + DS_offset
```

Examples:

- DS `0x2A00` -> file `0x00B700`
- DS `0x3B53` -> file `0x00C853`

## Status text and damage states

Confirmed UI strings in `Original/MW_MAIN.EXE`:

| text | file offset | loaded offset |
| --- | ---: | ---: |
| `FUNCTIONAL` | `0x00B82D` inside a longer string run | `0x00B42D` |
| `LIGHT DAMAGE` | `0x00B836` | `0x00B436` |
| `HEAVY DAMAGE` | `0x00B843` | `0x00B443` |
| `JUNK` | `0x00B850` | `0x00B450` |
| `NONFUNCTIONAL` | `0x00B855` | `0x00B455` |

The random-damage initializer in the Ghidra export writes component bytes as
`3`, `2`, and `1` for descending damage pools and leaves default bytes as `0`.
Current working enum:

```text
0 = FUNCTIONAL
1 = LIGHT DAMAGE
2 = HEAVY DAMAGE
3 = JUNK
```

`NONFUNCTIONAL` is present as a separate display string and should be treated as
a derived overall-mech state until the exact condition logic is confirmed.

## Mech list

MW_MAIN campaign-side tables include ten chassis, in this order:

```text
0 Locust
1 Wasp
2 Jenner
3 Phoenix Hawk
4 Shadow Hawk
5 Wolverine
6 Rifleman
7 Warhammer
8 Marauder
9 Battlemaster
```

The user-facing mech rows are stored around file `0x00BAC0`; the short names
are stored around `0x00BB85`. Wasp and Wolverine are present even though the
currently identified large status-art sequence in `MW_PICS.BIN` covers the
eight larger campaign mech images listed below.

Runtime policy as of the Mechbay implementation pass:

- Treat Wasp and Wolverine as historical/internal finds from unfinished content.
- Keep their strings and table bytes in research notes because they help explain
  MW_MAIN data layout.
- Do not expose them as playable, buyable, repairable, or inventory chassis in
  the rebuilt engine unless a future explicit historical/debug mode needs them.
- The runtime playable roster now contains the eight normal campaign chassis:
  Locust, Jenner, Phoenix Hawk, Shadow Hawk, Rifleman, Warhammer, Marauder, and
  Battlemaster.
- `REVIEW MECHS`, `MECH STATUS`, `REPAIR`, `DAMAGE LEVELS`, `RELOAD`,
  `CREW` assignment miniatures, buying, selling, and debug Mech-spawn cheats are
  expected to work from the same chassis definitions.
- The `REVIEW MECHS` inventory list supports the 12-Mech ownership limit and
  sizes its overlay window to the current inventory count instead of covering
  empty background with unused rows.

## Chassis stats table

Confirmed byte table at file `0x00B865`:

| chassis | tons | speed KPH | jump cap, m | heat sinks |
| --- | ---: | ---: | ---: | ---: |
| Locust | 20 | 129 | 0 | 10 |
| Wasp | 20 | 95 | 180 | 10 |
| Jenner | 35 | 118 | 150 | 10 |
| Phoenix Hawk | 45 | 97 | 180 | 10 |
| Shadow Hawk | 55 | 86 | 90 | 12 |
| Wolverine | 55 | 86 | 150 | 12 |
| Rifleman | 60 | 64 | 0 | 10 |
| Warhammer | 70 | 64 | 0 | 18 |
| Marauder | 75 | 64 | 0 | 16 |
| Battlemaster | 85 | 64 | 0 | 18 |

Heat sink totals are confirmed at file `0x00C38C`:

```text
0A 0A 0A 0A 0C 0C 0A 12 10 12
```

The jump jet count may be derived from `jump_cap_m / 30`, which gives:

| chassis | derived jump jets |
| --- | ---: |
| Locust | 0 |
| Wasp | 6 |
| Jenner | 5 |
| Phoenix Hawk | 6 |
| Shadow Hawk | 3 |
| Wolverine | 5 |
| Rifleman | 0 |
| Warhammer | 0 |
| Marauder | 0 |
| Battlemaster | 0 |

Original-game editor tests confirm that Jenner displays 3 jump jets, not the
`150 / 30 = 5` derived-table hypothesis. Keep other chassis denominators under
review until controlled screenshots or save edits confirm them.

## Repair detail labels

The repair/detail panel strings are present twice:

| label | repair/status block | alternate block |
| --- | ---: | ---: |
| `ENGINE:` | `0x00BF77` | `0x00D254` |
| `GYROS:` | `0x00BF96` | `0x00D273` |
| `SENSORS:` | `0x00BFB5` | `0x00D292` |
| `LIFE SUPPORT:` | `0x00BFD4` | `0x00D2B1` |
| `HEAT SINK   :   OF` | `0x00BFF3` | `0x00D2D0` |
| `LA ACTUATOR:` | `0x00C012` | `0x00D2EF` |
| `RA ACTUATOR:` | `0x00C031` | `0x00D30E` |
| `LL ACTUATOR:` | `0x00C050` | `0x00D32D` |
| `RL ACTUATOR:` | `0x00C06F` | `0x00D34C` |
| `JUMP JETS:     OF` | `0x00C08E` | `0x00D36B` |
| `ARMOR:           %` | `0x00C0AD` | `0x00D38A` |
| `WPN    LOC    CONDITION` | `0x00C0C5` | `0x00D3A2` |
| `COST OF REPAIR:` | `0x00C0EA` | not in same compact block |

The duplicate block likely supports a related purchase/status path. Do not
treat it as a second independent repair system.

## Repair cost matrix

The repair-cost routine near file `0x004450` reads a word from a component by
chassis matrix at DS `0x3B53` / file `0x00C853`:

```text
value = u16le[0x00C853 + component_index * 20 + mech_index * 2]
```

The routine then applies one of two visible multipliers:

```text
if component_index in {4, 9, 10}:
    cost = value * 1000
else:
    cost = value * 333
```

Confirmed examples:

- `HEAT SINK` is component index `4`; its matrix value is `2` for every
  chassis, so a heat sink repair/replacement displays `2000` C-bills.
- `4662` C-bills equals `14 * 333`, matching an actuator-style matrix value of
  `14`. Current raw table gives that value for Rifleman arm actuators. The
  screenshot association with "Jenner LL actuator" should be rechecked because
  the Jenner LL/RL actuator raw value in this table is `12`, which would yield
  `3996` by the currently traced formula.

Raw matrix rows currently mapped with the visible UI order:

| component | Locust | Wasp | Jenner | Phoenix Hawk | Shadow Hawk | Wolverine | Rifleman | Warhammer | Marauder | Battlemaster |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Engine | 210 | 0 | 58 | 42 | 232 | 240 | 192 | 20 | 220 | 108 |
| Gyros | 190 | 0 | 28 | 2 | 132 | 142 | 112 | 176 | 20 | 64 |
| Sensors | 40 | 40 | 70 | 90 | 110 | 110 | 120 | 140 | 150 | 170 |
| Life Support | 50 | 50 | 50 | 50 | 50 | 50 | 50 | 50 | 50 | 50 |
| Heat Sink | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 2 | 2 |
| LA Actuator | 5 | 5 | 8 | 10 | 13 | 13 | 14 | 16 | 17 | 19 |
| RA Actuator | 5 | 5 | 8 | 10 | 13 | 13 | 14 | 16 | 17 | 19 |
| LL Actuator | 7 | 7 | 12 | 15 | 19 | 19 | 21 | 24 | 26 | 29 |
| RL Actuator | 7 | 7 | 12 | 15 | 19 | 19 | 21 | 24 | 26 | 29 |
| Jump Jets | 0 | 2 | 2 | 1 | 1 | 2 | 0 | 0 | 0 | 0 |

Open caution: the first two rows look suspicious for Wasp and several other
chassis. The read address and formula are byte-confirmed, but the semantic
meaning of the first two rows still needs a runtime check.

Implementation note from the 2026-07-21 Mechbay pass: a fresh binary dump of
`0x00C853` confirms that the Jenner engine/gyro word values are `570` and
`540` in the raw table. Earlier shorthand notes that reduced these to `58` and
`28` were wrong for direct runtime use. The current engine code uses the raw
word values for Jenner component repair pricing.

2026-07-28 controlled starting-Jenner repair chain:

- `DATA6.GAM` starts with `LIFE SUPPORT: LIGHT DAMAGE`, `HEAT SINK: 9 OF 10`,
  `RA ACTUATOR: LIGHT DAMAGE`, `ARMOR: 66%`, and total displayed repair cost
  `54,306`.
- Repairing life support changes `.GAM` `0x0105: 01 -> 00` and costs `23,643`.
- Repairing RA actuator changes `.GAM` `0x0108: 01 -> 00` and costs `3,663`.
- Repairing one heat sink changes `.GAM` `0x0106: 01 -> 00` and costs `2,000`.
- Each armor repair in the chain costs `5,000`.
- These observed costs sum exactly to the initial `54,306` repair estimate:
  `23,643 + 3,663 + 2,000 + 5 * 5,000 = 54,306`.

2026-07-28 controlled replacement-Jenner repair chain:

- `DAT0.GAM` starts with a bought damaged Jenner: `CONDITION: FUNCTIONAL`,
  `SRM 4-PKS: 14`, `SENSORS: LIGHT DAMAGE`, one `M LAS` in `RA` at `JUNK`,
  and `ARMOR: 74%`.
- Repairing sensors changes `.GAM` `0x0104: 01 -> 00` and costs `27,306`.
- Repairing the observed `M LAS RA` while also doing one armor repair changes
  `.GAM` `0x010E` through the full condition chain
  `3 -> 2 -> 1 -> 0`, matching `JUNK -> HEAVY DAMAGE -> LIGHT DAMAGE ->
  FUNCTIONAL`.
- Each combined weapon+armor step in `DAT1 -> DAT2`, `DAT2 -> DAT3`, and
  `DAT3 -> DAT4` costs `19,651`. Since each step includes one confirmed
  `5,000` armor repair, the observed weapon step cost contribution is
  `14,651` in this context.
- `DAT2 -> DAT3` also triggers many campaign/world bytes outside the mech
  repair cluster, likely because enough repair time elapsed to cross a periodic
  update boundary. Do not assign those broad changes to the weapon/armor repair
  fields without a separate controlled save.

2026-07-28 replacement-Locust observation:

- `DAT5.GAM` starts after selling the replacement Jenner and buying a damaged
  Locust. The active chassis byte is `.GAM` `0x00EA = 0`, while the replacement
  Jenner in `DAT0.GAM` has `0x00EA = 2`; this matches the original chassis
  order (`0` Locust, `2` Jenner).
- The same active-mech condition block is reused for Locust in this one-Mech
  save: `0x0102 = 1` (`ENGINE: LIGHT DAMAGE`), `0x0107 = 3`
  (`LA ACTUATOR: JUNK`), and `0x010D = 2` (`MG RA: HEAVY DAMAGE`).
- Locust ammo uses the same active ammo byte: `.GAM` `0x025E = 149`, displayed
  as `MACH GUN: 149`. Original-game testing confirms the Locust machine-gun
  ammo maximum is `200`.
- Locust armor uses the same nine-byte active armor range. `DAT5` has
  `0x0116..0x011E = 00 00 00 01 00 00 00 00 01`, whose sum `2` gives
  `floor((27 - 2) * 100 / 27) = 92%`.
- The observed repair cost is `98,576` C-bills. Keep this as an observed
  original-game value until weapon/component cost tracing for Locust is
  tightened.

2026-07-29 owned-mech slot table:

- `MECH1.GAM` has one owned Locust. `MECH2.GAM` is the same campaign state
  after buying a Phoenix Hawk into slot 2.
- `.GAM` `0x00E8` is the owned mech count (`1 -> 2`).
- `.GAM` `0x00EA..0x0101` is a twelve-entry `uint16le` chassis-id list.
  `MECH2` has slot 1 `0` (`LOCUST`) and slot 2 `3` (`PHOENIX HAWK`); empty
  slots are `0xFFFF`.
- `.GAM` `0x0102..0x025D` is a twelve-record owned-mech state table with
  record stride `0x1D`. Record layout is shared across slots:
  components at `+0x00..+0x09`, up to ten weapon condition bytes at
  `+0x0A..+0x13`, and armor damage bytes at `+0x14..+0x1C`.
- `.GAM` `0x025E..0x0275` is a twelve-entry `uint16le` ammo-count table. In
  `MECH2`, slot 2 ammo at `0x0260` is `187`, matching Phoenix Hawk
  `MACH GUN: 187`.
- Phoenix Hawk slot 2 confirms `record_base = 0x011F`; `0x0125`
  (`record_base + 0x06`) is `RA ACTUATOR: JUNK`.
- Phoenix Hawk slot 2 armor bytes are `(0,1,0,0,0,3,0,0,0)`, giving
  `floor((27 - 4) * 100 / 27) = 85%`. The observed yellow left arm matches
  `LA = 1`; the heavily damaged torso matches current `CT = 3`.
- The save editor exposes all ten original chassis ids for preservation and
  controlled testing. Wasp and Wolverine remain historical/internal for the
  rebuilt runtime, but their original weapon rows (`M LAS RA`/`SRM2 LT` and
  `AC/5 RA`/`SRM6 LT`/`M LAS HD`) are now available as editor labels.

2026-07-28 original-game editor round-trip confirmations:

- Player Jenner component condition offsets confirmed:
  `0x0102` engine, `0x0103` gyros, `0x0104` sensors, `0x0105` life support,
  `0x0106` heat-sink missing/damaged count, `0x0108` RA actuator,
  `0x0109` LL actuator, and `0x010A` RL actuator.
- Player Jenner weapon condition offsets confirmed:
  `0x010C` `SRM4 CT`, `0x010D` `M LAS RA A`, `0x010E` `M LAS RA B`,
  `0x010F` `M LAS LA A`, and `0x0110` `M LAS LA B`.
- Overall Jenner status is derived: `ENGINE`, `GYROS`, `SENSORS`,
  `LIFE SUPPORT`, `LL ACTUATOR`, or `RL ACTUATOR` at `JUNK` makes the mech
  display `NONFUNCTIONAL`. Setting the same component to `HEAVY DAMAGE` returns
  the mech to `FUNCTIONAL` if no other critical component is still `JUNK`.
  Weapon `JUNK` states do not by themselves make the mech `NONFUNCTIONAL`.
- `0x0107` LA actuator and `0x010B` jump-jet missing/damaged count remain
  exposed as editor test fields until isolated controlled saves confirm them.

## Weapon status table

Weapon rows are stored as ready-to-render strings. A pointer table at file
`0x00C3AE` contains ten DS pointers to weapon groups:

```text
0x36C2 -> file 0x00C3C2
0x36FA -> file 0x00C3FA
0x3723 -> file 0x00C423
...
```

Groups are separated by the string `DONE`. Current mapping:

| chassis | weapon rows |
| --- | --- |
| Locust | `M LAS CT`, `MG RA`, `MG LA` |
| Wasp | `M LAS RA`, `SRM2 LT` |
| Jenner | `SRM4 CT`, `M LAS RA`, `M LAS RA`, `M LAS LA`, `M LAS LA` |
| Phoenix Hawk | `L LAS RA`, `M LAS RA`, `M LAS LA`, `MG LA`, `MG RA` |
| Shadow Hawk | `AC/5 LT`, `LRM5 RT`, `SRM2 HD`, `M LAS RA` |
| Wolverine | `AC/5 RA`, `SRM6 LT`, `M LAS HD` |
| Rifleman | `L LAS RA`, `L LAS LA`, `AC/5 RA`, `AC/5 LA`, `M LAS RT`, `M LAS LT` |
| Warhammer | `PPC RA`, `PPC LA`, `SRM6 RT`, `M LAS RT`, `M LAS LT`, `S LAS RT`, `S LAS LT`, `MG RT`, `MG LT` |
| Marauder | `PPC RA`, `PPC LA`, `M LAS RA`, `M LAS LA`, `AC/5 RT` |
| Battlemaster | `PPC RA`, `M LAS RT`, `M LAS RT`, `M LAS RT`, `MG LA`, `MG LA`, `SRM6 LT`, `M LAS LT`, `M LAS LT`, `M LAS LT` |

Weapon condition should use the same four-state enum as components unless a
later combat handoff trace proves a separate encoding.

## Ammo compatibility strings

Ammo rows are stored around `0x00CCDA..0x00CE13` and include compatibility
lists. Examples:

- `AC 5-PKS:` -> `SHADOW HAWK`, `WOLVERINE`, `RIFLEMAN`, `MARAUDER`
- `LRM 5-PKS:` -> `SHADOW HAWK`
- `SRM 2-PKS:` -> `WASP`, `SHADOW HAWK`
- `SRM 4-PKS:` -> `JENNER`
- `SRM 6-PKS:` -> `WOLVERINE`, `WARHAMMER`, `BATTLEMASTER`
- `MACH GUN:` -> `LOCUST`, `PHONIX HAWK`, `WARHAMMER`, `BATTLEMASTER`

The `PHONIX HAWK` spelling appears exactly this way in the source string.

Reload UI and current implementation:

- `RELOAD COST:`, `WEALTH:`, `RELOAD`, and `CANCEL` are present around
  file `0x00BE2B..0x00BE58`.
- A separate `BUY AMMO` screen and ammo-type list are present around
  `0x00CE75..0x00CF67`, but the exact max-ammo and reload-price tables have
  not yet been isolated from nearby UI/script records.
- Screenshot evidence for the starting Jenner shows `SRM 4-PKS: 8`, a reload
  to `25`, and `RELOAD COST: 21,845`; current formula is therefore
  `(25 - current_srm4_packs) * 1,285`.
- For the current all-Mech test pass, every reloadable ammo type on every
  playable chassis temporarily has a 25-pack capacity. This is deliberately a
  testing bridge until original-game measurements provide exact per-chassis
  capacities.

Extra ammo / inventory implementation:

- `EXTRA AMMO` is confirmed at file `0x00B308`.
- `BUY AMMO`, `AMMO TYPE`, `COST`, `IN HOLD`, and ammo-row UI records are
  present around `0x00CE75..0x00CF67`.
- Controlled `DATA4.GAM` / `DATA5.GAM` saves confirm that campaign extra ammo
  in hold is stored in `.GAM` as six little-endian `uint16` counters at
  `0x02EE..0x02F9`.
- The save order is `AC 5-PKS`, `LRM 5-PKS`, `SRM 2-PKS`, `SRM 4-PKS`,
  `SRM 6-PKS`, `MACH GUN`, matching the campaign UI order and the current
  recomp `kAmmoDefinitions` array.
- `DATA4.GAM` has all six hold counters at zero. `DATA5.GAM` has values
  `5, 10, 15, 20, 25, 50` after buying those amounts through `EXTRA AMMO`.
- `MACH GUN` is bought by tens in the UI, but the save stores the resulting
  total value directly; the observed stored value is `50`.
- Current engine implementation keeps six ammo inventory counters with a
  temporary cap of `9999` each, matching the confirmed save field width for
  editor purposes but not yet proving the original UI maximum.
- Manual `MECH STATUS -> RELOAD` still pays C-bills directly and does not
  consume this inventory. The inventory is reserved for later post-mission
  auto-refill behavior.
- Wasp and Wolverine are omitted from compatibility lists in the playable build.
- Temporary tier-1 prices from screenshot observations:

| ammo type | tier-1 cost | playable compatibility |
| --- | ---: | --- |
| `AC 5-PKS` | 285 | Shadow Hawk, Rifleman, Marauder |
| `LRM 5-PKS` | 1475 | Shadow Hawk |
| `SRM 2-PKS` | 637 | Shadow Hawk |
| `SRM 4-PKS` | 1274 | Jenner |
| `SRM 6-PKS` | 2124 | Warhammer, Battlemaster |
| `MACH GUN` | 5 | Locust, Phoenix Hawk, Warhammer, Battlemaster |

Price caution: the original-game money field changed from `1,049,915` in
`DATA4.GAM` to `923,280` in `DATA5.GAM`, a delta of `126,635` C-bills for the
observed purchase set. The temporary recomp prices above do not yet reproduce
that total and need a dedicated pricing pass.

## Mech status art

Confirmed `MW_PICS.BIN` mech-art records:

| chassis | MW_PICS known record |
| --- | --- |
| Locust | `known_007_0002A00D_header_phase1_111x182` |
| Jenner | `known_008_0002C7E6_header_phase1_125x165` |
| Phoenix Hawk | `known_009_0002F08A_header_phase1_115x183` |
| Shadow Hawk | `known_010_00031A09_header_phase1_119x182` |
| Rifleman | `known_011_000344BB_header_135x187` |
| Warhammer | `known_012_0003766F_header_phase1_113x178` |
| Marauder | `known_013_00039E1A_header_phase1_121x161` |
| Battlemaster | `known_014_0003C480_header_phase1_125x181` |

Runtime placement adjustments on the Mech Status grid:

| chassis | x offset | y offset |
| --- | ---: | ---: |
| Locust | +1 | -3 |
| Jenner | 0 | -2 |
| Phoenix Hawk | +1 | -1 |
| Shadow Hawk | +1 | -3 |
| Rifleman | 0 | -1 |
| Warhammer | -1 | -3 |
| Marauder | -1 | -2 |
| Battlemaster | 0 | -3 |

After drawing the status-art image, the engine redraws the outer GP frame. This
keeps the upper window decoration visually above tall Mech art when an image
touches or overlaps the frame area.

Working visual hypothesis:

- Purple/magenta regions in these images are not final display color.
- They are likely recolor masks for armor/location state.
- Screenshot evidence suggests:
  - healthy armor/location: light gray
  - damaged armor/location: yellow
  - destroyed/no armor: black

This still needs direct palette/index validation against the extracted image
pixels and the screenshots.

## Armor model status

Confirmed:

- The UI exposes aggregate `ARMOR: n%`.
- The art uses per-region recoloring, so the original game almost certainly
  tracks armor or armor-derived condition by location.

Not yet confirmed:

- Whether campaign-side `MW_MAIN.EXE` stores full location armor tables or
  mostly reads campaign/combat state returned from `BTECH.EXE`.
- Whether the `BACK` byte has a hidden non-front-art display path.

2026-07-21 follow-up search:

- A targeted scan around `0x00BC34`, `0x00BEE0`, `0x00BF00`, `0x00C853`, and
  the duplicate repair-string block did not reveal a stable
  `chassis x armor location` maximum-armor table in `MW_MAIN.EXE`.
- The block at `0x00C853` is still the confirmed repair-cost matrix, not armor
  HP.
- The smaller numeric runs around `0x00BC34` and `0x00BEE0` are mixed with
  ammo strings, pointers, or UI/script parameters and do not currently match a
  plausible armor-location layout.
Current engine model:

- Playable chassis armor now uses the confirmed `.GAM` damage-byte model:
  nine raw levels in the save/editor order `RA`, `LA`, `RL`, `LL`, `HEAD`,
  `CT`, `BACK`, `TR`, `TL`.
- Each raw section stores `0..3`, displayed by the editor as `100%`, `66%`,
  `33%`, and `0%`.
- Aggregate armor percent is
  `floor((27 - sum(armor_damage_bytes)) * 100 / 27)`.
- Random market/start damage distributes raw damage levels across those nine
  bytes instead of subtracting tabletop armor points.
- A manual armor repair action fixes the next damaged section back to raw `0`;
  the selected action cost is proportional to that section's raw damage level.
- Status-art recoloring uses hand-authored armor-region rectangles exported in
  `mechs_armor_parts/mw_pics_armor_regions_all.json`.
  Rectangles are only search bounds; the engine recolors purple stencil pixels
  inside them and leaves outlines, highlights, internals, cockpit, and grid
  pixels untouched.
- Current front-art overlays use the visible sections `RA`, `RL`, `RT`, `CT`,
  `LT`, `LL`, and `LA`; the runtime maps those legacy art labels onto GAM
  keys `RA`, `RL`, `TR`, `CT`, `TL`, `LL`, and `LA`. The `BACK` byte
  participates in armor percent and repair but has no observed front overlay.
- The current Warhammer mapping includes the added `LT` rectangle
  `x=91, y=3, w=22, h=20`.
- Armor status-art colors follow the raw damage levels: `0` light gray,
  `1` yellow, `2` red, `3` black.
- In these Mech Status art records, the purple armor stencil can occupy source
  color index `0`; unlike many sprite paths it must not be treated as
  transparent before armor recoloring.

Useful save-region candidates from existing notes:

- `.GAM` `0x00EA`: confirmed active/current mech chassis id in one-Mech saves;
  `0` = Locust in `DAT5`, `2` = Jenner in `DAT0`.
- `.GAM` `0x0105`: confirmed player Jenner life-support condition in the
  2026-07-28 `DATA6/DATA7` controlled repair saves (`1` = `LIGHT DAMAGE`,
  `0` = `FUNCTIONAL`).
- `.GAM` `0x0102`: confirmed active mech engine condition; `DAT5` Locust uses
  `1` for `LIGHT DAMAGE`.
- `.GAM` `0x0103`: confirmed player Jenner gyros condition.
- `.GAM` `0x0104`: confirmed player Jenner sensors condition in the
  2026-07-28 `DAT0/DAT1` controlled repair saves (`1` = `LIGHT DAMAGE`,
  `0` = `FUNCTIONAL`).
- `.GAM` `0x0106`: confirmed player Jenner missing/damaged heat-sink count in
  the 2026-07-28 `DATA8/DATA9` controlled repair saves (`1` displays as
  `9 OF 10`, `0` as `10 OF 10`).
- `.GAM` `0x0108`: confirmed player Jenner RA actuator condition in the
  2026-07-28 `DATA7/DATA8` controlled repair saves (`1` = `LIGHT DAMAGE`,
  `0` = `FUNCTIONAL`).
- `.GAM` `0x0109`: confirmed player Jenner LL actuator condition.
- `.GAM` `0x010A`: confirmed player Jenner RL actuator condition.
- `.GAM` `0x010B`: confirmed active mech jump-jet missing/damaged count; the
  correct Jenner total is 3 jump jets, while Locust has 0.
- `.GAM` `0x010E`: confirmed observed player Jenner `M LAS` in `RA` weapon
  condition in the 2026-07-28 `DAT1..DAT4` controlled repair saves
  (`3` = `JUNK`, `2` = `HEAVY DAMAGE`, `1` = `LIGHT DAMAGE`,
  `0` = `FUNCTIONAL`).
- `.GAM` `0x010C..0x0110`: confirmed active mech weapon-condition array.
  Jenner order: `SRM4 CT`, `M LAS RA A`, `M LAS RA B`, `M LAS LA A`,
  `M LAS LA B`. Locust order currently confirmed/expected as `M LAS CT`,
  `MG RA`, `MG LA`.
- `.GAM` `0x0116..0x011E`: confirmed active mech armor-damage range, modeled
  as nine `0..3` damage levels.
- For the observed one-Mech saves, the aggregate repair-menu armor percentage
  is `floor((27 - sum(confirmed armor-damage bytes)) * 100 / 27)`.
- Confirmed visible section mappings from the controlled repair chain and
  editor/original-game round trips: `0x0116` = `RA`, `0x0117` = `LA`,
  `0x0118` = `RL`, `0x0119` = `LL`, `0x011A` = `HEAD`, `0x011B` = `CT`,
  `0x011D` = `TR`, and `0x011E` = `TL`.
- `.GAM` `0x011A` was initially labeled `CT`, but the combined `0x011A = 2`
  and `0x011B = 1` screenshot shows it overlays the cockpit/head-like top
  section, so the current working name is `HEAD`.
- `.GAM` `0x011B` was initially labeled `UNK1`; current tests promote it to
  the real center torso (`CT`).
- `.GAM` `0x011C` is labeled `BACK` as a working hypothesis; it affects armor
  percentage but produced no observed front-art overlay.
- Armor damage level colors observed on the status art: `1` yellow, `2` red,
  `3` black. `0` is repaired/gray.
- A single armor level costs `4,000` C-bills to repair in the current
  controlled tests, and a single damaged section repairs straight to `100%`.
- `.GAM` `0x025E`: confirmed active mech ammo count in one-Mech saves. Jenner
  uses `SRM 4-PKS` (`4` packs damaged/partial, `25` full); Locust uses
  `MACH GUN` (`149` in `DAT5`, maximum `200`).
- `.GAM` `0x02EE..0x02F9`: confirmed extra-ammo-in-hold array with six
  `uint16le` counters in ammo UI order.
- `.GAM` `0x0500..0x0524`: roster / mech-state candidate. The 2026-07-28
  armor/reload saves did not change this range, so it is not the aggregate
  armor/ammo storage for the starting one-Jenner case.
- `.GAM` `0x0556..0x0560`: six larger values that change between owned mech
  states; likely armor/internal/repair related.
- `.GAM` `0x057A..0x0584`: six larger values; likely costs, armor, or
  part-value derived.
- `.GAM` `0x0562..0x0578` and `0x0586..0x059C`: repeated small values;
  likely condition or damage arrays.

Next best confirmation steps:

1. Trace the code that writes the `ARMOR: n%` numeric field and the mech-art
   recolor calls.
2. Create controlled saves where only one location's armor changes.
3. Compare before/after bytes in `.GAM` and the corresponding screenshot
   recolor region.
4. Check whether `BTECH.EXE` contains a more complete combat-side armor table
   that is copied back to `MW_MAIN.EXE`.
