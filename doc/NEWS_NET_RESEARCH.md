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
article type. Message seen/read state uses `DS:0ABA + message_id`, which maps
to save offset `0x0672 + message_id`. Confirmed save examples:

```text
message id 0x01 -> DS:0ABB -> save 0x0673
message id 0x02 -> DS:0ABC -> save 0x0674
message id 0x03 -> DS:0ABD -> save 0x0675
message id 0x04 -> DS:0ABE -> save 0x0676
message id 0x05 -> DS:0ABF -> save 0x0677
message id 0x10 -> DS:0ACA -> save 0x0682
message id 0x11 -> DS:0ACB -> save 0x0683
message id 0x12 -> DS:0ACC -> save 0x0684
message id 0x13 -> DS:0ACD -> save 0x0685
message id 0x14 -> DS:0ACE -> save 0x0686
message id 0x15 -> DS:0ACF -> save 0x0687
message id 0x16 -> DS:0AD0 -> save 0x0688
message id 0x1D -> DS:0AD7 -> save 0x068F
message id 0x1E -> DS:0AD8 -> save 0x0690
message id 0x1F -> DS:0AD9 -> save 0x0691
message id 0x20 -> DS:0ADA -> save 0x0692
message id 0x21 -> DS:0ADB -> save 0x0693
message id 0x22 -> DS:0ADC -> save 0x0694
message id 0x23 -> DS:0ADD -> save 0x0695
message id 0x2A -> DS:0AE4 -> save 0x069C
message id 0x2C -> DS:0AE6 -> save 0x069E
message id 0x55 -> DS:0B0F -> save 0x06C7
message id 0x56 -> DS:0B10 -> save 0x06C8
message id 0x57 -> DS:0B11 -> save 0x06C9
message id 0x58 -> DS:0B12 -> save 0x06CA
message id 0x59 -> DS:0B13 -> save 0x06CB
message id 0x5A -> DS:0B14 -> save 0x06CC
message id 0x5C -> DS:0B16 -> save 0x06CE
message id 0x5D -> DS:0B17 -> save 0x06CF
message id 0x5E -> DS:0B18 -> save 0x06D0
message id 0x5F -> DS:0B19 -> save 0x06D1
message id 0x60 -> DS:0B1A -> save 0x06D2
message id 0x61 -> DS:0B1B -> save 0x06D3
message id 0x62 -> DS:0B1C -> save 0x06D4
message id 0x63 -> DS:0B1D -> save 0x06D5
message id 0x65 -> DS:0B1F -> save 0x06D7
message id 0x66 -> DS:0B20 -> save 0x06D8
message id 0x67 -> DS:0B21 -> save 0x06D9
message id 0x68 -> DS:0B22 -> save 0x06DA
message id 0x69 -> DS:0B23 -> save 0x06DB
message id 0x6A -> DS:0B24 -> save 0x06DC
message id 0x6B -> DS:0B25 -> save 0x06DD
message id 0x6C -> DS:0B26 -> save 0x06DE
message id 0x6D -> DS:0B27 -> save 0x06DF
message id 0x6E -> DS:0B28 -> save 0x06E0
message id 0x6F -> DS:0B29 -> save 0x06E1
message id 0x70 -> DS:0B2A -> save 0x06E2
```

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

A generated table with record offsets, text mappings, and confidence notes is
stored in:

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

All `51` known publication records in this block are mapped to text blocks and
have confirmed save-side read-flag observations. Some brief-headline records
have raw month/day bytes outside normal displayed ranges; those raw bytes are
preserved in the table, while the visible article title dates are documented in
the row notes.

## Save-state observations

### `SAV0.GAM -> SAV1.GAM`, 2026-07-29

Source files:

```text
C:/Users/MadCat/Desktop/saves/SAV0.GAM
C:/Users/MadCat/Desktop/saves/SAV1.GAM
```

Scenario:

- `SAV0.GAM`: saved immediately after starting a new game on Oshika.
- `SAV1.GAM`: opened NEWS NET, read the first and only available article, left
  the screen, then saved again.

Binary diff:

| Offset | `SAV0` | `SAV1` | Current interpretation |
| --- | ---: | ---: | --- |
| `.GAM 0x0031` | `0x01` | `0x02` | Month-day counter advanced one step/day. |
| `.GAM 0x0037` | `0x00` | `0x01` | 14-day periodic counter advanced with the same time step. |
| `.GAM 0x0673` | `0x00` | `0x01` | News Net seen/read flag for the first available `3024-04-01` article (`message id 0x01`, text `mw_main.news.01c0b4`). |

No mech, money, planet, family, extra-ammo, sound, or detail fields changed.
Later controlled saves confirm `0x0673` is the seen/read flag for message id
`0x01`, following the formula `save_offset = 0x0672 + message_id`.

### `SAV0.GAM -> SAV2.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV2.GAM
```

Scenario:

- Loaded `SAV0.GAM`.
- Did not open NEWS NET.
- Traveled from Oshika to nearby Matsuida, then saved.

Key result:

- `.GAM 0x0673` stays `0`, unlike `SAV1.GAM`.

This strongly suggests `0x0673` is not a generic time-passed or travel flag. It
is tied to opening/reading the first available News Net article, or to News Net
state created by visiting that screen.

Date/travel fields:

| Offset | `SAV0` | `SAV2` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0021` | `0x0A` | `0x09` | Planet changes from Oshika to Matsuida. |
| `.GAM 0x002B` | `0x91` | `0x9A` | Current planet starmap X metadata changes from `145` to `154`. |
| `.GAM 0x002D` | `0x5A` | `0x60` | Current planet starmap Y metadata changes from `90` to `96`. |
| `.GAM 0x0031` | `0x01` | `0x27` | Month-day counter advances by `38`. |
| `.GAM 0x0037` | `0x00` | `0x0A` | 14-day periodic counter advances by `38 mod 14 = 10`. |
| `.GAM 0x0049..0x004C` | `1,199,000` | `1,154,000` | Wealth decreases by `45,000`, matching one owned mech and one jump. |

The travel result matches the current starmap model: Oshika/Matsuida are one
jump apart by map coordinates, and the one-mech travel cost is
`20,000 + 25,000 * 1 = 45,000`.

### `SAV2.GAM -> SAV3.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV3.GAM
```

Scenario:

- Loaded `SAV2.GAM` on Matsuida, `3024-04-20`.
- Opened NEWS NET and read three displayed messages.
- Saved after leaving NEWS NET.

Binary diff:

| Offset | `SAV2` | `SAV3` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0031` | `0x27` | `0x28` | Month-day counter advances by one. |
| `.GAM 0x0037` | `0x0A` | `0x0B` | 14-day periodic counter advances by one. |
| `.GAM 0x0673` | `0x00` | `0x01` | Message id `0x01` seen/read. |
| `.GAM 0x0674` | `0x00` | `0x01` | Message id `0x02` seen/read. |
| `.GAM 0x0677` | `0x00` | `0x01` | Message id `0x05` seen/read. |

This confirms the message flag formula:

```text
save_seen_flag_offset = 0x0672 + message_id
```

It also confirms the time-cost rule observed in later saves: reading multiple
messages in one NEWS NET visit advances the date counters by one, not by one
per article. The cost appears to be one day per NEWS NET visit/session or per
leave/save cycle after using NEWS NET.

The attached screenshots identify the three displayed messages as the early
Ander's Moon heir item, the Great Gorge bandit item, and a Jordan Rowe
birthday/personal message. With the existing `0x01 -> Ander's Moon` and
`0x02 -> Great Gorge` mappings, the remaining new flag confirms
`id 0x05 -> mw_main.news.01c303` for the birthday/personal message.

### `SAV3.GAM -> SAV4.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV4.GAM
```

Scenario:

- Started from `SAV3.GAM` on Matsuida, `3024-04-21`, with messages
  `0x01`, `0x02`, and `0x05` already marked read.
- Traveled to Lesnovo.
- Opened NEWS NET, read the newly available messages, then saved.

Key travel/date result:

| Field | `SAV3` | `SAV4` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x09` | `0x48` | Matsuida -> Lesnovo. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `154,96` | `51,167` | Lesnovo coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `1,154,000` | `934,000` | Travel cost `220,000`, matching one mech and `8` jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | April day counter `40`, period `11` | June day counter `59`, period `10` | `138` travel days plus `1` News Net day. |

Matsuida -> Lesnovo travel math:

```text
dx = abs(154 - 51) = 103
dy = abs(96 - 167) = 71
scaled_dy = (71 * 32) / 20 = 113
distance = floor(sqrt(103*103 + 113*113)) = 152
jumps = 152 / 18 = 8
cost = 20,000 + 25,000 * 8 = 220,000
travel_days = (8 - 1) * 14 + Matsuida.factor(12) * 2 + Lesnovo.factor(8) * 2 = 138
```

News Net flag diff:

| Offset | `SAV3` | `SAV4` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0676` | `0x00` | `0x01` | Message id `0x04` seen/read. |
| `.GAM 0x069E` | `0x00` | `0x01` | Message id `0x2C` seen/read. |

Previously read flags for ids `0x01`, `0x02`, and `0x05` remain set. This
extends the confirmed flag formula to non-contiguous and higher message ids.
The visit again advances the date by one day total, not one day per newly read
article.

The attached screenshots identify the two new messages:

- `id 0x04 -> mw_main.news.01c60a`: `WANTED - 200,000 C-BILL REWARD`.
- `id 0x2C -> mw_main.news.019f24`: Susquehanna / Pirate Valasek raid.

### `SAV4.GAM -> SAV5.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV5.GAM
```

Scenario:

- Started from `SAV4.GAM` on Lesnovo, `3024-06-30`, with messages
  `0x01`, `0x02`, `0x04`, `0x05`, and `0x2C` already marked read.
- Traveled to Land's End.
- Opened NEWS NET, read the newly available messages, then saved.

Key travel/date result:

| Field | `SAV4` | `SAV5` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x48` | `0x1D` | Lesnovo -> Land's End. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `51,167` | `176,64` | Land's End coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `934,000` | `639,000` | Travel cost `295,000`, matching one mech and `11` jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | June day counter `59`, period `10` | October day counter `12`, period `7` | `192` travel days plus `1` News Net day. |

Lesnovo -> Land's End travel math:

```text
dx = abs(51 - 176) = 125
dy = abs(167 - 64) = 103
scaled_dy = (103 * 32) / 20 = 164
distance = floor(sqrt(125*125 + 164*164)) = 206
jumps = 206 / 18 = 11
cost = 20,000 + 25,000 * 11 = 295,000
travel_days = (11 - 1) * 14 + Lesnovo.factor(8) * 2 + Land's End.factor(18) * 2 = 192
```

The save date advances by `193` total day-counter units from `SAV4` to `SAV5`,
which equals `192` travel days plus one News Net day. The periodic counter also
matches: `10 + 193 = 203`, and `203 mod 14 = 7`.

News Net flag diff:

| Offset | `SAV4` | `SAV5` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x068F` | `0x00` | `0x01` | Message id `0x1D` seen/read. |
| `.GAM 0x069C` | `0x00` | `0x01` | Message id `0x2A` seen/read. |

The attached screenshots identify the two new messages:

- `id 0x1D -> mw_main.news.01ad7d`: New Earth / Alliance Games.
- `id 0x2A -> mw_main.news.01a192`: Thestria / ISP border security.

The visit again advances the date by one day total, not one day per newly read
article.

### `SAV5.GAM -> SAV6.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV6.GAM
```

Scenario:

- Started from `SAV5.GAM` on Land's End, `3024-10-07`, with messages
  `0x01`, `0x02`, `0x04`, `0x05`, `0x1D`, `0x2A`, and `0x2C` already marked
  read.
- Used the save editor to raise wealth to `10,000,000` C-bills for travel
  testing.
- Traveled to Claybrooke.
- Opened NEWS NET, read the newly available messages, then saved.

Key travel/date result:

| Field | `SAV5` | `SAV6` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x1D` | `0x4D` | Land's End -> Claybrooke. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `176,64` | `89,179` | Claybrooke coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | edited to `10,000,000` before travel | `9,705,000` | Travel cost `295,000`, matching one mech and `11` jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0035/0x0037` | October day counter `12`, year `3024`, period `7` | January day counter `47`, year `3025`, period `12` | `214` travel days plus `1` News Net day. |

Land's End -> Claybrooke travel math:

```text
dx = abs(176 - 89) = 87
dy = abs(64 - 179) = 115
scaled_dy = (115 * 32) / 20 = 184
distance = floor(sqrt(87*87 + 184*184)) = 203
jumps = 203 / 18 = 11
cost = 20,000 + 25,000 * 11 = 295,000
travel_days = (11 - 1) * 14 + Land's End.factor(18) * 2 + Claybrooke.factor(19) * 2 = 214
```

The save date advances by `215` total day-counter units from `SAV5` to `SAV6`,
which equals `214` travel days plus one News Net day. The periodic counter also
matches: `7 + 215 = 222`, and `222 mod 14 = 12`.

News Net flag diff:

| Offset | `SAV5` | `SAV6` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0682` | `0x00` | `0x01` | Message id `0x10` seen/read. |
| `.GAM 0x06C7` | `0x00` | `0x01` | Message id `0x55` seen/read. |
| `.GAM 0x06C8` | `0x00` | `0x01` | Message id `0x56` seen/read. |

Previously read flags remain set. This extends the confirmed flag formula to
the late message-id range.

The attached screenshots identify the three new messages:

- `id 0x10 -> mw_main.news.01beee`: Regis / Verthandi revolt spreads.
- `id 0x55 -> mw_main.news.01e383`: Corant City / Matabushi's new image.
- `id 0x56 -> mw_main.news.01e668`: Valensia / senior council prepares for
  fiscal battle.

The visit again advances the date by one day total, not one day per newly read
article.

### `SAV6.GAM -> SAV7.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV7.GAM
```

Scenario:

- Started from `SAV6.GAM` on Claybrooke, `3025-01-24`, with messages
  `0x01`, `0x02`, `0x04`, `0x05`, `0x10`, `0x1D`, `0x2A`, `0x2C`, `0x55`,
  and `0x56` already marked read.
- Traveled to Rasalhague.
- Opened NEWS NET, read the visible messages, then saved.

Key travel/date result:

| Field | `SAV6` | `SAV7` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x4D` | `0x17` | Claybrooke -> Rasalhague. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `89,179` | `109,55` | Rasalhague coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `9,705,000` | `9,410,000` | Travel cost `295,000`, matching one mech and `11` jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | January day counter `47`, period `12` | May day counter `40`, period `7` | `232` travel days plus `1` News Net day. |
| Raw age byte, `.GAM 0x0047` | `18` | `19` | Age-related byte increments after crossing the 3025 birthday window; exact rule still needs a focused test. |

Claybrooke -> Rasalhague travel math:

```text
dx = abs(89 - 109) = 20
dy = abs(179 - 55) = 124
scaled_dy = (124 * 32) / 20 = 198
distance = floor(sqrt(20*20 + 198*198)) = 199
jumps = 199 / 18 = 11
cost = 20,000 + 25,000 * 11 = 295,000
travel_days = (11 - 1) * 14 + Claybrooke.factor(19) * 2 + Rasalhague.factor(27) * 2 = 232
```

The save date advances by `233` total day-counter units from `SAV6` to `SAV7`,
which equals `232` travel days plus one News Net day. The periodic counter also
matches: `12 + 233 = 245`, and `245 mod 14 = 7`.

News Net flag diff:

| Offset | `SAV6` | `SAV7` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0675` | `0x00` | `0x01` | Message id `0x03` seen/read. |
| `.GAM 0x06C9` | `0x00` | `0x01` | Message id `0x57` seen/read. |
| `.GAM 0x06CA` | `0x00` | `0x01` | Message id `0x58` seen/read. |
| `.GAM 0x06CC` | `0x00` | `0x01` | Message id `0x5A` seen/read. |

The supplied screenshots show five visible pages, but only four new read flags
are created. The Matabushi article (`id 0x55`) was already marked read in
`SAV6` and remains visible in this News Net window. Therefore visibility in the
current News Net list is not the same thing as unread state.

The observed page order is `0x03 -> 0x55 -> 0x57 -> 0x58 -> 0x5A`, which
matches original publication-record order rather than chronological order.
However, the filter is not yet fully explained: `0x56` is also already read and
sits between `0x55` and `0x57` in the publication records, but it is not visible
in this five-page window.

The screenshots identify the newly read messages:

- `id 0x03 -> mw_main.news.01c906`: Jordan Rowe personal birthday message.
- `id 0x57 -> mw_main.news.01e8f3`: Valensia / financial services company
  opening.
- `id 0x58 -> mw_main.news.01ea9c`: Valensia / Ander's Moon economy reeling.
- `id 0x5A -> mw_main.news.01f01f`: Valensia / new alliances are forming.

The visit again advances the date by one day total, not one day per newly read
article.

### `SAV7.GAM -> SAV8.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV8.GAM
```

Scenario:

- Started from `SAV7.GAM` on Rasalhague, `3025-05-21`.
- Traveled to Zanzibar.
- Opened NEWS NET and viewed the currently visible messages, then saved.

Key travel/date result:

| Field | `SAV7` | `SAV8` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x17` | `0x71` | Rasalhague -> Zanzibar. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `109,55` | `123,177` | Zanzibar coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `9,410,000` | `9,140,000` | Travel cost `270,000`, matching one mech and `10` jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | May day counter `40`, period `7` | August day counter `51`, period `2` | `190` travel days plus `1` News Net day. |

Rasalhague -> Zanzibar travel math:

```text
dx = abs(109 - 123) = 14
dy = abs(55 - 177) = 122
scaled_dy = (122 * 32) / 20 = 195
distance = floor(sqrt(14*14 + 195*195)) = 195
jumps = 195 / 18 = 10
cost = 20,000 + 25,000 * 10 = 270,000
travel_days = (10 - 1) * 14 + Rasalhague.factor(27) * 2 + Zanzibar.factor(5) * 2 = 190
```

The save date advances by `191` total day-counter units from `SAV7` to `SAV8`,
which equals `190` travel days plus one News Net day. The periodic counter also
matches: `7 + 191 = 198`, and `198 mod 14 = 2`.

News Net flags:

No bytes in the message seen/read flag area changed. The complete set of read
message ids remains:

```text
01, 02, 03, 04, 05, 10, 1D, 2A, 2C, 55, 56, 57, 58, 5A, 74
```

The supplied screenshots show only already-read messages:

- `id 0x03`: Jordan Rowe personal birthday message.
- `id 0x57`: Valensia / financial services company opening.
- `id 0x58`: Valensia / Ander's Moon economy reeling.

This confirms that the current News Net visible window can shrink or shift
without adding new read flags. It also confirms that opening/using News Net can
advance the date by one day even when no new messages are marked read.

### `SAV8.GAM -> SAV9.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV9.GAM
```

Scenario:

- Started from `SAV8.GAM` on Zanzibar, `3025-08-26`.
- Traveled to Anywhere.
- Opened NEWS NET, read the newly visible message, then saved.

Key travel/date result:

| Field | `SAV8` | `SAV9` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x71` | `0x32` | Zanzibar -> Anywhere. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `123,177` | `69,49` | Anywhere coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `9,140,000` | `8,845,000` | Travel cost `295,000`, matching one mech and `11` jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | August day counter `51`, period `2` | November day counter `46`, period `9` | `174` travel days plus `1` News Net day. |

Zanzibar -> Anywhere travel math:

```text
dx = abs(123 - 69) = 54
dy = abs(177 - 49) = 128
scaled_dy = (128 * 32) / 20 = 204
distance = floor(sqrt(54*54 + 204*204)) = 211
jumps = 211 / 18 = 11
cost = 20,000 + 25,000 * 11 = 295,000
travel_days = (11 - 1) * 14 + Zanzibar.factor(5) * 2 + Anywhere.factor(12) * 2 = 174
```

The save date advances by `175` total day-counter units from `SAV8` to `SAV9`,
which equals `174` travel days plus one News Net day. The periodic counter also
matches: `2 + 175 = 177`, and `177 mod 14 = 9`.

News Net flag diff:

| Offset | `SAV8` | `SAV9` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x06CB` | `0x00` | `0x01` | Message id `0x59` seen/read. |

The screenshot identifies the new message:

- `id 0x59 -> mw_main.news.01ed2c`: Valensia / Sacrificial Forests and the
  Money God.

### `SAV9.GAM -> SAV10.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV10.GAM
```

Scenario:

- Started from `SAV9.GAM` on Anywhere, `3025-11-24`.
- Traveled to Hyalite.
- Opened NEWS NET. The feed displayed no messages, old or new.
- Saved after leaving NEWS NET.

Key travel/date result:

| Field | `SAV9` | `SAV10` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x32` | `0x7F` | Anywhere -> Hyalite. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `69,49` | `150,174` | Hyalite coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `8,845,000` | `8,550,000` | Travel cost `295,000`, matching one mech and `11` jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0035/0x0037` | November 3025 day counter `46`, period `9` | March 3026 day counter `17`, period `10` | `210` travel days plus `1` News Net day. |
| Raw age byte, `.GAM 0x0047` | `19` | `20` | Age-related byte increments across the yearly birthday window. |

Anywhere -> Hyalite travel math:

```text
dx = abs(69 - 150) = 81
dy = abs(49 - 174) = 125
scaled_dy = (125 * 32) / 20 = 200
distance = floor(sqrt(81*81 + 200*200)) = 215
jumps = 215 / 18 = 11
cost = 20,000 + 25,000 * 11 = 295,000
travel_days = (11 - 1) * 14 + Anywhere.factor(12) * 2 + Hyalite.factor(23) * 2 = 210
```

The save date advances by `211` total day-counter units from `SAV9` to
`SAV10`, which equals `210` travel days plus one News Net day. The periodic
counter also matches: `9 + 211 = 220`, and `220 mod 14 = 10`.

News Net flags:

No bytes in the message seen/read flag area changed. The complete set of read
message ids remains:

```text
01, 02, 03, 04, 05, 10, 1D, 2A, 2C, 55, 56, 57, 58, 59, 5A, 74
```

This is the first confirmed empty-feed save in the current travel chain. It
proves that the visible News Net window can contain no messages at all while
the read-flag history remains populated and unchanged.

### `SAV10.GAM -> SAV11.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV11.GAM
```

Scenario:

- Started from `SAV10.GAM` on Hyalite, `3026-03-09`, with an empty News Net
  feed observed there.
- Traveled to Anembo.
- Opened NEWS NET, read the visible messages, then saved.

Key travel/date result:

| Field | `SAV10` | `SAV11` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x7F` | `0x34` | Hyalite -> Anembo. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `150,174` | `38,71` | Anembo coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `8,550,000` | `8,255,000` | Travel cost `295,000`, matching one mech and `11` jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | March 3026 day counter `17`, period `10` | June 3026 day counter `32`, period `9` | `194` travel days plus `1` News Net day. |

Hyalite -> Anembo travel math:

```text
dx = abs(150 - 38) = 112
dy = abs(174 - 71) = 103
scaled_dy = (103 * 32) / 20 = 164
distance = floor(sqrt(112*112 + 164*164)) = 198
jumps = 198 / 18 = 11
cost = 20,000 + 25,000 * 11 = 295,000
travel_days = (11 - 1) * 14 + Hyalite.factor(23) * 2 + Anembo.factor(4) * 2 = 194
```

The save date advances by `195` total day-counter units from `SAV10` to
`SAV11`, which equals `194` travel days plus one News Net day. The periodic
counter also matches: `10 + 195 = 205`, and `205 mod 14 = 9`.

News Net flag diff:

| Offset | `SAV10` | `SAV11` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0688` | `0x00` | `0x01` | Message id `0x16` seen/read. |

The supplied screenshots show two visible pages. Only one new read flag is
created:

- `id 0x16 -> mw_main.news.01afd5`: Ministry of Peaceful Order and Honor /
  Wanted - 50,000 C-bill reward.

The Jordan Rowe birthday message (`id 0x03`) was already marked read and is
visible again after being absent from the empty Hyalite feed. Because the
publication table contains annual `0x03` records, this is probably a newly sent
yearly birthday message that reuses the same message id and read flag. The
visible News Net window is still independent from unread/read state.

### `SAV11.GAM -> SAV12.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV12.GAM
```

Scenario:

- Started from `SAV11.GAM` on Anembo, `3026-06-17`.
- Traveled to Okefenokee.
- Opened NEWS NET, read the visible messages, then saved.

Key travel/date result:

| Field | `SAV11` | `SAV12` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x34` | `0x7B` | Anembo -> Okefenokee. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `38,71` | `215,125` | Okefenokee coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `8,255,000` | `7,985,000` | Travel cost `270,000`, matching one mech and `10` jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | June 3026 day counter `32`, period `9` | September 3026 day counter `1`, period `4` | `148` travel days plus `1` News Net day. |

Anembo -> Okefenokee travel math:

```text
dx = abs(38 - 215) = 177
dy = abs(71 - 125) = 54
scaled_dy = (54 * 32) / 20 = 86
distance = floor(sqrt(177*177 + 86*86)) = 196
jumps = 196 / 18 = 10
cost = 20,000 + 25,000 * 10 = 270,000
travel_days = (10 - 1) * 14 + Anembo.factor(4) * 2 + Okefenokee.factor(7) * 2 = 148
```

The save date advances by `149` total day-counter units from `SAV11` to
`SAV12`, which equals `148` travel days plus one News Net day. The periodic
counter also matches: `9 + 149 = 158`, and `158 mod 14 = 4`.

News Net flag diff:

| Offset | `SAV11` | `SAV12` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0690` | `0x00` | `0x01` | Message id `0x1E` seen/read. |
| `.GAM 0x06CE` | `0x00` | `0x01` | Message id `0x5C` seen/read. |

The supplied screenshots show four visible pages. Two create new read flags:

- `id 0x1E -> mw_main.news.01ac1a`: New Avalon / AFFS announces massive
  wargames.
- `id 0x5C -> mw_main.news.01f292`: Valensia / Winemakers make dramatic
  comeback.

Two visible pages do not create new flags:

- `id 0x03`: annual Jordan Rowe birthday message.
- `id 0x16`: Wanted - 50,000 C-bill reward, already read in `SAV11`.

### `SAV12.GAM -> SAV13.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV13.GAM
```

Scenario:

- Started from `SAV12.GAM` on Okefenokee, `3026-09-01`.
- Traveled to Poulsbo.
- Opened NEWS NET, read the visible messages, then saved.

Key travel/date result:

| Field | `SAV12` | `SAV13` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x7B` | `0x22` | Okefenokee -> Poulsbo. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `215,125` | `26,127` | Poulsbo coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `7,985,000` | `7,715,000` | Travel cost `270,000`, matching one mech and `10` jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | September 3026 day counter `1`, period `4` | November 3026 day counter `44`, period `13` | `162` travel days plus `1` News Net day. |

Okefenokee -> Poulsbo travel math:

```text
dx = abs(215 - 26) = 189
dy = abs(125 - 127) = 2
scaled_dy = (2 * 32) / 20 = 3
distance = floor(sqrt(189*189 + 3*3)) = 189
jumps = 189 / 18 = 10
cost = 20,000 + 25,000 * 10 = 270,000
travel_days = (10 - 1) * 14 + Okefenokee.factor(7) * 2 + Poulsbo.factor(11) * 2 = 162
```

The save date advances by `163` total day-counter units from `SAV12` to
`SAV13`, which equals `162` travel days plus one News Net day. The periodic
counter also matches: `4 + 163 = 167`, and `167 mod 14 = 13`.

News Net flag diff:

| Offset | `SAV12` | `SAV13` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0691` | `0x00` | `0x01` | Message id `0x1F` seen/read. |

The supplied screenshots show four visible pages. Only one creates a new read
flag:

- `id 0x1F -> mw_main.news.01a9f0`: New Avalon / Operation Galahad off and
  running.

The other visible pages were already marked read:

- `id 0x16`: Wanted - 50,000 C-bill reward.
- `id 0x1E`: AFFS announces massive wargames.
- `id 0x5C`: Winemakers make dramatic comeback.

### `SAV13.GAM -> SAV14.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV14.GAM
```

Scenario:

- Started from `SAV13.GAM` on Poulsbo, `3026-11-23`.
- Traveled to Greely.
- Opened NEWS NET, read the visible messages, then saved.

Key travel/date result:

| Field | `SAV13` | `SAV14` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x22` | `0x87` | Poulsbo -> Greely. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `26,127` | `189,112` | Greely coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `7,715,000` | `7,495,000` | Travel cost `220,000`, matching one mech and `8` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0035/0x0037` | November 3026 day counter `44`, period `13` | January 3027 day counter `54`, period `3` | `128` travel days plus `2` News Net openings. |
| Displayed age | `20` | `21` | The editor-derived displayed age advances after crossing the birthday window; raw age byte stays `20`. |

Poulsbo -> Greely current travel math:

```text
dx = abs(26 - 189) = 163
dy = abs(127 - 112) = 15
scaled_dy = (15 * 32) / 20 = 24
distance = floor(sqrt(163*163 + 24*24)) = 164
charged_jumps_from_money = (220,000 - 20,000) / 25,000 = 8
```

The user confirmed NEWS NET was opened twice while taking screenshots. The
observed date advance is therefore explained: `128` travel days plus two News
Net day advances equals `130`. The periodic counter agrees with that:
`13 + 130 = 143`, and `143 mod 14 = 3`.

News Net flag diff:

| Offset | `SAV13` | `SAV14` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0692` | `0x00` | `0x01` | Message id `0x20` seen/read. |

The supplied screenshots show four visible pages. Only one creates a new read
flag:

- `id 0x20 -> mw_main.news.01a84d`: New Avalon / Operation Galahad ends.

The other visible pages were already marked read:

- `id 0x1E`: AFFS announces massive wargames.
- `id 0x1F`: Operation Galahad off and running.
- `id 0x5C`: Winemakers make dramatic comeback.

### `SAV14.GAM -> SAV15.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV15.GAM
```

Scenario:

- Started from `SAV14.GAM` on Greely, `3027-01-28`.
- Traveled to Valloire.
- Opened NEWS NET, read the visible messages, then saved.

Key travel/date result:

| Field | `SAV14` | `SAV15` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x87` | `0x38` | Greely -> Valloire. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `189,112` | `23,112` | Valloire coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `7,495,000` | `7,275,000` | Travel cost `220,000`, matching one mech and `8` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | January 3027 day counter `54`, period `3` | April 3027 day counter `1`, period `4` | `126` travel days plus `1` News Net day. |
| Raw age byte, `.GAM 0x0047` | `20` | `21` | Age-related byte increments across the yearly birthday window. |

Greely -> Valloire travel math:

```text
dx = abs(189 - 23) = 166
dy = abs(112 - 112) = 0
charged_jumps_from_money = (220,000 - 20,000) / 25,000 = 8
travel_days = (8 - 1) * 14 + Greely.factor(4) * 2 + Valloire.factor(10) * 2 = 126
```

The save date advances by `127` total day-counter units from `SAV14` to
`SAV15`, which equals `126` travel days plus one News Net day. The periodic
counter also matches: `3 + 127 = 130`, and `130 mod 14 = 4`.

News Net flag diff:

| Offset | `SAV14` | `SAV15` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0686` | `0x00` | `0x01` | Message id `0x14` seen/read. |

The supplied screenshots show two visible pages. Only one creates a new read
flag:

- `id 0x14 -> mw_main.news.01b3c6`: Tiantan / Outlaw mercenaries massacre
  millions.

The other visible page was already marked read:

- `id 0x20`: Operation Galahad ends.

### `SAV15.GAM -> SAV16.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV16.GAM
```

Scenario:

- Started from `SAV15.GAM` on Valloire, `3027-04-01`.
- Traveled to Tabayama.
- Opened NEWS NET, read the visible messages, then saved.

Key travel/date result:

| Field | `SAV15` | `SAV16` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x38` | `0x04` | Valloire -> Tabayama. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `23,112` | `179,79` | Tabayama coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `7,275,000` | `7,055,000` | Travel cost `220,000`, matching one mech and `8` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | April 3027 day counter `1`, period `4` | June 3027 day counter `20`, period `3` | `138` travel days plus `1` News Net day. |
| Candidate trailing state, `.GAM 0x070E` | `0x01` | `0x02` | Changes here, but not yet identified as NEWS-specific. |

Valloire -> Tabayama travel math:

```text
dx = abs(23 - 179) = 156
dy = abs(112 - 79) = 33
charged_jumps_from_money = (220,000 - 20,000) / 25,000 = 8
travel_days = (8 - 1) * 14 + Valloire.factor(10) * 2 + Tabayama.factor(10) * 2 = 138
```

The save date advances by `139` total day-counter units from `SAV15` to
`SAV16`, which equals `138` travel days plus one News Net day. The periodic
counter also matches: `4 + 139 = 143`, and `143 mod 14 = 3`.

News Net flag diff:

| Offset | `SAV15` | `SAV16` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0687` | `0x00` | `0x01` | Message id `0x15` seen/read. |
| `.GAM 0x06D4` | `0x00` | `0x01` | Message id `0x62` seen/read. |

The supplied screenshots show four visible pages. Two create new read flags:

- `id 0x15 -> mw_main.news.01b214`: Helmdown / Gray Death cleared of Sirius V
  massacre.
- `id 0x62 -> mw_main.news.01fa8d`: Brief Headlines / Xiang's revenge
  continues, Phillip Capet killed in the games.

The other visible pages were already marked read:

- `id 0x14`: Outlaw mercenaries massacre millions.
- `id 0x03`: annual Jordan Rowe birthday message.

### `SAV16.GAM -> SAV17.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV17.GAM
```

Scenario:

- Started from `SAV16.GAM` on Tabayama, `3027-06-11`.
- Traveled to Timbiqui.
- Opened NEWS NET, read the visible messages, then saved.

Key travel/date result:

| Field | `SAV16` | `SAV17` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x04` | `0x35` | Tabayama -> Timbiqui. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `179,79` | `34,128` | Timbiqui coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `7,055,000` | `6,835,000` | Travel cost `220,000`, matching one mech and `8` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | June 3027 day counter `20`, period `3` | August 3027 day counter `35`, period `12` | `134` travel days plus `1` News Net day. |
| Candidate trailing state, `.GAM 0x070E` | `0x02` | `0x03` | Changes here again, but still not identified as NEWS-specific. |

Tabayama -> Timbiqui travel math:

```text
dx = abs(179 - 34) = 145
dy = abs(79 - 128) = 49
charged_jumps_from_money = (220,000 - 20,000) / 25,000 = 8
travel_days = (8 - 1) * 14 + Tabayama.factor(10) * 2 + Timbiqui.factor(8) * 2 = 134
```

The save date advances by `135` total day-counter units from `SAV16` to
`SAV17`, which equals `134` travel days plus one News Net day. The periodic
counter also matches: `3 + 135 = 138`, and `138 mod 14 = 12`.

News Net flag diff:

| Offset | `SAV16` | `SAV17` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0693` | `0x00` | `0x01` | Message id `0x21` seen/read. |
| `.GAM 0x0694` | `0x00` | `0x01` | Message id `0x22` seen/read. |

The supplied screenshots show six visible pages. Two create new read flags:

- `id 0x21 -> mw_main.news.01a6a6`: New Avalon / Operation Galahad to be
  repeated.
- `id 0x22 -> mw_main.news.01a4fb`: New Avalon / Galahad 3027 is launched;
  LCAF announces Thor operation.

The other visible pages were already marked read:

- `id 0x14`: Outlaw mercenaries massacre millions.
- `id 0x03`: annual Jordan Rowe birthday message.
- `id 0x15`: Gray Death cleared of Sirius V massacre.
- `id 0x62`: Brief Headlines / Xiang's revenge continues.

### `SAV17.GAM -> SAV18.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV18.GAM
```

Scenario:

- Started from `SAV17.GAM` on Timbiqui, `3027-08-18`.
- Traveled to Qandahar.
- Opened NEWS NET, read the visible messages, then saved.

Key travel/date result:

| Field | `SAV17` | `SAV18` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x35` | `0x02` | Timbiqui -> Qandahar. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `34,128` | `162,59` | Qandahar coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `6,835,000` | `6,590,000` | Travel cost `245,000`, matching one mech and `9` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | August 3027 day counter `35`, period `12` | November 3027 day counter `0`, period `3` | `144` travel days plus `1` News Net day. |
| Candidate trailing state, `.GAM 0x070E` | `0x03` | `0x01` | Changes again; still only a candidate trailing state byte. |

Timbiqui -> Qandahar travel math:

```text
dx = abs(34 - 162) = 128
dy = abs(128 - 59) = 69
charged_jumps_from_money = (245,000 - 20,000) / 25,000 = 9
travel_days = (9 - 1) * 14 + Timbiqui.factor(8) * 2 + Qandahar.factor(8) * 2 = 144
```

The save date advances by `145` total day-counter units from `SAV17` to
`SAV18`, which equals `144` travel days plus one News Net day. The periodic
counter also matches: `12 + 145 = 157`, and `157 mod 14 = 3`.

News Net flag diff:

| Offset | `SAV17` | `SAV18` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x06D1` | `0x00` | `0x01` | Message id `0x5F` seen/read. |
| `.GAM 0x06D5` | `0x00` | `0x01` | Message id `0x63` seen/read. |

The supplied screenshots show six visible pages. Two create new read flags:

- `id 0x5F -> mw_main.news.01f8dc`: Brief Headlines / Allard trial ends in
  confusion. The publication condition is `3027-10-02`, while the text title
  line says `30 JAN 3027`.
- `id 0x63 -> mw_main.news.01fb1e`: Brief Headlines / Hanse Davion and
  Melissa Steiner to wed; Morgan Kell comes out of retirement.

The other visible pages were already marked read:

- `id 0x21`: Operation Galahad to be repeated.
- `id 0x22`: Galahad 3027 is launched.
- `id 0x15`: Gray Death cleared of Sirius V massacre.
- `id 0x62`: Brief Headlines / Xiang's revenge continues.

### `SAV18.GAM -> SAV19.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV19.GAM
```

Scenario:

- Started from `SAV18.GAM` on Qandahar, `3027-11-01`.
- Traveled to Menke.
- Opened NEWS NET, read a long visible message run, then saved.

Key travel/date result:

| Field | `SAV18` | `SAV19` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x02` | `0x5E` | Qandahar -> Menke. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `162,59` | `115,170` | Menke coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `6,590,000` | `6,320,000` | Travel cost `270,000`, matching one mech and `10` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0035/0x0037` | November 3027 day counter `0`, period `3` | January 3028 day counter `29`, period `12` | `148` travel-day counter units plus `1` News Net day. |
| Candidate trailing state, `.GAM 0x070E` | `0x01` | `0x01` | Unchanged in this hop; still only a candidate trailing state byte. |

Qandahar -> Menke travel math:

```text
dx = abs(162 - 115) = 47
dy = abs(59 - 170) = 111
charged_jumps_from_money = (270,000 - 20,000) / 25,000 = 10
travel_days = (10 - 1) * 14 + Qandahar.factor(8) * 2 + Menke.factor(3) * 2 = 148
```

The save date advances by `149` total day-counter units from `SAV18` to
`SAV19`, which equals `148` travel units plus one News Net day. The periodic
counter also matches: `3 + 149 = 152`, and `152 mod 14 = 12`.

The displayed pilot age is now `22`, while the raw age/start-age byte remains
`21`. This supports the current editor interpretation that displayed age is
derived from the current campaign date plus the stored start-age byte.

News Net flag diff:

| Offset | `SAV18` | `SAV19` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x0683` | `0x00` | `0x01` | Message id `0x11` seen/read. |
| `.GAM 0x0684` | `0x00` | `0x01` | Message id `0x12` seen/read. |
| `.GAM 0x0685` | `0x00` | `0x01` | Message id `0x13` seen/read. |
| `.GAM 0x0695` | `0x00` | `0x01` | Message id `0x23` seen/read. |
| `.GAM 0x06CF` | `0x00` | `0x01` | Message id `0x5D` seen/read. |
| `.GAM 0x06D0` | `0x00` | `0x01` | Message id `0x5E` seen/read. |
| `.GAM 0x06D2` | `0x00` | `0x01` | Message id `0x60` seen/read. |
| `.GAM 0x06D3` | `0x00` | `0x01` | Message id `0x61` seen/read. |
| `.GAM 0x06E2` | `0x00` | `0x01` | Message id `0x70` seen/read. |

Newly observed visible pages:

- `id 0x23 -> mw_main.news.01a30c`: New Avalon / Galahad 3027 and Operation
  Thor end. The publication condition is `3027-11-15`, while the text title
  line says `6 November 3027`.
- `id 0x11 -> mw_main.news.01b971`: Cerant, An Ting / rioting shakes capital
  of An Ting.
- `id 0x12 -> mw_main.news.01bc2f`: Cerant, An Ting / ComStar facility
  attacked and Hephaestus destroyed.
- `id 0x13 -> mw_main.news.01b667`: Cerant, An Ting / Wolf's Dragoons defeat
  Kurita Ryuken.
- `id 0x5D -> mw_main.news.01f545`: Valensia / alleged McBrin crimes fall on
  deaf ears.
- `id 0x5E -> mw_main.news.01f82e`: Brief Headlines / Justin Allard goes on
  trial for treason. The publication record bytes are `5E 03 01 1C 01 1B FF`;
  this uses invalid raw month `0x1C`, while the text title date says
  `20 JAN 3027`.
- `id 0x60 -> mw_main.news.01f96d`: Brief Headlines / Justin Xiang seeks
  vengence. The publication record bytes are `60 03 01 1F 03 1B FF`; this uses
  invalid raw month `0x1F`, while the text title date says `20 MAR 3027`.
- `id 0x61 -> mw_main.news.01f9fe`: Brief Headlines / Xiang continues
  meteroic rise, slays Billy Wolfson. The publication record bytes are
  `61 03 01 1E 04 1B FF`; this uses invalid raw month `0x1E`, while the text
  title date says `20 APR 3027`.
- `id 0x70 -> mw_main.news.01fc47`: Brief Headlines / Captain A. Redburn and
  other First Training Battalion members attacked.

The other visible pages were already marked read:

- `id 0x22`: Galahad 3027 is launched.
- `id 0x5F`: Brief Headlines / Allard trial ends in confusion.
- `id 0x63`: Brief Headlines / Hanse Davion and Melissa Steiner to wed;
  Morgan Kell comes out of retirement.

### `SAV19.GAM -> SAV20.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV20.GAM
```

Scenario:

- Started from `SAV19.GAM` on Menke, `3028-01-15`.
- Traveled to Kirchbach.
- Opened NEWS NET, viewed the existing message run, then saved.

Key travel/date result:

| Field | `SAV19` | `SAV20` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x5E` | `0x16` | Menke -> Kirchbach. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `115,170` | `94,56` | Kirchbach coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `6,320,000` | `6,050,000` | Travel cost `270,000`, matching one mech and `10` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | January 3028 day counter `29`, period `12` | April 3028 day counter `2`, period `11` | `152` travel-day counter units plus `1` News Net day. |
| Raw age byte, `.GAM 0x0047` | `21` | `22` | The original save now refreshes the age-related byte after the displayed age had already advanced in `SAV19`. |
| Candidate trailing state, `.GAM 0x070E` | `0x01` | `0x02` | Changes again; still only a candidate trailing state byte. |

Menke -> Kirchbach travel math:

```text
dx = abs(115 - 94) = 21
dy = abs(170 - 56) = 114
charged_jumps_from_money = (270,000 - 20,000) / 25,000 = 10
travel_days = (10 - 1) * 14 + Menke.factor(3) * 2 + Kirchbach.factor(10) * 2 = 152
```

The save date advances by `153` total day-counter units from `SAV19` to
`SAV20`, which equals `152` travel units plus one News Net day. The periodic
counter also matches: `12 + 153 = 165`, and `165 mod 14 = 11`.

No bytes in the message seen/read flag area changed. The complete set of read
message ids is unchanged from `SAV19`, so this is a useful negative control:
viewing the Kirchbach feed on this date did not mark any new messages.

The supplied screenshots show only already-read visible pages:

- `id 0x23`: Galahad 3027 and Operation Thor end.
- `id 0x11`: Rioting shakes capital of An Ting.
- `id 0x12`: ComStar facility attacked and Hephaestus destroyed.
- `id 0x13`: Wolf's Dragoons defeat Kurita Ryuken.
- `id 0x5D`: Alleged McBrin crimes fall on deaf ears.
- `id 0x5E`: Brief Headlines / Justin Allard goes on trial for treason.
- `id 0x5F`: Brief Headlines / Allard trial ends in confusion.
- `id 0x60`: Brief Headlines / Justin Xiang seeks vengence.
- `id 0x61`: Brief Headlines / Xiang continues meteroic rise.
- `id 0x63`: Brief Headlines / Hanse Davion and Melissa Steiner to wed;
  Morgan Kell comes out of retirement.
- `id 0x70`: Brief Headlines / Captain A. Redburn and other First Training
  Battalion members attacked.

### `SAV20.GAM -> SAV21.GAM`, 2026-07-29

Source file:

```text
C:/Users/MadCat/Desktop/saves/SAV21.GAM
```

Scenario:

- Started from `SAV20.GAM` on Kirchbach, `3028-04-02`.
- Traveled to Frazer.
- Opened NEWS NET, viewed the existing message run, then saved.

Key travel/date result:

| Field | `SAV20` | `SAV21` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x16` | `0x89` | Kirchbach -> Frazer. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `94,56` | `126,171` | Frazer coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `6,050,000` | `5,780,000` | Travel cost `270,000`, matching one mech and `10` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | April 3028 day counter `2`, period `11` | June 3028 day counter `53`, period `0` | `170` travel-day counter units plus `1` News Net day. |
| Candidate trailing state, `.GAM 0x070E` | `0x02` | `0x01` | Changes again; still only a candidate trailing state byte. |

Kirchbach -> Frazer travel math:

```text
dx = abs(94 - 126) = 32
dy = abs(56 - 171) = 115
charged_jumps_from_money = (270,000 - 20,000) / 25,000 = 10
travel_days = (10 - 1) * 14 + Kirchbach.factor(10) * 2 + Frazer.factor(12) * 2 = 170
```

The save date advances by `171` total day-counter units from `SAV20` to
`SAV21`, which equals `170` travel units plus one News Net day. The periodic
counter also matches: `11 + 171 = 182`, and `182 mod 14 = 0`.

No bytes in the message seen/read flag area changed. The complete set of read
message ids is unchanged from `SAV20`, so Frazer is another negative control:
by `3028-06-27`, opening the feed did not mark any new messages. This is just
before the draft table's next publication record, `id 0x65`, dated
`3028-06-30`.

The supplied screenshots show only already-read visible pages:

- `id 0x61`: Brief Headlines / Xiang continues meteoric rise.
- `id 0x11`: Rioting shakes capital of An Ting.
- `id 0x12`: ComStar facility attacked and Hephaestus destroyed.
- `id 0x13`: Wolf's Dragoons defeat Kurita Ryuken.
- `id 0x03`: Personal Jordan Rowe birthday message.
- `id 0x5D`: Alleged McBrin crimes fall on deaf ears.
- `id 0x5E`: Brief Headlines / Justin Allard goes on trial for treason.
- `id 0x60`: Brief Headlines / Justin Xiang seeks vengence.

### `SAV21.GAM -> SAV22.GAM`, 2026-07-29

Source files:

```text
C:/Users/MadCat/Desktop/saves/SAV21.GAM
C:/Users/MadCat/Desktop/saves/SAV22.GAM
```

Scenario:

- Started from `SAV21.GAM` on Frazer, `3028-06-27`.
- Traveled to Albiero.
- Opened NEWS NET, viewed the visible message run, then saved.

Key travel/date result:

| Field | `SAV21` | `SAV22` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x89` | `0x18` | Frazer -> Albiero. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `126,171` | `128,58` | Albiero coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `5,780,000` | `5,510,000` | Travel cost `270,000`, matching one mech and `10` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | June 3028 day counter `53`, period `0` | September 3028 day counter `30`, period `3` | `156` travel-day counter units plus `1` News Net day. |

Frazer -> Albiero travel math:

```text
dx = abs(126 - 128) = 2
dy = abs(171 - 58) = 113
scaled_dy = (113 * 32) / 20 = 180
distance = floor(sqrt(2*2 + 180*180)) = 180
charged_jumps_from_money = (270,000 - 20,000) / 25,000 = 10
travel_days = (10 - 1) * 14 + Frazer.factor(12) * 2 + Albiero.factor(3) * 2 = 156
```

The save date advances by `157` total day-counter units from `SAV21` to
`SAV22`, which equals `156` travel units plus one News Net day. The periodic
counter also matches: `0 + 157 = 157`, and `157 mod 14 = 3`.

News Net flag diff:

| Offset | `SAV21` | `SAV22` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x06D7` | `0x00` | `0x01` | Message id `0x65` seen/read. |
| `.GAM 0x06D8` | `0x00` | `0x01` | Message id `0x66` seen/read. |
| `.GAM 0x06D9` | `0x00` | `0x01` | Message id `0x67` seen/read. |
| `.GAM 0x06DA` | `0x00` | `0x01` | Message id `0x68` seen/read. |

The supplied screenshots show eight visible pages. Four create new read flags:

- `id 0x65 -> mw_main.headline.01fcfa`: Brief Headlines / Operations Gallahad
  and Thor 3028 to proceed as regularly scheduled.
- `id 0x66 -> mw_main.headline.01fd83`: Brief Headlines / Wolf's Dragoons
  officially stationed in Davion space; wedding guests begin to arrive.
- `id 0x67 -> mw_main.headline.01fea8`: Brief Headlines / Hanse Davion and
  Melissa Steiner wed; Hanse Davion declares war on House Liao.
- `id 0x68 -> mw_main.headline.01ffb8`: Brief Headlines / Fed Suns attack
  multiple worlds; Commonwealth attacks Kurita systems; Shensi falls to Davion.

The other visible pages were already marked read:

- `id 0x03`: Personal Jordan Rowe birthday message.
- `id 0x5E`: Brief Headlines / Justin Allard goes on trial for treason.
- `id 0x60`: Brief Headlines / Justin Xiang seeks vengence.
- `id 0x61`: Brief Headlines / Xiang continues meteoric rise.

Many bytes in candidate campaign/story/market regions also change during this
hop, but the confirmed News Net-specific changes are only the four message
seen/read flags above. Mech state, current ammo, extra ammo, family attitudes,
sound, and detail settings are unchanged.

### `SAV22.GAM -> SAV23.GAM`, 2026-07-29

Source files:

```text
C:/Users/MadCat/Desktop/saves/SAV22.GAM
C:/Users/MadCat/Desktop/saves/SAV23.GAM
```

Scenario:

- Started from `SAV22.GAM` on Albiero, `3028-09-16`.
- Traveled to Buenos Aires.
- Opened NEWS NET, viewed the visible message run, then saved.

Key travel/date result:

| Field | `SAV22` | `SAV23` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x18` | `0x5C` | Albiero -> Buenos Aires. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `128,58` | `103,172` | Buenos Aires coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `5,510,000` | `5,240,000` | Travel cost `270,000`, matching one mech and `10` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | September 3028 day counter `30`, period `3` | November 3028 day counter `55`, period `8` | `144` travel-day counter units plus `1` News Net day. |

Albiero -> Buenos Aires travel math:

```text
dx = abs(128 - 103) = 25
dy = abs(58 - 172) = 114
scaled_dy = (114 * 32) / 20 = 182
distance = floor(sqrt(25*25 + 182*182)) = 183
charged_jumps_from_money = (270,000 - 20,000) / 25,000 = 10
travel_days = (10 - 1) * 14 + Albiero.factor(3) * 2 + BuenosAires.factor(6) * 2 = 144
```

The save date advances by `145` total day-counter units from `SAV22` to
`SAV23`, which equals `144` travel units plus one News Net day. The periodic
counter also matches: `3 + 145 = 148`, and `148 mod 14 = 8`.

News Net flag diff:

| Offset | `SAV22` | `SAV23` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x06DB` | `0x00` | `0x01` | Message id `0x69` seen/read. |
| `.GAM 0x06DC` | `0x00` | `0x01` | Message id `0x6A` seen/read. |
| `.GAM 0x06DD` | `0x00` | `0x01` | Message id `0x6B` seen/read. |

The supplied screenshots show ten visible pages. Three create new read flags:

- `id 0x69 -> mw_main.headline.020152`: Brief Headlines / The Fox still on
  the hunt; Tikonov Commonality is next. The publication condition is
  `3028-10-11`, while the text title line says `25 SEP 3028`.
- `id 0x6A -> mw_main.headline.0201d2`: Brief Headlines / Tikonov throws in
  the towel; Commonwealth launches a follow-up. The publication condition is
  `3028-10-28`, while the text title line says `18 OCT 3028`.
- `id 0x6B -> mw_main.headline.020328`: Brief Headlines / Fox goes for the
  throat; eight more worlds under attack. The publication condition is
  `3028-11-23`, while the text title line says `13 NOV 3028`.

The other visible pages were already marked read:

- `id 0x11`: Rioting shakes capital of An Ting.
- `id 0x60`: Brief Headlines / Justin Xiang seeks vengence.
- `id 0x61`: Brief Headlines / Xiang continues meteoric rise.
- `id 0x65`: Brief Headlines / Operations Gallahad and Thor 3028.
- `id 0x66`: Brief Headlines / Wolf's Dragoons officially stationed in
  Davion space.
- `id 0x67`: Brief Headlines / Hanse Davion and Melissa Steiner wed.
- `id 0x68`: Brief Headlines / Fed Suns and Commonwealth attacks.

Candidate campaign/story/market regions also change during this hop, but the
confirmed News Net-specific changes are only the three message seen/read flags
above. Mech state, current ammo, extra ammo, family attitudes, sound, and
detail settings are unchanged.

### `SAV23.GAM -> SAV24.GAM`, 2026-07-29

Source files:

```text
C:/Users/MadCat/Desktop/saves/SAV23.GAM
C:/Users/MadCat/Desktop/saves/SAV24.GAM
```

Scenario:

- Started from `SAV23.GAM` on Buenos Aires, `3028-11-28`.
- Traveled to Keid.
- Opened NEWS NET, viewed the visible message run, then saved.

Key travel/date result:

| Field | `SAV23` | `SAV24` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x5C` | `0x63` | Buenos Aires -> Keid. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `103,172` | `103,119` | Keid coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `5,240,000` | `5,120,000` | Travel cost `120,000`, matching one mech and `4` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0035/0x0037` | November 3028 day counter `55`, period `8` | January 3029 day counter `4`, period `7` | `68` travel-day counter units plus `1` News Net day. |
| Displayed age | `22` | `23` | The editor-derived displayed age advances after crossing into 3029; raw age byte remains `22`. |

Buenos Aires -> Keid travel math:

```text
dx = abs(103 - 103) = 0
dy = abs(172 - 119) = 53
scaled_dy = (53 * 32) / 20 = 84
distance = floor(sqrt(0*0 + 84*84)) = 84
charged_jumps_from_money = (120,000 - 20,000) / 25,000 = 4
travel_days = (4 - 1) * 14 + BuenosAires.factor(6) * 2 + Keid.factor(7) * 2 = 68
```

The save date advances by `69` total day-counter units from `SAV23` to
`SAV24`, which equals `68` travel units plus one News Net day. The periodic
counter also matches: `8 + 69 = 77`, and `77 mod 14 = 7`.

No bytes in the message seen/read flag area changed. The complete set of read
message ids is unchanged from `SAV23`, so this is another useful negative
control for News Net flagging after travel and year rollover.

The supplied screenshots show only already-read visible pages:

- `id 0x6B`: Brief Headlines / Fox goes for the throat.
- `id 0x63`: Brief Headlines / Hanse Davion and Melissa Steiner to wed;
  Morgan Kell comes out of retirement.
- `id 0x68`: Brief Headlines / Fed Suns and Commonwealth attacks.
- `id 0x69`: Brief Headlines / The Fox still on the hunt.
- `id 0x6A`: Brief Headlines / Tikonov throws in the towel; Commonwealth
  launches a follow-up.

Candidate campaign/story/market regions change heavily during this hop,
especially around the existing candidate story/market blocks, but no confirmed
News Net read flags, mech state, current ammo, extra ammo, family attitudes,
sound, or detail settings changed.

### `SAV24.GAM -> SAV25.GAM`, 2026-07-29

Source files:

```text
C:/Users/MadCat/Desktop/saves/SAV24.GAM
C:/Users/MadCat/Desktop/saves/SAV25.GAM
```

Scenario:

- Started from `SAV24.GAM` on Keid, `3029-01-03`.
- Traveled to Sevren.
- Opened NEWS NET, viewed the visible message run, then saved.

Key travel/date result:

| Field | `SAV24` | `SAV25` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x63` | `0x2C` | Keid -> Sevren. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `103,119` | `89,60` | Sevren coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `5,120,000` | `4,975,000` | Travel cost `145,000`, matching one mech and `5` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | January 3029 day counter `4`, period `7` | February 3029 day counter `25`, period `4` | `80` travel-day counter units plus `1` News Net day. |

Keid -> Sevren travel math:

```text
dx = abs(103 - 89) = 14
dy = abs(119 - 60) = 59
scaled_dy = (59 * 32) / 20 = 94
distance = floor(sqrt(14*14 + 94*94)) = 95
charged_jumps_from_money = (145,000 - 20,000) / 25,000 = 5
travel_days = (5 - 1) * 14 + Keid.factor(7) * 2 + Sevren.factor(5) * 2 = 80
```

The date counters advance by `81` total units from `SAV24` to `SAV25`, which
matches the expected `80` travel units plus one News Net day. The friendly
display date wraps from January day counter `4` to February day counter `25`;
the raw counter/month bytes are the authoritative values here. The periodic
counter also matches: `7 + 81 = 88`, and `88 mod 14 = 4`.

News Net flag diff:

| Offset | `SAV24` | `SAV25` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x06DE` | `0x00` | `0x01` | Message id `0x6C` seen/read. |

The supplied screenshots show four visible pages. One creates a new read flag:

- `id 0x6C -> mw_main.headline.0203a4`: Brief Headlines / Northwind
  Highlanders change camp and join Davion; Theodore Kurita kicks Commonwealth
  off Vega; Buckminster defenders stall Steiner forces. The publication
  condition is `3029-01-15`, while the text title dates are `15 DEC 3028`,
  `20 DEC 3028`, and `31 DEC 3028`.

The other visible pages were already marked read:

- `id 0x69`: Brief Headlines / The Fox still on the hunt.
- `id 0x6A`: Brief Headlines / Tikonov throws in the towel; Commonwealth
  launches a follow-up.
- `id 0x6B`: Brief Headlines / Fox goes for the throat.

Candidate campaign/story/market regions change heavily during this hop, but
the confirmed News Net-specific change is only the `0x6C` message seen/read
flag. Mech state, current ammo, extra ammo, family attitudes, sound, and detail
settings are unchanged.

### `SAV25.GAM -> SAV26.GAM`, 2026-07-29

Source files:

```text
C:/Users/MadCat/Desktop/saves/SAV25.GAM
C:/Users/MadCat/Desktop/saves/SAV26.GAM
```

Scenario:

- Started from `SAV25.GAM` on Sevren, `3029-02-13`.
- Traveled to Andurien.
- Opened NEWS NET, viewed the visible message run, then saved.

Key travel/date result:

| Field | `SAV25` | `SAV26` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x2C` | `0x40` | Sevren -> Andurien. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `89,60` | `91,170` | Andurien coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `4,975,000` | `4,730,000` | Travel cost `245,000`, matching one mech and `9` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | February day counter `25`, period `4` | April day counter `50`, period `9` | `144` travel units plus `1` News Net day. |
| Raw age byte, `.GAM 0x0047` | `22` | `23` | Age-related byte refreshes after the displayed derived age had already reached `23` in earlier saves. |

Sevren -> Andurien travel math:

```text
dx = abs(89 - 91) = 2
dy = abs(60 - 170) = 110
scaled_dy = (110 * 32) / 20 = 176
distance = floor(sqrt(2*2 + 176*176)) = 176
jumps = 176 / 18 = 9
cost = 20,000 + 25,000 * 9 = 245,000
travel_days = (9 - 1) * 14 + Sevren.factor(5) * 2 + Andurien.factor(11) * 2 = 144
```

The date counters advance by `145` total units from `SAV25` to `SAV26`, which
matches the expected `144` travel units plus one News Net day. The periodic
counter also matches: `4 + 145 = 149`, and `149 mod 14 = 9`.

News Net flag diff:

| Offset | `SAV25` | `SAV26` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x06DF` | `0x00` | `0x01` | Message id `0x6D` seen/read. |

The supplied screenshots identify the newly flagged message:

- `id 0x6D -> mw_main.news.020515`: Valensia / Ander's Moon article
  `SENIOR COUNCIL MOVES TO CHOOSE NEW DUKE`. The publication condition is
  `3029-04-15`.

Other visible screenshots show already-read messages, including the Jordan
Rowe birthday personal message, `id 0x6B`, and `id 0x6C`. Candidate
campaign/story/market regions change heavily during this hop, but the
confirmed News Net-specific change is only the `0x6D` message seen/read flag.
Mech state, current ammo, extra ammo, family attitudes, sound, and detail
settings are unchanged.

### `SAV26.GAM -> SAV27.GAM`, 2026-07-29

Source files:

```text
C:/Users/MadCat/Desktop/saves/SAV26.GAM
C:/Users/MadCat/Desktop/saves/SAV27.GAM
```

Scenario:

- Started from `SAV26.GAM` on Andurien, `3029-04-26`.
- Traveled to New Delos.
- Opened NEWS NET, viewed the visible message run, then saved.

Key travel/date result:

| Field | `SAV26` | `SAV27` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x40` | `0x46` | Andurien -> New Delos. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `91,170` | `92,140` | New Delos coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `4,730,000` | `4,660,000` | Travel cost `70,000`, matching one mech and `2` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | April day counter `50`, period `9` | May day counter `39`, period `2` | `48` travel units plus `1` News Net day. |

Andurien -> New Delos travel math:

```text
dx = abs(91 - 92) = 1
dy = abs(170 - 140) = 30
scaled_dy = (30 * 32) / 20 = 48
distance = floor(sqrt(1*1 + 48*48)) = 48
jumps = 48 / 18 = 2
cost = 20,000 + 25,000 * 2 = 70,000
travel_days = (2 - 1) * 14 + Andurien.factor(11) * 2 + NewDelos.factor(6) * 2 = 48
```

The date counters advance by `49` total units from `SAV26` to `SAV27`, which
matches the expected `48` travel units plus one News Net day. The periodic
counter also matches: `9 + 49 = 58`, and `58 mod 14 = 2`.

News Net flag diff:

| Offset | `SAV26` | `SAV27` | Meaning |
| --- | ---: | ---: | --- |
| `.GAM 0x06E0` | `0x00` | `0x01` | Message id `0x6E` seen/read. |
| `.GAM 0x06E1` | `0x00` | `0x01` | Message id `0x6F` seen/read. |

The supplied screenshots identify the newly flagged messages:

- `id 0x6E -> mw_main.news.0206af`: Valensia / Ander's Moon article
  `MCBRIN SUCCESSFUL! CHALICE RETURNED!`. The publication condition is
  `3029-04-30`.
- `id 0x6F -> mw_main.news.020912`: Valensia / Ander's Moon article
  `JARRIS THEODORE MCBRIN IS NEW DUKE`. The publication condition is
  `3029-05-15`.

Other visible screenshots show already-read messages, including the Jordan
Rowe birthday personal message, `id 0x6C`, and `id 0x6D`. Candidate
campaign/story/market regions change heavily during this hop, but the
confirmed News Net-specific changes are only the `0x6E` and `0x6F` message
seen/read flags. Mech state, current ammo, extra ammo, family attitudes,
sound, and detail settings are unchanged.

### `SAV27.GAM -> SAV28.GAM`, 2026-07-29

Source files:

```text
C:/Users/MadCat/Desktop/saves/SAV27.GAM
C:/Users/MadCat/Desktop/saves/SAV28.GAM
```

Scenario:

- Started from `SAV27.GAM` on New Delos, `3029-05-20`.
- Traveled to Oliver.
- Opened NEWS NET, viewed the visible message run, then saved.

Key travel/date result:

| Field | `SAV27` | `SAV28` | Meaning |
| --- | ---: | ---: | --- |
| Current planet id, `.GAM 0x0021` | `0x46` | `0x4F` | New Delos -> Oliver. |
| Starmap X/Y, `.GAM 0x002B/0x002D` | `92,140` | `94,115` | Oliver coordinates match the planet table. |
| Wealth, `.GAM 0x0049..0x004C` | `4,660,000` | `4,590,000` | Travel cost `70,000`, matching one mech and `2` charged jumps. |
| Date counters, `.GAM 0x0031/0x0033/0x0037` | May day counter `39`, period `2` | June day counter `12`, period `7` | `32` travel units plus `1` News Net day. |

New Delos -> Oliver travel math:

```text
dx = abs(92 - 94) = 2
dy = abs(140 - 115) = 25
scaled_dy = (25 * 32) / 20 = 40
distance = floor(sqrt(2*2 + 40*40)) = 40
jumps = 40 / 18 = 2
cost = 20,000 + 25,000 * 2 = 70,000
travel_days = (2 - 1) * 14 + NewDelos.factor(6) * 2 + Oliver.factor(3) * 2 = 32
```

The date counters advance by `33` total units from `SAV27` to `SAV28`, which
matches the expected `32` travel units plus one News Net day. The periodic
counter also matches: `2 + 33 = 35`, and `35 mod 14 = 7`.

No bytes in the message seen/read flag area changed. The supplied screenshots
show only already-read messages, including the Jordan Rowe birthday personal
message, `id 0x6C`, `id 0x6D`, `id 0x6E`, and `id 0x6F`. This is a useful
negative control after the final observed Ander's Moon plot-news pair, but it
should be treated as "no new eligible NEWS NET messages at `3029-06-07` in
this route", not yet as proof that every possible story-gated message is
exhausted.

Candidate campaign/story/market regions change heavily during this hop, but
there is no confirmed News Net-specific flag change. Mech state, current ammo,
extra ammo, family attitudes, sound, and detail settings are unchanged.

### `SAV28.GAM -> SAV30.GAM`, 2026-07-29

Source files:

```text
C:/Users/MadCat/Desktop/saves/SAV28.GAM
C:/Users/MadCat/Desktop/saves/SAV30.GAM
```

Scenario:

- `SAV28.GAM`: Oliver, `3029-06-07`.
- `SAV30.GAM`: after additional Sphere travel, Okeefenokee, `3030-02-03`.
- The player reported that NEWS NET no longer showed any news during the
  additional travel.

Key result:

- No bytes in the message seen/read flag area changed from `SAV28` to
  `SAV30`.
- The full confirmed News Net flag set remains unchanged through
  `3030-02-03`: all previously observed ids through `0x6F`, plus the earlier
  known `0x70`, are still marked read and no later News Net publication flag is
  newly set.
- Byte `0x06E6` (`0x0672 + 0x74`) is already set in every checked save from
  `SAV0` (`3024-04-01`) through `SAV30` (`3030-02-03`). It did not change in
  this late control sequence and is not tied to an observed NEWS NET
  publication record.

This strengthens the current conclusion that the date-driven NEWS NET stream
has no newly eligible visible messages after the final observed Ander's Moon
plot-news pair (`0x6E`, `0x6F`) for this route. Keep the wording conservative:
this confirms no new visible/read NEWS NET flags through `3030-02-03`, not
that no other story-gated message can exist under different campaign state.

## Open questions

Save-side NEWS NET state is now mostly settled:

- Confirmed read/seen flags use `save_offset = 0x0672 + message_id` for all
  observed NEWS NET publication ids.
- Opening/reading NEWS NET advances the campaign counters by one day per visit,
  not by one day per article.
- No new visible/read NEWS NET flags appeared through `3030-02-03` after the
  final observed Ander's Moon plot-news pair, ids `0x6E` and `0x6F`.

Remaining save-file question:

- `0x06E6` (`0x0672 + 0x74`) is set from the initial checked save
  `SAV0.GAM`, so current evidence argues against it being a late campaign
  failure flag or the read flag for the final Ander's Moon news. Treat it as
  adjacent/unknown story, message, or initialized state until a controlled save
  shows it changing.

Remaining engine/UI questions, outside save editing:

- Fully name the navigation state variables around `DS:5E6A` and `DS:5E6C`.
- Determine exactly how the original chooses the visible start point that makes
  NEWS NET expose only the recent tail of the publication list. `SAV7` shows
  record-order display, but not a simple "show every eligible record" rule:
  already-read `0x55` remains visible while already-read `0x56` is skipped.
  This affects reproducing NEWS NET navigation in the rebuilt engine, not the
  save-file read-flag layout.
- Distinguish publication-list NEWS records from story-gated/personal message
  records that share nearby text storage but do not necessarily use this table.
