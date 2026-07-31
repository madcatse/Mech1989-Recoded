# MechWarrior `.GAM` Save Format

This document is the current working specification for original DOS
`MW_MAIN.EXE` save files. It should describe the latest known layout, not the
full history of every intermediate hypothesis.

## File Model

- Fixed size: `1816` bytes (`0x718`) in every observed sample.
- Endianness: little-endian for all confirmed multi-byte integers.
- Header, magic, checksum: none observed.
- Offsets are file-relative hexadecimal byte offsets.
- `confirmed` means controlled original-game saves, editor round trips, or
  executable/table analysis identify the field.
- `candidate` ranges are not blank space; they are preserved state blocks that
  still need subdivision by controlled saves.

## Restore-Menu Filename Behavior

The original restore screen does not scan saves by payload identity. It calls
DOS `FindFirst` / `FindNext` for `*.GAM`, copies at most twelve filenames into
its restore list, and shows fixed rows `1..12`.

Practical rule for testing:

- Overwrite an existing visible save slot when testing in the original game.
- `Export As` files are valid analysis copies, but a newly named copy may not
  appear if it falls outside the first twelve DOS directory entries.

## Primitive Encodings

### Damage State

Used by mech components and weapon condition bytes:

| Raw | UI text |
| ---: | --- |
| `0` | `FUNCTIONAL` |
| `1` | `LIGHT DAMAGE` |
| `2` | `HEAVY DAMAGE` |
| `3` | `JUNK` |

Overall mech `FUNCTIONAL` / `NONFUNCTIONAL` appears derived, not stored as a
separate confirmed byte. `JUNK` in `ENGINE`, `GYROS`, `SENSORS`,
`LIFE SUPPORT`, `LL ACTUATOR`, or `RL ACTUATOR` makes the mech
`NONFUNCTIONAL` in confirmed tests. `HEAVY DAMAGE` does not if no critical
component remains `JUNK`.

### Armor Damage

Each armor section stores a raw damage level:

| Editor quality | Raw damage |
| --- | ---: |
| `100%` | `0` |
| `66%` | `1` |
| `33%` | `2` |
| `0%` | `3` |

The observed overall armor percentage uses:

```text
floor((27 - sum(armor_damage_bytes)) * 100 / 27)
```

Single section tests confirm raw levels `1`, `2`, and `3` display as yellow,
red, and black respectively for visible front-art sections.

## Chassis IDs

Chassis ids follow the original string-table order:

| ID | Chassis | Editor note |
| ---: | --- | --- |
| `0` | `LOCUST` | Normal campaign chassis. |
| `1` | `WASP` | Hidden/internal. Original game can use it, but UI/art glitches. Editor shows `WASP (WILL GLITCH!)`. |
| `2` | `JENNER` | Normal campaign chassis. |
| `3` | `PHOENIX HAWK` | Normal campaign chassis. |
| `4` | `SHADOW HAWK` | Normal campaign chassis. |
| `5` | `WOLVERINE` | Hidden/internal. Original game can use it, but UI/art glitches. Editor shows `WOLVERINE (WILL GLITCH!)`. |
| `6` | `RIFLEMAN` | Normal campaign chassis. |
| `7` | `WARHAMMER` | Normal campaign chassis. |
| `8` | `MARAUDER` | Normal campaign chassis. |
| `9` | `BATTLEMASTER` | Normal campaign chassis. |
| `0xFFFF` | `EMPTY` | Empty owned-mech slot. |

The rebuilt engine currently treats Wasp and Wolverine as historical/internal
content, but the save editor exposes them for preservation and original-game
testing.

## Current Field Index

| Offset / range | Size | Type | Meaning |
| --- | ---: | --- | --- |
| `0x001D` | 1 | `uint8` | Raw reputation/rank value. |
| `0x0021` | 1 | `uint8` | Current planet index in the primary planet table. |
| `0x002B` | 1 | `uint8` | Current planet metadata copy: starmap X coordinate. |
| `0x002D` | 1 | `uint8` | Current planet metadata copy: starmap Y coordinate. |
| `0x0031` | 1 | `uint8` | Day counter within displayed month. |
| `0x0033` | 1 | `uint8` | Zero-based displayed campaign month, `0..11`. |
| `0x0035` | 2 | `uint16le` | Displayed campaign year. |
| `0x0037` | 1 | `uint8` | 14-day periodic campaign update counter, `0..13`. |
| `0x003B..0x0045` | 11 | ASCII, NUL padded | Current mech label/callsign. Read-only in editor for combat handoff safety. |
| `0x0047` | 1 | `uint8` | Raw start-age / age-related byte. It may lag behind derived displayed age until the original game later refreshes it. |
| `0x0049..0x004C` | 4 | `uint32le` | Wealth / C-bills. |
| `0x004D..0x0056` | 10 | five `int16le` | Family attitudes in UI order: Kurita, Steiner, Marik, Liao, Davion. |
| `0x00E8..0x00E9` | 2 | `uint16le` | Owned mech count, max `12`. |
| `0x00EA..0x0101` | 24 | twelve `uint16le` | Owned mech chassis list. Empty slot is `0xFFFF`. |
| `0x0102..0x025D` | 348 | twelve records | Owned mech state records, stride `0x1D`. |
| `0x025E..0x0275` | 24 | twelve `uint16le` | Owned mech ammo counts, one per mech slot. Meaning depends on chassis. |
| `0x02EE..0x02F9` | 12 | six `uint16le` | Extra ammo in hold: `AC 5-PKS`, `LRM 5-PKS`, `SRM 2-PKS`, `SRM 4-PKS`, `SRM 6-PKS`, `MACH GUN`. |
| `0x05E2` | 1 | `uint8` | Sound disabled flag: `0` on, `1` off. |
| `0x0672 + message_id` | variable | `uint8` flags | Message seen/read flags. Confirmed examples: id `0x01` at `0x0673`, id `0x02` at `0x0674`, id `0x03` at `0x0675`, id `0x04` at `0x0676`, id `0x05` at `0x0677`, id `0x10` at `0x0682`, id `0x11` at `0x0683`, id `0x12` at `0x0684`, id `0x13` at `0x0685`, id `0x14` at `0x0686`, id `0x15` at `0x0687`, id `0x16` at `0x0688`, id `0x1D` at `0x068F`, id `0x1E` at `0x0690`, id `0x1F` at `0x0691`, id `0x20` at `0x0692`, id `0x21` at `0x0693`, id `0x22` at `0x0694`, id `0x23` at `0x0695`, id `0x2A` at `0x069C`, id `0x2C` at `0x069E`, id `0x55` at `0x06C7`, id `0x56` at `0x06C8`, id `0x57` at `0x06C9`, id `0x58` at `0x06CA`, id `0x59` at `0x06CB`, id `0x5A` at `0x06CC`, id `0x5C` at `0x06CE`, id `0x5D` at `0x06CF`, id `0x5E` at `0x06D0`, id `0x5F` at `0x06D1`, id `0x60` at `0x06D2`, id `0x61` at `0x06D3`, id `0x62` at `0x06D4`, id `0x63` at `0x06D5`, id `0x65` at `0x06D7`, id `0x66` at `0x06D8`, id `0x67` at `0x06D9`, id `0x68` at `0x06DA`, id `0x69` at `0x06DB`, id `0x6A` at `0x06DC`, id `0x6B` at `0x06DD`, id `0x6C` at `0x06DE`, id `0x6D` at `0x06DF`, id `0x6E` at `0x06E0`, id `0x6F` at `0x06E1`, id `0x70` at `0x06E2`. |
| `0x070D` | 1 | `uint8` | Graphics detail level: `0` low, `1` medium, `2` high. |

### Planet IDs

These values are save ids from the primary planet table. The save also stores a
copy of the current planet's starmap coordinates at `0x002B` and `0x002D`. The
editor displays these ids by planet name.

| Planet id | Planet | Starmap X | Starmap Y |
| ---: | --- | ---: | ---: |
| `0x00` | `LUTHIEN` | `133` | `78` |
| `0x01` | `PESHT` | `141` | `68` |
| `0x02` | `QANDAHAR` | `162` | `59` |
| `0x03` | `NEW SAMARKAND` | `172` | `76` |
| `0x04` | `TABAYAMA` | `179` | `79` |
| `0x05` | `GALEDON V` | `166` | `87` |
| `0x06` | `KAZNEJOV` | `166` | `94` |
| `0x07` | `THESTRIA` | `159` | `105` |
| `0x08` | `MISERY` | `150` | `105` |
| `0x09` | `MATSUIDA` | `154` | `96` |
| `0x0A` | `OSHIKA` | `145` | `90` |
| `0x0B` | `IRURZUN` | `134` | `102` |
| `0x0C` | `PROSERPINA` | `131` | `112` |
| `0x0D` | `BENJAMIN` | `125` | `96` |
| `0x0E` | `DIERON` | `108` | `111` |
| `0x0F` | `KESSEL` | `102` | `103` |
| `0x10` | `BUCKMINSTER` | `105` | `91` |
| `0x11` | `KARBALA` | `94` | `88` |
| `0x12` | `RUBIGEN` | `107` | `83` |
| `0x13` | `GALUZZO` | `98` | `77` |
| `0x14` | `ALSHAIN` | `113` | `72` |
| `0x15` | `RADSTADT` | `108` | `67` |
| `0x16` | `KIRCHBACH` | `94` | `56` |
| `0x17` | `RASALHAGUE` | `109` | `55` |
| `0x18` | `ALBIERO` | `128` | `58` |
| `0x19` | `VEGA` | `105` | `101` |
| `0x1A` | `SKOKIE` | `93` | `64` |
| `0x1B` | `XINYANG` | `123` | `81` |
| `0x1C` | `DELACRUZ` | `171` | `98` |
| `0x1D` | `LAND'S END` | `176` | `64` |
| `0x1E` | `THARKAD` | `53` | `96` |
| `0x1F` | `DONEGAL` | `61` | `93` |
| `0x20` | `ALARION` | `33` | `99` |
| `0x21` | `COVENTRY` | `41` | `86` |
| `0x22` | `POULSBO` | `26` | `127` |
| `0x23` | `TIMBUKTU` | `13` | `95` |
| `0x24` | `BOUNTIFUL HARVEST` | `71` | `77` |
| `0x25` | `CHUCKCHI III` | `64` | `104` |
| `0x26` | `SKYE` | `90` | `104` |
| `0x27` | `ALEXANDRIA` | `93` | `98` |
| `0x28` | `RAHNE` | `77` | `113` |
| `0x29` | `HESPERUS II` | `78` | `107` |
| `0x2A` | `PORT MOSEBY` | `99` | `93` |
| `0x2B` | `MIZAR` | `92` | `109` |
| `0x2C` | `SEVREN` | `89` | `60` |
| `0x2D` | `CARSE` | `93` | `80` |
| `0x2E` | `KOBE` | `98` | `70` |
| `0x2F` | `DUSTBALL` | `81` | `75` |
| `0x30` | `SUK II` | `94` | `73` |
| `0x31` | `WINFIELD` | `79` | `54` |
| `0x32` | `ANYWHERE` | `69` | `49` |
| `0x33` | `TAMAR` | `92` | `61` |
| `0x34` | `ANEMBO` | `38` | `71` |
| `0x35` | `TIMBIQUI` | `34` | `128` |
| `0x36` | `DIXIE` | `48` | `121` |
| `0x37` | `ARCADIA` | `62` | `117` |
| `0x38` | `VALLOIRE` | `23` | `112` |
| `0x39` | `THORIN` | `99` | `110` |
| `0x3A` | `GARRISON` | `82` | `89` |
| `0x3B` | `NIANGOL` | `23` | `84` |
| `0x3C` | `ATREUS` | `62` | `146` |
| `0x3D` | `ANGELL II` | `79` | `132` |
| `0x3E` | `MARIK` | `84` | `130` |
| `0x3F` | `SILVER` | `37` | `145` |
| `0x40` | `ANDURIEN` | `91` | `170` |
| `0x41` | `ALULA AUSTRALIS` | `97` | `113` |
| `0x42` | `GIBSON` | `55` | `157` |
| `0x43` | `MOSIRO` | `86` | `164` |
| `0x44` | `CALLOWAY VI` | `88` | `153` |
| `0x45` | `ORIENTE` | `85` | `156` |
| `0x46` | `NEW DELOS` | `92` | `140` |
| `0x47` | `REGULUS` | `73` | `150` |
| `0x48` | `LESNOVO` | `51` | `167` |
| `0x49` | `AMITY` | `80` | `123` |
| `0x4A` | `SHILOH` | `85` | `116` |
| `0x4B` | `PROCYON` | `100` | `116` |
| `0x4C` | `TAMARIND` | `49` | `135` |
| `0x4D` | `CLAYBROOKE` | `89` | `179` |
| `0x4E` | `IRIAN` | `91` | `126` |
| `0x4F` | `OLIVER` | `94` | `115` |
| `0x50` | `SUZANO` | `101` | `137` |
| `0x51` | `GOODNA` | `96` | `161` |
| `0x52` | `SADURNI` | `97` | `174` |
| `0x53` | `CIREBON` | `75` | `164` |
| `0x54` | `NESTOR` | `73` | `121` |
| `0x55` | `GRIFFITH` | `38` | `135` |
| `0x56` | `AUTUMN WIND` | `68` | `133` |
| `0x57` | `CHALOUBA` | `48` | `145` |
| `0x58` | `SOPHIE'S WORLD` | `78` | `139` |
| `0x59` | `ZORTMAN` | `58` | `130` |
| `0x5A` | `SIAN` | `105` | `156` |
| `0x5B` | `BETELGEUSE` | `101` | `165` |
| `0x5C` | `BUENOS AIRES` | `103` | `172` |
| `0x5D` | `GRAND BASE` | `107` | `164` |
| `0x5E` | `MENKE` | `115` | `170` |
| `0x5F` | `TURIN` | `102` | `178` |
| `0x60` | `TIKONOV` | `119` | `124` |
| `0x61` | `ALDEBARAN` | `104` | `127` |
| `0x62` | `BHARAT` | `112` | `122` |
| `0x63` | `KEID` | `103` | `119` |
| `0x64` | `NANKING` | `106` | `125` |
| `0x65` | `NEW HESSEN` | `114` | `126` |
| `0x66` | `TALL TREES` | `100` | `128` |
| `0x67` | `CAPELLA` | `111` | `149` |
| `0x68` | `ARES` | `117` | `147` |
| `0x69` | `BITHINIA` | `95` | `149` |
| `0x6A` | `EXEDOR` | `98` | `154` |
| `0x6B` | `NECROMO` | `118` | `150` |
| `0x6C` | `RABALLA` | `105` | `146` |
| `0x6D` | `STYK` | `107` | `135` |
| `0x6E` | `TSINGHAI` | `99` | `144` |
| `0x6F` | `WARLOCK` | `122` | `159` |
| `0x70` | `MATSU` | `116` | `138` |
| `0x71` | `ZANZIBAR` | `123` | `177` |
| `0x72` | `MILOS` | `116` | `155` |
| `0x73` | `NEW AVALON` | `152` | `135` |
| `0x74` | `NEW SYRTIS` | `143` | `163` |
| `0x75` | `ROBINSON` | `146` | `123` |
| `0x76` | `KESAI IV` | `171` | `103` |
| `0x77` | `GALAX` | `158` | `139` |
| `0x78` | `MALLORY'S WORLD` | `123` | `116` |
| `0x79` | `KATHIL` | `131` | `141` |
| `0x7A` | `REDFIELD` | `129` | `151` |
| `0x7B` | `OKEFENOKEE` | `215` | `125` |
| `0x7C` | `GAMBLER` | `176` | `126` |
| `0x7D` | `MARDUK` | `140` | `111` |
| `0x7E` | `ANDER'S MOON` | `154` | `108` |
| `0x7F` | `HYALITE` | `150` | `174` |
| `0x80` | `TANCREDI IV` | `182` | `103` |
| `0x81` | `HOBBS` | `152` | `154` |
| `0x82` | `KITTERY` | `122` | `155` |
| `0x83` | `GREAT GORGE` | `193` | `159` |
| `0x84` | `CAPH` | `108` | `116` |
| `0x85` | `HOFF` | `153` | `114` |
| `0x86` | `BAXLEY` | `184` | `140` |
| `0x87` | `GREELY` | `189` | `112` |
| `0x88` | `COGDELL` | `200` | `137` |
| `0x89` | `FRAZER` | `126` | `171` |
| `0x8A` | `MORAVIAN` | `122` | `139` |
| `0x8B` | `NEW ARAGON` | `115` | `130` |
| `0x8C` | `NOATAK` | `168` | `153` |
| `0x8D` | `BEECHER` | `138` | `130` |
| `0x8E` | `XHOSA VII` | `135` | `117` |
| `0x8F` | `IMMENSTADT` | `117` | `163` |
| `0x90` | `DELACAMBRE` | `205` | `114` |

## Owned Mech Structure

The player company has twelve mech slots:

```text
slot_index     = 0..11
chassis_offset = 0x00EA + slot_index * 2
record_base    = 0x0102 + slot_index * 0x1D
ammo_offset    = 0x025E + slot_index * 2
```

`MECH1.GAM` and `MECH2.GAM` confirm this layout. `MECH1` has one Locust.
`MECH2` changes the owned count to `2`, changes slot 2 chassis from `0xFFFF`
to `3` (`PHOENIX HAWK`), and adds Phoenix Hawk state at slot 2 record base
`0x011F`.

### Per-Slot Record Layout

| Relative offset | Size | Meaning |
| ---: | ---: | --- |
| `+0x00` | 1 | Engine condition. |
| `+0x01` | 1 | Gyros condition. |
| `+0x02` | 1 | Sensors condition. |
| `+0x03` | 1 | Life support condition. |
| `+0x04` | 1 | Missing/damaged heat-sink count. Working sinks are `chassis_total - value`. |
| `+0x05` | 1 | LA actuator condition. |
| `+0x06` | 1 | RA actuator condition. |
| `+0x07` | 1 | LL actuator condition. |
| `+0x08` | 1 | RL actuator condition. |
| `+0x09` | 1 | Missing/damaged jump-jet count. Working jets are `chassis_total - value`. |
| `+0x0A..+0x13` | 10 | Weapon condition bytes in chassis weapon order. Unused weapon slots are normally zero. |
| `+0x14..+0x1C` | 9 | Armor damage bytes in the order below. |

### Armor Byte Order

Armor byte order inside every mech record:

| Relative offset | Editor key | Meaning |
| ---: | --- | --- |
| `+0x14` | `RA` | Right arm. |
| `+0x15` | `LA` | Left arm. |
| `+0x16` | `RL` | Right leg. |
| `+0x17` | `LL` | Left leg. |
| `+0x18` | `HEAD` | Head / cockpit-like top section. |
| `+0x19` | `CT` | Center torso. |
| `+0x1A` | `BACK` | Back armor hypothesis: affects armor percent, no observed front overlay. |
| `+0x1B` | `TR` | Right torso. |
| `+0x1C` | `TL` | Left torso. |

Left/right labels are mech-relative. In the original front-art view, the mech's
left side appears on the viewer's right.

### Chassis Weapon Order

Weapon condition bytes use the weapon order from original UI string tables and
current engine definitions:

| Chassis | Weapon condition slots |
| --- | --- |
| `LOCUST` | `M LAS CT`, `MG RA`, `MG LA` |
| `WASP (WILL GLITCH!)` | `M LAS RA`, `SRM2 LT` |
| `JENNER` | `SRM4 CT`, `M LAS RA`, `M LAS RA`, `M LAS LA`, `M LAS LA` |
| `PHOENIX HAWK` | `L LAS RA`, `M LAS RA`, `M LAS LA`, `MG LA`, `MG RA` |
| `SHADOW HAWK` | `AC/5 LT`, `LRM5 RT`, `SRM2 HD`, `M LAS RA` |
| `WOLVERINE (WILL GLITCH!)` | `AC/5 RA`, `SRM6 LT`, `M LAS HD` |
| `RIFLEMAN` | `L LAS RA`, `L LAS LA`, `AC/5 RA`, `AC/5 LA`, `M LAS RT`, `M LAS LT` |
| `WARHAMMER` | `PPC RA`, `PPC LA`, `SRM6 RT`, `M LAS RT`, `M LAS LT`, `S LAS RT`, `S LAS LT`, `MG RT`, `MG LT` |
| `MARAUDER` | `PPC RA`, `PPC LA`, `M LAS RA`, `M LAS LA`, `AC/5 RT` |
| `BATTLEMASTER` | `PPC RA`, `M LAS RT`, `M LAS RT`, `M LAS RT`, `MG LA`, `MG LA`, `SRM6 LT`, `M LAS LT`, `M LAS LT`, `M LAS LT` |

### Chassis Heat Sinks And Jump Jets

The save stores missing/damaged counts. The editor displays working counts:

| Chassis | Heat sinks | Jump jets |
| --- | ---: | ---: |
| `LOCUST` | 10 | 0 |
| `WASP` | 10 | 6 |
| `JENNER` | 10 | 3 |
| `PHOENIX HAWK` | 10 | 6 |
| `SHADOW HAWK` | 12 | 3 |
| `WOLVERINE` | 12 | 5 |
| `RIFLEMAN` | 10 | 0 |
| `WARHAMMER` | 18 | 0 |
| `MARAUDER` | 16 | 0 |
| `BATTLEMASTER` | 18 | 0 |

Jenner heat sinks and jump jets are confirmed by controlled saves/editor
round trips. Locust jump jets are confirmed as `0`; other chassis totals come
from original table/current engine definitions and should be rechecked when
controlled saves for those chassis are available.

Editor note: when changing a mech slot's chassis, `tools/gam_editor.py` resets
the missing heat-sink and missing jump-jet bytes to `0`, so the newly selected
chassis starts with its full chassis-total counts instead of inheriting stale
damage counts from the previous chassis.

### Current Ammo Count

`0x025E..0x0275` stores one `uint16le` ammo count per mech slot. The current
meaning is chassis-dependent:

| Chassis | Editor label | Confirmed max |
| --- | --- | ---: |
| `LOCUST` | `MG ammo` | 200 |
| `JENNER` | `SRM4 ammo` | 25 |
| `PHOENIX HAWK` | `MG ammo` | 200 |

Other chassis expose an editable current-ammo count with a conservative editor
cap of `255` until controlled original-game reload tests identify exact maxima.

## Extra Ammo In Hold

The hold inventory is a six-counter array:

| Offset | Type | Meaning |
| --- | --- | --- |
| `0x02EE` | `uint16le` | `AC 5-PKS` extra ammo in hold. |
| `0x02F0` | `uint16le` | `LRM 5-PKS` extra ammo in hold. |
| `0x02F2` | `uint16le` | `SRM 2-PKS` extra ammo in hold. |
| `0x02F4` | `uint16le` | `SRM 4-PKS` extra ammo in hold. |
| `0x02F6` | `uint16le` | `SRM 6-PKS` extra ammo in hold. |
| `0x02F8` | `uint16le` | `MACH GUN` extra ammo in hold. |

`DATA4.GAM -> DATA5.GAM` confirms the array by buying:
`5,10,15,20,25,50`.

## System Settings

Controlled `SAVE1.GAM..SAVE4.GAM` files confirm:

| Offset | Type | Values |
| --- | --- | --- |
| `0x05E2` | `uint8` | `0` = sound on, `1` = sound off. |
| `0x070D` | `uint8` | `0` = low detail, `1` = medium detail, `2` = high detail. |

## Full Address Coverage Map

This map covers the whole `0x0000..0x0717` save block. Candidate ranges must be
preserved exactly until controlled saves split them into named fields.

| Range | Current classification |
| --- | --- |
| `0x0000..0x001C` | Candidate global flags/counters. |
| `0x001D` | Confirmed raw reputation/rank byte. |
| `0x001E..0x001F` | Candidate global state near reputation. |
| `0x0020..0x003A` | World/date cluster. Confirmed fields inside: `0x0021`, `0x002B`, `0x002D`, `0x0031`, `0x0033`, `0x0035..0x0036`, `0x0037`. |
| `0x003B..0x0045` | Confirmed current mech label/callsign. |
| `0x0046` | Candidate identity/state byte. |
| `0x0047` | Confirmed raw age/start-age-related byte. |
| `0x0048` | Candidate identity/state byte. |
| `0x0049..0x004C` | Confirmed wealth / C-bills. |
| `0x004D..0x0056` | Confirmed five family attitude scores. |
| `0x0057..0x00E7` | Candidate campaign flags, mission lists, markets, and short counters. |
| `0x00E8..0x00E9` | Confirmed owned mech count. |
| `0x00EA..0x0101` | Confirmed twelve-slot chassis list. |
| `0x0102..0x025D` | Confirmed twelve-slot mech state table. |
| `0x025E..0x0275` | Confirmed twelve-slot current ammo table. |
| `0x0276..0x02ED` | Candidate event, story, market, and inventory/ammo flags. |
| `0x02EE..0x02F9` | Confirmed extra-ammo-in-hold array. |
| `0x02FA..0x033F` | Candidate event, story, market, and inventory/ammo flags. |
| `0x0340..0x04FF` | Candidate visited-world, NEWS/story, availability, and enum matrix. |
| `0x0500..0x05B8` | Candidate roster, pilot, mission, and combat-transfer block. |
| `0x05B9..0x05DE` | Static lookup table copied into saves; identical in existing corpus. |
| `0x05DF..0x05E1` | Candidate mixed campaign/system state. |
| `0x05E2` | Confirmed sound disabled flag. |
| `0x05E3..0x064B` | Candidate mixed campaign/system block. |
| `0x064C..0x0671` | Candidate sparse campaign flags and story state. |
| `0x0672..0x06FD` | Message seen/read flag area. Confirmed formula for known messages: flag byte = `0x0672 + message_id`; examples `0x0673`, `0x0674`, `0x0675`, `0x0676`, `0x0677`, `0x0682`, `0x0683`, `0x0684`, `0x0685`, `0x0686`, `0x0687`, `0x0688`, `0x068F`, `0x0690`, `0x0691`, `0x0692`, `0x0693`, `0x0694`, `0x0695`, `0x069C`, `0x069E`, `0x06C7`, `0x06C8`, `0x06C9`, `0x06CA`, `0x06CB`, `0x06CC`, `0x06CE`, `0x06CF`, `0x06D0`, `0x06D1`, `0x06D2`, `0x06D3`, `0x06D4`, `0x06D5`, `0x06D7`, `0x06D8`, `0x06D9`, `0x06DA`, `0x06DB`, `0x06DC`, `0x06DD`, `0x06DE`, `0x06DF`, `0x06E0`, `0x06E1`, `0x06E2`. Other bytes in this range may include message flags for later ids and adjacent story state. |
| `0x06FE..0x070C` | Candidate final location/story/system cluster. |
| `0x070D` | Confirmed graphics detail level. |
| `0x070E..0x0717` | Candidate trailing location/story/system cluster. |

## Editor Policy

`tools/gam_editor.py` may edit only fields listed above as confirmed or
structurally confirmed:

- money;
- friendly date fields that derive confirmed date bytes;
- family attitudes;
- all twelve mech chassis ids, records, armor values, weapon conditions,
  heat-sink/jump-jet counts, and current ammo counts;
- extra ammo in hold;
- sound and detail settings.

The editor keeps the mech label/callsign read-only because combat handoff
safety is not fully confirmed.

Wasp and Wolverine are deliberately visible in the editor with `WILL GLITCH!`
warnings. The original game can load and use them, but it lacks complete
status art/interface support for them.
