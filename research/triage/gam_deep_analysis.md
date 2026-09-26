# MechWarrior 1989 `.GAM` savegame analysis

Analyzed files: `Sorted Original Files/GAM/*.GAM`

## Executive summary

`.GAM` files are savegame/state files. Every sample is exactly `1816` bytes (`0x718`), with no file magic, section directory, compression signature, encryption pattern, or obvious checksum.

The strongest model is: the game writes a fixed-size, mostly little-endian runtime data block directly to disk. `MW_MAIN.EXE` contains a matching new-game/default data template around file offset `0x9148`; it has the same commander name, initial age, initial money, and initial year fields.

The format is not a clean external serialization format. It includes both player/campaign state and nearby static lookup tables that are identical in all saves.

## High-confidence fields

Offsets are file offsets inside `.GAM`.

| Offset | Size | Type | Meaning |
| --- | ---: | --- | --- |
| `0x001D` | 1 | `uint8` | Raw reputation/rank value. Values `1` and `2` have both been observed displaying as `WORTH WATCHING`, so this is not a direct string index. |
| `0x0033` | 1 | zero-based `uint8` | Current month. `6` displays as `JULY`. |
| `0x0035` | 2 | little-endian `uint16` | Current year. Values observed: `3024..3028`. |
| `0x0037` | 1 | `uint8` | Not month; likely day/tick within the current date. Earlier analysis misidentified this. |
| `0x003B` | 11 | ASCII, NUL padded | Mech label/callsign. Changing it to `MADCAT` displays `JENNER : MADCAT` in the mech list. |
| `0x0047` | 1 | `uint8` | Raw start-age/birth-date-related value. Displayed age appears derived from campaign year (`year - 3006`) rather than directly from this byte. |
| `0x0049` | 4 | little-endian `uint32` | Wealth in C-bills. Initial EXE template is `1,000,000`. |
| `0x004D` | 10 | 5 signed `int16` values | Family attitude vector: Kurita, Steiner, Marik, Liao, Davion. |

The reputation and attitude fields are inferred from the in-game command-screen labels in `MW_MAIN.EXE` and a controlled edited save/screenshot pair:
`AGE`, `REPUTATION`, `WEALTH`, `FAMILY ATTITUDES`, followed by `KURITA`, `STEINER`, `MARIK`, `LIAO`, `DAVION`.

## Parsed save headers

| File | Age | Money | Year | Month | `0x004D..0x0055` signed words |
| --- | ---: | ---: | ---: | ---: | --- |
| `1.GAM` | 21 | 2900147 | 3027 | 7 | `[2, 0, 0, -6, 10]` |
| `286WI2.GAM` | 18 | 2028147 | 3026 | 6 | `[0, 0, 0, -2, 2]` |
| `286WIN.GAM` | 21 | 1957110 | 3028 | 3 | `[0, 0, 0, -6, 12]` |
| `286WIN1.GAM` | 21 | 2900147 | 3027 | 7 | `[4, 0, 0, -6, 8]` |
| `BOUNTY.GAM` | 18 | 1140311 | 3024 | 6 | `[0, 0, 0, -2, 2]` |
| `JUN3.GAM` | 20 | 2711345 | 3027 | 5 | `[8, -4, -2, 4, -4]` |
| `MAY17.GAM` | 19 | 1837884 | 3026 | 3 | `[8, 0, -2, -4, 0]` |
| `MAY172.GAM` | 18 | 1542027 | 3025 | 9 | `[0, 0, 0, -2, 2]` |
| `MAY25.GAM` | 19 | 1980221 | 3026 | 7 | `[8, 0, -2, -4, 0]` |
| `SAVE1.GAM` | 18 | 1235311 | 3024 | 5 | `[0, 0, 0, 0, 0]` |
| `SAVE2.GAM` | 18 | 808845 | 3024 | 5 | `[0, 0, 0, 0, 0]` |
| `SAVE3.GAM` | 18 | 1208135 | 3025 | 6 | `[0, 0, 0, 0, 0]` |
| `SAVE4.GAM` | 18 | 1296575 | 3025 | 8 | `[0, 0, 0, 0, 0]` |
| `TEST.GAM` | 18 | 1330000 | 3024 | 6 | `[2, -2, 0, 0, 0]` |

## Structural map

| Range | Notes |
| --- | --- |
| `0x0000..0x001F` | Small global state fields and flags. Contains many `0xFFFF` sentinels. Mostly 16-bit little-endian values, but not all fields are aligned. |
| `0x0020..0x0038` | Current world/date/mission cluster. `0x0021` is likely current planet index, `0x0033` is zero-based month, and `0x0035` is year. Nearby values are likely planet/location, contract, and day/tick fields. |
| `0x003B..0x0058` | Mech label/campaign identity block: mech label/callsign, age, money, house attitudes, and one extra value at `0x0057`. |
| `0x0059..0x011F` | Campaign flags, mission lists, and short counters. Values are sparse and include mirrored/repeated small lists around `0x00CC..0x00E3`. |
| `0x0120..0x025D` | All zero across the current corpus. Probably reserved or unused save slots/state arrays. |
| `0x025E..0x033F` | Sparse event/story/message flags. Mostly zero with isolated small 16-bit values. |
| `0x0340..0x04FF` | Dense matrix of small enum/flag values: mostly `0`, `1`, `2`, `3`, `5`, and `0xFFFF`. Likely visited-world/news/story/availability state. |
| `0x0500..0x05B8` | Active roster/mission/combat-transfer block. Contains counts, six-element lists, pointer-like values in the `0x8Axx..0x8Dxx` range, skill/condition-style small numbers, and cost/amount-style numbers. |
| `0x05B9..0x05DE` | Static lookup table, identical in all saves. Also visible in `MW_MAIN.EXE` near the planet terrain strings `DESERT`, `TROPICAL`, `ICE`. |
| `0x05E0..0x064B` | Mixed block: first words vary, then a large constant table from `0x05F0..0x0644`. |
| `0x064C..0x06FD` | Mostly zero, sparse campaign flags. |
| `0x06FE..0x0717` | Final current-location/story state cluster. Values overlap with `0x0700..0x0708`, suggesting duplicated active selection/state. |

## Important observations

- The save size is fixed: `0x718` bytes in all 14 samples.
- No duplicate payloads were found. The closest pair is `1.GAM` vs `286WIN1.GAM`, differing by only 9 bytes.
- Only 473 of 1816 byte offsets vary across the corpus.
- Most numeric fields are little-endian 16-bit words. Confirmed exceptions are ASCII strings and unaligned fields in the commander/date area.
- `MW_MAIN.EXE` save UI contains `*.GAM` and save/restore prompts, confirming the extension is used by the game itself.
- `MW_MAIN.EXE` contains a new-game template with:
  - initial mech label/callsign `G. BRAVER` at template-relative `0x003B`
  - age `18` at `0x0047`
  - money `1,000,000` at `0x0049`
  - year `3024` at `0x0035`
- Some blocks in `.GAM` are static game tables. This is characteristic of a raw global-data save rather than a designed portable file format.

## Confidence notes

High confidence:
- `.GAM` is a save/state file.
- Fixed raw block layout, no compression/encryption/header.
- Mech label, derived/displayed age, money, raw reputation, year, and month fields.
- Signed family attitude vector at `0x004D..0x0055`.

Medium confidence:
- `0x0020..0x0038` is current location/date/contract metadata.
- `0x0340..0x04FF` is a large flag/enum matrix for story/news/world state.
- `0x0500..0x05B8` is roster/mission/combat-transfer state.

Low confidence:
- Exact labels for most individual fields beyond the confirmed commander/date/money block.
- Exact ordering of the five house attitudes, though the natural order from the UI is Kurita, Steiner, Marik, Liao, Davion.

## Next steps for exact field naming

1. Create controlled saves by changing one variable at a time: travel, rest one month, accept a contract, hire/fire a pilot, buy/sell a mech, repair/reload, finish a mission.
2. Diff those controlled saves against a baseline.
3. Patch simple fields like money or age in a copy of a save and load it in the game to confirm no checksum exists.
4. Use Ghidra or DOSBox instrumentation on `MW_MAIN.EXE` save/load functions around the `*.GAM` string to identify the exact memory address and write size.
