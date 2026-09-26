# MW_MAIN planet table findings

Original files in `Original/` were not modified.

## Summary

The BattleTechWiki planet table can be reconstructed from `Original/MW_MAIN.EXE`.

The planet string area starts at file offset `0x00F4C0` with `LUTHIEN`, but each planet record actually begins 10 bytes earlier than the name. Example:

```text
0x00F4B6: 01 00 02 00 01 85 4E 08 53 14  LUTHIEN\0CAPITAL OF THE DRACONIS COMBINE\0
```

The EXE has 145 unique planet records:

- Kurita: 30
- Steiner: 30
- Marik: 30
- Liao: 25
- Davion: 30

## Record format

Each planet record is:

```text
byte 0      planet number, 1..145 in the unique table; 1..146 in the raw primary block because of the duplicate
byte 1      house id: 0 Kurita, 1 Steiner, 2 Marik, 3 Liao, 4 Davion
byte 2      terrain code, 1..12
byte 3      unknown, values 0..4
byte 4      contract availability flag: 0 no House icon/contracts, 1 contracts available
byte 5      starmap/screen x
byte 6      starmap/screen y
byte 7      unknown, values preserved as raw data
bytes 8..9  population in millions, uint16 little-endian
bytes 10..  planet name, NUL-terminated ASCII
then        planet description, NUL-terminated ASCII
```

Population rule:

```text
population = uint16le(record[8:10]) * 1,000,000
```

Environment rule:

```text
terrain_code % 3 == 1 -> Desert
terrain_code % 3 == 2 -> Tropical
terrain_code % 3 == 0 -> Ice
```

This matches the PDF table. For example:

- Luthien: terrain code `2` -> Tropical, population `0x1453 * 1,000,000 = 5,203,000,000`.
- Albiero: terrain code `10` -> Desert, population `0x0358 * 1,000,000 = 856,000,000`.
- Land's End: terrain code `12` -> Ice, population `0x007B * 1,000,000 = 123,000,000`.

## Pointer tables

At file offset `0x00F270` there is a 16-bit pointer table. Pointer values are not absolute file offsets; add `0x008D00` to get the file offset of a planet record.

Example:

```text
pointer value 0x67B6 + 0x008D00 = 0x00F4B6  -> Luthien record
pointer value 0x6D2E + 0x008D00 = 0x00FA2E  -> Albiero record
```

Two useful pointer orders exist:

- primary order: first 146 pointers, grouped by house; this yields 145 unique planets because of one duplicate;
- alphabetical order: recovered from later pointers beginning at pointer index 146. This order matches the wiki/PDF table: Albiero, Alshain, Benjamin, ...

## Generated files

- `tools/planet_table_extractor.py` - reproducible extractor.
- `research/analysis/mw_main_planet_table.csv` - table for spreadsheet/manual review.
- `research/analysis/mw_main_planet_table.json` - structured data for the future engine.

## Open fields

Bytes 3 and 7 are not fully identified yet. They are preserved in the generated table as `unknown_byte_3` and `unknown_byte_7`.

Current evidence suggests bytes 5 and 6 are map/screen coordinates. Further confirmation should come from comparing them with the starmap rendering code or screenshot coordinates.

## Davion extension and duplicate

The first pass stopped too early at 130 records. The next 16 primary pointers contain the missing Davion list section:

```text
KITTERY, GREAT GORGE, CAPH, HOFF, BAXLEY,
GREELY, COGDELL, FRAZER, MORAVIAN, NEW ARAGON,
NOATAK, BEECHER, XHOSA VII, IMMENSTADT, DELACAMBRE,
ANDER'S MOON
```

`ANDER'S MOON` appears twice as a planet record. The generated 145-planet table keeps the first record, because the alphabetical pointer table references that copy and the game list displays only one `ANDER'S MOON`.
