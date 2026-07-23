# Starmap and planet data integration

Goal: integrate the campaign starmap and planet selection UI while reading the
canonical planet data from `Original/MW_MAIN.EXE`.

Original files in `Original/` must stay read-only. Do not copy the planet table
into C++ source as hard-coded data. Load or extract the data from the original
EXE using the format below.

## Source of truth

Use `Original/MW_MAIN.EXE`.

The planet records are referenced by a pointer table at file offset `0x00F270`.
Each pointer is a 16-bit little-endian value. Convert a pointer to a file offset
with:

```text
record_file_offset = 0x008D00 + pointer_value
```

Example:

```text
pointer 0x67B6 -> file offset 0x00F4B6 -> LUTHIEN
```

The reproducible reference extractor is:

```text
tools/planet_table_extractor.py
```

The generated CSV/JSON are review artifacts, not the canonical runtime source:

```text
research/analysis/mw_main_planet_table.csv
research/analysis/mw_main_planet_table.json
```

## Record format

Each planet record begins 10 bytes before the planet name:

```text
byte 0      planet number
byte 1      house id: 0 Kurita, 1 Steiner, 2 Marik, 3 Liao, 4 Davion
byte 2      terrain code, 1..12
byte 3      unknown_byte_3, preserve as raw uint8
byte 4      contract availability flag: 0 no House icon/contracts, 1 contracts available
byte 5      starmap/screen x
byte 6      starmap/screen y
byte 7      unknown_byte_7, preserve as raw uint8
bytes 8..9  population in millions, uint16 little-endian
bytes 10..  planet name, NUL-terminated ASCII
then        planet description, NUL-terminated ASCII
```

Keep `unknown_byte_3` and `unknown_byte_7` in the model exactly as read from the
EXE. Do not infer behavior from them until separately confirmed.

Economy follow-up: `docs/MECH_ECONOMY_RESEARCH.md` records current BattleMech
buy/sell findings and checks the hypothesis that `unknown_byte_3` might be the
planet economy tier. Current evidence argues against a direct mapping: several
known or expected tier-1 and tier-4 worlds share the same `unknown_byte_3`
values. Keep the byte as raw data until the original market-generation routine
is traced.

Population:

```text
population = uint16le(bytes 8..9) * 1,000,000
```

Environment/background:

```text
terrain_code % 3 == 1 -> Desert
terrain_code % 3 == 2 -> Tropical
terrain_code % 3 == 0 -> Ice
```

Use the environment value to select the main location and bar background:

```text
Desert   -> airless/desert background
Tropical -> tropical background
Ice      -> ice background
```

## Pointer ranges and duplicate handling

The first `146` pointers at `0x00F270` are the primary planet pointer block.
They contain `145` unique planets because `ANDER'S MOON` appears twice.

When loading the primary block:

1. Parse all 146 records.
2. Deduplicate by planet name.
3. Keep the first `ANDER'S MOON` record.
4. Expect exactly 145 unique planets.

The duplicate `ANDER'S MOON` should not appear in player-facing planet lists.

Expected house counts:

```text
Kurita  30
Steiner 30
Marik   30
Liao    25
Davion  30
Total  145
```

## Ordering

There are two useful orders.

Primary order:

```text
pointer indexes 0..145
```

This order is grouped by house and is useful for preserving original numeric
planet ids and save-game indexing.

Alphabetical order:

```text
pointer indexes 146..290
```

This order recovers 145 planets. Use it for the House planet list UI shown in
the original game: filter by house and display the planets in this recovered
alphabetical order.

This matches the captured House planet screenshots:

```text
screenshots/image0135.png  House Kurita
screenshots/image0136.png  House Steiner
screenshots/image0137.png  House Marik
screenshots/image0138.png  House Liao
screenshots/image0139.png  House Davion
```

## Runtime model

Recommended runtime structure:

```cpp
struct PlanetRecord {
    uint16_t tableOrder;              // 1-based primary unique order
    uint16_t alphaOrder;              // 1-based recovered alphabetical order
    uint8_t planetNumber;             // raw byte 0
    uint8_t houseId;                  // raw byte 1
    uint8_t terrainCode;              // raw byte 2
    uint8_t unknownByte3;             // raw byte 3, preserve
    uint8_t contractAvailableFlag;    // raw byte 4
    uint8_t mapX;                     // raw byte 5
    uint8_t mapY;                     // raw byte 6
    uint8_t unknownByte7;             // raw byte 7, preserve
    uint64_t population;
    std::string name;
    std::string description;
};
```

For compatibility, keep the raw bytes available even when adding friendly
derived fields such as house name, environment name, and formatted population.

## Current planet and save relation

Existing save analysis indicates that `.GAM` offset `0x0021` stores the current
planet as a zero-based planet index. For example:

```text
0x00 -> LUTHIEN
0x76 -> KESAI IV
```

When entering a location screen, resolve the current planet from this index,
then use the planet record for:

```text
name
environment/background
population display
description
house id / house icon
contract availability
map coordinates
```

Known copied current-planet metadata in saves includes house id, contract flag,
and map coordinates. Treat the EXE record as canonical and use save fields only
as current state evidence until the save format is fully named.

## UI behavior to preserve

Planet list window:

```text
1. Player selects a House.
2. Display only planets owned by that House.
3. Use recovered alphabetical order.
4. Highlight the currently selected row in yellow.
5. Do not show the duplicate ANDER'S MOON.
```

Main location screen:

```text
1. Show current planet name.
2. Show environment label: DESERT, TROPICAL, or ICE.
3. Show population as POP:x,xxx,xxx,xxx.
4. Use terrain-derived background.
5. If contractAvailableFlag == 0, hide the House contract icon.
6. If contractAvailableFlag == 1, show the House contract icon and allow contract flow.
```

Starmap plotting:

```text
Use byte 5 as x and byte 6 as y.
Keep byte 7 in the planet model as a raw value; it is also used by travel time.
```

## Travel jumps and cost

The original travel-cost routine in `Original/MW_MAIN.EXE` uses the planet
`map_x` and `map_y` bytes to derive jump count.

For two different planets:

```text
dx = abs(destination.map_x - current.map_x)
dy = abs(destination.map_y - current.map_y)
scaled_dy = (dy * 32) / 20       integer division
distance = floor(sqrt(dx*dx + scaled_dy*scaled_dy))
jumps = max(1, distance / 18)    integer division
```

For the same planet in the recomp UI, treat travel as `0` jumps and do not
start the travel animation.

Known validation:

```text
OKEFENOKEE -> TIMBUKTU: dx 202, dy 30, scaled_dy 48, distance 207, jumps 11
OKEFENOKEE -> NIANGOL:  dx 192, dy 41, scaled_dy 65, distance 202, jumps 11
```

Cost:

```text
if mech_count > 0:
    cost = mech_count * (20,000 + 25,000 * jumps)
else:
    cost = min(pilot_count, 4) * 2,500 * jumps
```

The player can have up to `4` pilots including the player and up to `12`
owned 'Mechs. When at least one 'Mech is transported, pilot cost is ignored.

UI behavior:

```text
Show travel cost on the starmap, but keep jump count internal.
After pressing TRAVEL, keep the starmap visible for about 1 second and draw a
white straight route line from the current planet to the selected planet before
switching to the shuttle animation.
```

## Travel time and date advancement

The original date-advance routine is at `Original/MW_MAIN.EXE` file offset
`0x0011EE`. It takes `AX` as a day count and updates the campaign date fields.

Save/memory relation for the relevant fields:

```text
save 0x0031 -> DS:0479  day counter within displayed month, 0..59
save 0x0033 -> DS:047B  displayed month, zero-based, 0..11
save 0x0035 -> DS:047D  displayed year, uint16 little-endian
save 0x0037 -> DS:047F  14-day periodic update counter, 0..13
```

New-game initialization near file offset `0x006E38` sets:

```text
DS:0479 = 1       day counter within April
DS:047B = 3       APRIL, zero-based
DS:047D = 3024
DS:047F = 0       14-day periodic update counter
DS:048F = 18      raw starting age
```

The embedded new-game/default state around file offset `0x009148` contains the
same date values at save-relative offsets `0x0031`, `0x0033`, `0x0035`, and
`0x0037`.

The original date model uses fixed `60`-day displayed months and `12` displayed
months per year:

```text
month_day_counter += days
if month_day_counter >= 60:
    month += month_day_counter / 60
    month_day_counter %= 60

if month >= 12:
    year += month / 12
    month %= 12

periodic_14_day_counter += days
if periodic_14_day_counter >= 14:
    periodic_14_day_counter %= 14
    run periodic campaign updates
```

The travel completion routine near file offset `0x006488` advances time by:

```text
travel_days =
    (jumps - 1) * 14
    + current_planet.unknown_byte_7 * 2
    + destination_planet.unknown_byte_7 * 2
```

This confirms that planet record byte 7 is used by travel timing. Keep it as a
raw field in the model, but it can now also have a derived/friendly meaning such
as `travelTimeFactor`.

Examples:

```text
LUTHIEN -> BENJAMIN:      jumps 1, byte7 8 + 3,  travel_days 22
OKEFENOKEE -> TIMBUKTU:   jumps 11, byte7 7 + 4, travel_days 162
OKEFENOKEE -> NIANGOL:    jumps 11, byte7 7 + 18, travel_days 190
```

Confirmed save checks:

```text
New game start:
  APRIL 3024, month_day_counter 1, periodic_14_day_counter 0

IRURZUN -> POULSBO:
  jumps 6, travel_days 106
  save POULSBO.GAM: MAY 3024, month_day_counter 47, periodic_14_day_counter 8

POULSBO -> OKEFENOKEE:
  jumps 10, travel_days 162
  save OKEFENO.GAM: AUGUST 3024, month_day_counter 29, periodic_14_day_counter 2

IRURZUN -> CLAYBROOKE -> RASALHAGUE -> ZANZIBAR:
  cumulative travel_days 558
  save ZANZIZ.GAM: JANUARY 3025, month_day_counter 19, periodic_14_day_counter 12
```

## Validation checklist

Before considering the integration complete:

```text
Loader parses exactly 145 unique planets.
House counts are 30, 30, 30, 25, 30.
The duplicate ANDER'S MOON is skipped in UI lists.
The five House planet lists match screenshots image0135..image0139.
Environment counts match the extractor output: Tropical 79, Desert 48, Ice 18.
Population formatting matches the original UI style.
Planets with contractAvailableFlag == 0 do not show the House contract icon.
unknownByte3 and unknownByte7 remain stored and exported for later research.
```

## Do not do yet

Do not use `unknown_byte_3` for gameplay behavior until a separate task
confirms its meaning against the original game. It is not a confirmed direct
economy-tier field; see `docs/MECH_ECONOMY_RESEARCH.md`. `unknown_byte_7` is
now confirmed for travel timing, but keep the raw byte stored/exported.

Do not replace the canonical EXE reader with the generated JSON unless the user
explicitly asks for a packaged data export path.
