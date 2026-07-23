# NEWS NET research notes

Goal: reconstruct the original `MW_MAIN.EXE` NEWS NET publication and
navigation logic.

## Confirmed code/data anchors

Global data offsets in the Ghidra export use the relation:

```text
file_offset = 0x008D00 + DS_offset
```

Confirmed anchors:

```text
DS:2500 / file 0x00B200  NEWS NET UI/button data block
DS:2564 / file 0x00B264  "NO OTHER MESSAGES ON FILE!"
DS:60B9 / file 0x00EDB9  pointer list for NEWS NET publication records
DS:63F7 / file 0x00F0F7  first confirmed NEWS NET publication record
```

The NEWS NET screen code is around file `0x0031C0` / code offset
`101B:2C10`. It calls:

```text
101B:6314 / file 0x0068C4  initialize message navigation state
101B:6325 / file 0x0068D5  scan forward to the next matching NEWS record
```

The generic message dispatcher is:

```text
101B:6236 / file 0x0067E6
```

It scans message records of the requested type. Type `3` is the NEWS NET
article type. For non-type-3 messages it checks `DS:0ABA + message_id` and
skips already-seen records; type `3` is allowed to be read repeatedly.

The condition interpreter is:

```text
101B:639B / file 0x00694B
```

Each record starts with:

```text
byte 0  message id
byte 1  message type
then    condition opcodes
0xFF    terminator
```

## Date condition

NEWS article records use condition opcode `0x01`:

```text
message_id 03 01 month day year_minus_3000 FF
```

The date handler compares against the campaign date fields:

```text
DS:0479  day counter within displayed month, 0..59
DS:047B  displayed month, zero-based
DS:047D  displayed year
```

The date bytes are converted as:

```text
required_month = month - 1
required_day_counter = (day - 1) * 2
required_year = 3000 + year_minus_3000
```

The condition passes when the current campaign date is at or after the required
date.

## Confirmed publication table

The confirmed table begins at `DS:63F7` / file `0x00F0F7`.

A generated working table with record offsets, known text mappings, and
confidence notes is stored in:

```text
docs/NEWS_NET_PUBLICATION_TABLE_DRAFT.md
research/analysis/news_net_publication_table_draft.csv
```

```text
id 01: 3024-04-01
id 02: 3024-04-08
id 05: 3024-04-15
id 04: 3024-05-01
id 2C: 3024-06-30
id 1D: 3024-07-01
id 2A: 3024-08-25
id 10: 3024-11-15
id 03: 3025-04-08
id 03: 3026-04-08
id 16: 3026-06-01
id 1E: 3026-08-15
id 1F: 3026-09-05
id 20: 3026-11-30
id 14: 3027-03-15
id 21: 3027-07-15
id 22: 3027-08-17
id 23: 3027-11-15
id 03: 3027-04-08
id 15: 3027-05-15
id 11: 3028-01-02
id 12: 3028-01-03
id 13: 3028-01-14
id 03: 3028-04-08
id 03: 3029-04-08
id 55: 3025-01-22
id 56: 3024-11-05
id 57: 3025-03-05
id 58: 3025-04-10
id 59: 3025-09-15
id 5A: 3025-02-14
id 5C: 3026-08-28
id 5D: 3028-01-12
id 5E: raw bytes 1C 01 1B
id 5F: 3027-10-02
id 60: raw bytes 1F 03 1B
id 61: raw bytes 1E 04 1B
id 62: 3027-06-08
id 63: 3027-10-31
id 70: 3027-11-28
id 65: 3028-06-30
id 66: 3028-08-13
id 67: 3028-08-22
id 68: 3028-08-30
id 69: 3028-10-11
id 6A: 3028-10-28
id 6B: 3028-11-23
id 6C: 3029-01-15
id 6D: 3029-04-15
id 6E: 3029-04-30
id 6F: 3029-05-15
```

Some late records have month/day bytes outside normal displayed ranges. Keep
the raw bytes until the associated story gates are understood.

## Open questions

- Map every message id to its final text block offset. Early confirmed examples:
  `01 -> 0x01C0B4`, `02 -> 0x01C418`, `05 -> 0x01C60A`,
  `2C -> 0x019F24`, `1D -> 0x01AD7D`, `2A -> 0x01A192`,
  `10 -> 0x01BEEE`, `03 -> 0x01C906`.
- The April 3025 tail is inferred from the original record order plus the
  observed NEWS5 order as `55 -> 0x01E383`, `56 -> 0x01E668`,
  `57 -> 0x01E8F3`, `58 -> 0x01EA9C`, `5A -> 0x01F01F`. This should still
  be verified with narrower original-game date cuts.
- Fully name the navigation state variables around `DS:5E6A` and `DS:5E6C`.
- Determine exactly how the original chooses the visible start point that makes
  NEWS NET expose only the recent tail of the publication list.
- Distinguish plain date-driven NEWS records from story-gated or personal
  messages that share nearby text storage.
