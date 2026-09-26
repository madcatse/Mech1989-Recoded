# Mission Position Distribution Notes

Status: Phase 9 reverse-engineering notes for the battle start map, cockpit map,
and future campaign-to-battle placement bridge. Do not treat the names below as
final public API names until the MW_MAIN-to-BTECH handoff writer is fully mapped.

## Current conclusion

The original game does not appear to pass final world coordinates for the
player, enemies, and objective directly from `MW_MAIN.EXE` to `BTECH.EXE`.
Instead, the main game chooses a contract/mission record and a small battle
context. `BTECH.EXE` then expands that context through `SNARIO.DAT` and its own
mission logic tables into runtime object records.

The placement path has three layers:

1. `MW_MAIN.EXE` contract generation chooses mission metadata and a candidate
   terrain/scenario record.
2. The battle context passed to `BTECH.EXE` contains compact selectors:
   environment, seed, scenario/mission selector, graphics detail, and lance slot
   ids.
3. `BTECH.EXE` reads `SNARIO.DAT[seed]`, chooses position banks for the player
   side and enemy/objective side, copies 32-byte coordinate blocks, creates the
   player objects first, creates up to four hostile/objective objects, and then
   computes initial facing.

This means our C++ bridge should reproduce selectors first. We should not bake
current screenshot-matched map blip coordinates as final mission placement.

## MW_MAIN contract-side selectors

The current contract generator candidate remains `MW_MAIN.EXE`
`FUN_101b_4f6e`.

Known writes for each offered contract slot:

| Field | Meaning candidate |
|---|---|
| `DS:0962 + slot*2` | mission/target category |
| `DS:096E + slot*2` | mission class bucket, usually `1..4` |
| `DS:094A + slot*2` | mission title/data pointer |
| `DS:0A2B + slot*2` | one-based mission handoff id copied directly to BTECH `context[7]` |
| `DS:0956 + slot*2` | scenario/terrain/target candidate |
| `DS:097A/0986/0992 + slot*2` | heavy/medium/light enemy count estimates |

`MW_MAIN.EXE` also extracts bytes from the selected mission table entry in
`FUN_101b_1ace`:

```text
terrain_band        = (entry[2] - 1) / 3
btech_environment   = (entry[2] - 1) % 3
entry[1]            -> DS:046D
entry[4]            -> DS:0471
entry[5]            -> DS:0473
entry[6]            -> DS:0475
```

The direct handoff writer is now proven from `Original/MW_MAIN.EXE` file offsets
`0x005B0D..0x005B48`. With `SI = accepted_offer_slot * 2`, it writes:

```text
DS:097A[SI] -> DS:044B -> BTECH context[3]  estimated heavy count
DS:0986[SI] -> DS:044C -> BTECH context[4]  estimated medium count
DS:0992[SI] -> DS:044D -> BTECH context[5]  estimated light count
DS:0956[SI] + 1 -> DS:044E -> BTECH context[6] seed source
DS:0A2B[SI] -> DS:044F -> BTECH context[7] mission selector
```

`BTECH FUN_1000_2c34` then computes `c96e = context[7] - 1`. For ordinary
contracts this is the zero-based placement selector used by the C++ mission
catalog. The three extended briefing ids `14`, `15`, and `17` are different:
they enter the original three-stage campaign sequence and replace `c96e` from
`DS:08F6` before the placement tables are read. The eight recovered selector
routes are:

```text
bank 0: 25,5,19    bank 4: 29,29,7
bank 1: 26,18,34   bank 5: 7,7,25
bank 2: 31,6,16    bank 6: 24,13,21
bank 3: 7,29,29    bank 7: 32,4,16
```

These are zero-based post-remap selectors. Current C++ setup diagnostics do
not choose an extended sequence bank or advance its stage. Snapshot metadata
therefore marks direct probes of ids `14`, `15`, and `17` as
`btech_extended_pre_remap_diagnostic` and `placementSelectorRuntimeResolved=false`.

The same writer derives `DS:044A`, hence `BTECH context[2]`, as the remainder
of `(selected_planet_entry[2] - 1) / 3`, proving the previously suspected
normalized environment mapping. Before returning nonzero to `MW.EXE`, the loop
at file offsets `0x00225A..0x002273` copies local `DS:0448..0B5F` back to the
external far block. `DS:0450`, hence `BTECH context[8]`, is copied from
`DS:0B55` immediately before that loop. The options path at file offsets
`0x0075B0..0x0075EF` increments it modulo three and indexes `LOW/MED/HIGH`;
it is the graphics terrain-detail level, not the independent `SNARIO.DAT`
terrain mode.

## BTECH battle context fields

On fresh battle setup, `BTECH.EXE` stores the far pointer to the incoming battle
context in `0xc93e:0xc940` and reads these fields:

| Context offset | BTECH use |
|---:|---|
| `+0x001` | side/mission boolean into `0xc34c` |
| `+0x002` | environment id, clamped to `0..2` for DESERT/TROPIC/ARCTIC PAL |
| `+0x006` | seed source, `seed = context[6] % 20`, stored in `0x5336` |
| `+0x007` | scenario/mission selector minus one, stored in `0xc96e`; also indexes the battle mission text table |
| `+0x008` | graphics terrain-detail level (`LOW/MED/HIGH`) into `0xc352` |
| `+0x0c2..0x0c9` | four u16 player lance/mech slot ids |
| `+0x0ea..` | indirection into mech type/asset data |
| `+0x102..` | per-mech loadout/status records |
| `+0x25e..` | per-mech ammo/component state |
| `+0x2ee..` | per-mech result values written back after battle |
| `+0x5e2` | persistent battle/result flag |

Important: `seed = context[6] % 20` is the value passed into
`FUN_1000_1a22(seed)`, and that function reads `SNARIO.DAT` at
`seed << 4`. For our current visual scenario 2, this is `SNARIO.DAT` record 2,
raw:

```text
03 04 08 0F  02 03 00 00 03  01 00 03 03 02  04 04
```

This same record is why the terrain layout is `TILE3,TILE4,TILE8,TILE15`.

## SNARIO position-bank selection

`FUN_1000_1a22(seed)` first reads 16 bytes from `SNARIO.DAT[seed]` into
`DS:7d6a`.

Observed layout of the 16-byte seed record:

| Bytes | Meaning candidate |
|---:|---|
| `0..3` | four terrain tile ids |
| `4..8` | five player-side position bank selectors, indexed by player-side placement mode |
| `9..13` | five enemy/objective-side position bank selectors, indexed by enemy-side placement mode |
| `14` | player-side heading/entry flags used when placement mode is `1` |
| `15` | enemy-side heading/entry flags used when placement mode is `1` |

The selected position bank is indirect:

```text
side_mode[0]        = BTECH_word_table_092c[c96e]
side_mode[1]        = BTECH_word_table_08d8[side_mode[0]]
selector_byte_offset = 4 + side*5 + side_mode[side]
bank_index_source    = SNARIO[selector_byte_offset]
bank_id              = SNARIO[bank_index_source]
coordinate_offset    = 0x03c0 + bank_id * 0x00a0 + side_mode[side] * 0x20
```

Then BTECH copies `0x20` bytes from that `coordinate_offset` into the active
side's slot-coordinate buffer. This is enough for four object positions. The
values behave as signed 32-bit world-unit pairs, not as 16.16 fixed-point
fractions; applying the known terrain transform places them into plausible
174 x 94 battlefield grid cells.

```text
object[i].x_low  = block[i*8 + 0]
object[i].x_high = block[i*8 + 2]
object[i].z_low  = block[i*8 + 4]
object[i].z_high = block[i*8 + 6]
```

The player lance occupies runtime slots `0..3`; spawned hostile/objective
objects occupy slots `4..7`.

Decoded `c96e` mission selector table from `BTECH.EXE`. Raw instructions at
file offset `0x3068` address `DS:092C` and `DS:08D8`; the independently proven
load-image mapping is `file_offset = ds_offset + 0x26A54`. The earlier
`0x26A56` decoder base was one word late and has been corrected:

```text
selector  player_mode  opposing_mode
0..6      4            0
7         0            2
8         2            4
9..13     3            3
14        4            0
15        0            2
16        3            3
17        2            4
18..19    1            1
20        2            4
21..27    3            3
28        0            2
29..30    3            3
31        0            2
32..34    3            3
```

`BattleSetupMetadata` now keeps the selector stages separate: zero-based
briefing id, one-based MW_MAIN handoff id, initial `context[7]-1` selector,
the selector actually passed to the placement decoder, and a runtime-resolved
flag. Selector `10` locks the direct chain `10 -> handoff 11 -> placement 10`;
selector `17` remains a pre-remap diagnostic until an extended-route bank and
stage are supplied explicitly.

## Diagnostic helper

`tools/decode_btech_placement.py` decodes the current evidence directly from
`Original/BTECH.EXE` and `Sorted Original Files/DAT/SNARIO.DAT`.

Useful commands:

```text
python tools/decode_btech_placement.py --list-mission-selectors
python tools/decode_btech_placement.py --seed 2 --mission-selector 10
```

For current scenario seed `2` and selector `10`, the decoded modes are
`player=3`, `opposing=3`. The first player-side slot lands at
`world=(-15872,2048)`, approximately `grid=(55.25,42.88)`, while the first
opposing-side slot lands at `world=(13824,-2560)`, approximately
`grid=(113.25,51.88)`.

## C++ diagnostic integration

The same hypothesis is now integrated as an explicit opt-in battle simulator
path:

```text
build\Release\mw_battle_viewer.exe --smoke --battle-command-map --battle-placement original --mech-preset locust --terrain-scenario-index 2 --terrain-environment-id 1 --battle-sim-ticks 0
```

`src/battle/battle_placement.*` keeps the decoded BTECH mode tables static and
reads `SNARIO.DAT` for scenario seed records plus coordinate banks.
`src/battle/battle_setup.*` is the current unified bridge: it treats
`SNARIO seed + mission selector` as one battlefield setup, returning both the
four-tile terrain layout and the player/enemy/target transforms. The viewer uses
this setup only when `--battle-placement original` is selected:

```text
player combatant  -> player-side slot 0
enemy combatant   -> opposing-side slot 0
cyan map target   -> opposing-side slot 1
player heading    -> cyan target, unless --battle-heading-deg is explicit
enemy heading     -> opposing-side facing, 180 degrees from player-to-enemy
```

The old manual placeholder placement remains the default for existing probes.
`mw_battle_viewer_smoke_battle_original_placement` locks the scenario-2,
mission-selector-10 diagnostic values:

```text
setup_tiles=TILE3,TILE4,TILE8,TILE15
setup_terrain_mode=3
player_grid=55.25,42.88
enemy_grid=113.25,51.88
target_grid=110.25,65.88
```

## Enemy/objective creation

Player objects are created first from `battle_context + 0x0c2..0x0c9`.

After that, BTECH creates hostile/objective objects with `FUN_1000_3502(type)`.
The counts come from context bytes `+3`, `+4`, and `+5`, with campaign
continuation adjustment when the extended mission chain index `0xc35a` is
active:

```text
count_a = context[3]
count_b = context[4]
count_c = context[5]

if campaign_chain_active:
    count = (count - chain_index + 2) / 3
```

For each requested object, BTECH visits the three count buckets in a repeated
`+3 -> +4 -> +5` round-robin and picks a type from small random masks:

```text
count_a -> FUN_1000_3502(((rand & 0x0c) >> 2) + 4)
count_b -> FUN_1000_3502(((rand & 0x04) >> 2) + 2)
count_c -> FUN_1000_3502((rand & 0x04) >> 2)
```

If the selected type is too expensive or already used, `FUN_1000_3502` walks a
fallback ring using tables near `0x982` and `0x992`, then allocates an object
record through `FUN_1000_36d3(0)` and initializes it through `FUN_1000_3577`.
`FUN_1000_3502` increments the shared object count before allocation and refuses
the fifth request, proving a four-object cap. For example, fresh-battle counts
`2,1,3` request buckets/types in this order:

```text
context +3 -> candidate type 4..7
context +4 -> candidate type 2..3
context +5 -> candidate type 0..1
context +3 -> candidate type 4..7
remaining counts = 0,0,2; fifth object is not requested
```

The runtime-to-placement slot mapping is also proven. `FUN_1000_36d3(0)` starts
at runtime object slot `4` and scans the four hostile records in order, so the
first through fourth successful requests occupy runtime slots `4..7`.
`FUN_1000_1a22` then iterates all eight runtime slots and copies position pair
`i` from the two active 32-byte coordinate blocks into runtime object slot `i`.
Consequently the hostile requests map directly to the opposing-side placement
slots:

```text
runtime object 4 -> opposing slot 0
runtime object 5 -> opposing slot 1
runtime object 6 -> opposing slot 2
runtime object 7 -> opposing slot 3
```

This does not prove that all four objects are hostile mechs for every mission,
nor does it identify the mission objective. The separate active-objective
pointer at `0xccee` remains outside this spawn-plan claim. Static xrefs and raw
instructions currently show its only fresh-battle assignment in the mode-4
allocation block; no non-mode-4 allocator has been proven.

This explains why enemy count/type distribution belongs to BTECH initialization,
not to the command map renderer.

### BTECH mech type identity

The eight generated type ids now have a concrete catalog mapping. The fixed-width
name table in `BTECH.EXE` at file offsets `0x028A27..0x028A8D` contains exactly
eight 13-byte entries in generator order:

```text
0 LOCUST        -> locust
1 JENNER        -> jenner
2 PHOENIX HAWK  -> phoenix_hawk
3 SHADOW HAWK   -> shadow_hawk
4 RIFLEMAN      -> rifleman
5 WARHAMMER     -> warhammer
6 MARAUDER      -> marauder
7 BATTLEMASTER  -> battlemaster
```

This aligns exactly with the proven random ranges: `0..1` light, `2..3`
medium, and `4..7` heavy. Wasp and Wolverine are present in the broader
MW_MAIN/save catalog but absent from this BTECH combat type table and therefore
must not be generated as opposing mechs.

The original Dark Wing final battle is also a separate, proven BTECH path. A
raw handoff mission byte of `0x63` is decoded at `BTECH.EXE.c:8911..8916` into
the special flag at `0xc7be`, sequence group `8`, and an initial internal value
of `0x10`. Since the sequence index starts at `-1`, the common chain step at
`BTECH.EXE.c:8947..8955` advances it to zero and reads the first group-8 table
value `13`, producing mission selector `12` after the common `-1` conversion.

The special opposition branch at `BTECH.EXE.c:9140..9144` calls the mech
constructor with exact type order `7, 7, 7, 5`: three BattleMasters followed
by one Warhammer. The already-proven allocator/coordinate copy therefore maps
them without another inference:

```text
spawn 0: BattleMaster -> runtime object 4 -> opposing slot 0
spawn 1: BattleMaster -> runtime object 5 -> opposing slot 1
spawn 2: BattleMaster -> runtime object 6 -> opposing slot 2
spawn 3: Warhammer    -> runtime object 7 -> opposing slot 3
```

C++ owns the raw `0x63`, selector `12`, aggregate counts, and these four ordered
slots in `BattleOppositionRosterMetadata`. The metadata-only factory remains
non-spawning; `applyOriginalDarkWingFinalOpposition` is the explicit bridge
that creates four static `BattleSnapshot::combatants` on the decoded opposing
transforms. It adds no AI or movement. Selector `12` uses placement modes
`3/3`, so this path does not allocate a mode-4 dedicated factory object;
final-battle
objective semantics remain separate and deferred.

## Active objective allocation

The fresh-battle path in `FUN_1000_1a22` proves that the active objective is
not one of runtime hostile slots `4..7`. While loading the two 32-byte side
placement blocks, BTECH remembers the selector for any side whose placement
mode is `4`. It then reads a separate 8-byte transform record and allocates:

```text
FUN_1000_b157(resource_base + 14, objective_transform)
object flags |= 0x08
0xccee = object pointer
0xc968 = object pointer
0xccf0 = 0x28
```

Many targeting/render paths address this object as special target index `8`,
where ordinary mech records use indices `0..7`. The depletion path at
`BTECH.EXE.c:11254..11267` sets flag `0x80` and clears `0x08` when the `0xccf0`
counter reaches zero. The `resource_base + 14` allocation is consistent with
the user-identified static factory at `OTHPCK.TBL` runtime record `014`.

This proves separate object ownership, the dedicated 8-byte transform, and the
model-relative record index. C++ now exposes a mode-4 object as
`btech_mode4_dedicated_object`, while non-mode-4 `opposing:1` remains
`setup_derived_diagnostic`. Neither state claims mission victory/destruction
semantics, and neither is folded into `BattleSnapshot::combatants`.

The C++ decoder now preserves this distinction explicitly. A mode-4 objective
is read from `SNARIO.DAT` at `0x0dc0 + bank_id * 8` into snapshot-owned
`BattleSetupMetadata::dedicatedObjective`. Controlled scenario-2 probes lock:

```text
selector 10: modes 3/3, no mode-4 object; opposing:1 remains diagnostic
selector  0: player mode 4, bank 15, offset 0x0e38, grid 152.25,75.875
selector 17: opposing mode 4, bank 8, offset 0x0e00, grid 43.125,66.40625
```

Selector `17` in that list is intentionally a direct placement-table probe,
not a claim about the first battlefield of an extended siege contract. The
real extended path remaps briefing id `17` through one of the eight sequence
banks before setup.

The mode-4 metadata carries special target index `8` and resource record `14`.
Its dedicated transform replaces the diagnostic map transform in
`BattleObjectiveState`, with `transformProven=true`,
`activeObjectProven=true`. Mission semantics are bound separately below.

## Initial facing

After positions are copied, BTECH computes heading separately.

For the common placement modes `0`, `2`, and `4`, it computes a bearing between
the first enemy-side object and the first player-side object and assigns:

```text
player-side heading = bearing_to_enemy
enemy-side heading  = bearing_to_enemy + 0x7ff8
```

That matches the observed behavior that the player spawns looking toward the
target/opposing side.

For placement mode `1`, heading comes from bit flags in `SNARIO[14]` or
`SNARIO[15]` and maps to fixed cardinal/semi-cardinal values:

```text
bit 0 -> 0
bit 1 -> 0x7ff8
bit 2 -> 0x3ffc
bit 3 -> -0x3ffc
```

For placement mode `3`, heading is computed toward the active objective object
stored at `0xccee`.

## Battlefield perimeter and actor exits

The dashed rectangle on `STATUS.SCR` and `MAP.SCR` is a runtime overlay, not
part of either SCR bitmap. `FUN_2000_158b` projects the two fixed BTECH boundary
records at `DS:0336` and `DS:0342` through the same north-up world-to-map helper
used for actors. Their position payloads contain the opposite corners:

```text
original world X: -0xac80 .. +0xac80  (-44160 .. +44160)
original world Z:   -24000 .. +24000
```

These are the same constants used by the out-of-bounds helper
`FUN_2000_272f`. Under the established C++ terrain transform they become:

```text
world_x = (original_x + 0xac80) / 0x200 * terrain_cell_size
world_z = (24000 - original_z) / 0x200 * terrain_cell_size

terrain_cell_size 500 -> X 0 .. 86250, Z 0 .. 46875
```

`FUN_2000_272f` returns edge ids `0..3`, and battle initialization stores an
escaped actor as `status = -(edge_id + 2)`. The exact mapping is:

```text
edge 0 / status -2 / mask 0x01 -> north  (+original Z, -grid Y)
edge 1 / status -3 / mask 0x02 -> south  (-original Z, +grid Y)
edge 2 / status -4 / mask 0x04 -> west   (-original X, -grid X)
edge 3 / status -5 / mask 0x08 -> east   (+original X, +grid X)
```

The masks are not a new table guess. `SNARIO[14]` is loaded directly at
`DS:7D78` for the player side and `SNARIO[15]` at `DS:7D79` for the opposing
side. They are active only when that side's placement mode is `1`.
`FUN_2000_2699` uses the escaped actor's edge bit and this mask when deciding
whether a side is complete. This proves a general mode-1 allowed-exit policy
that can support missions where an actor must leave the field; it does not yet
prove which mission title should be named pursuit or sprint.

The main battle loop handles the player separately:

```text
ordinary player exit                         -> raw result code 2
player mode 1 + exit edge present in mask    -> raw result code 0
```

Result `0` is already proven as the success/payment path. The user-provided
original capture independently shows an ordinary player escape as contract
failure, but C++ still treats result `2` as a raw exit result rather than
guessing a final public outcome enum.

`FUN_2000_158b` uses the same mode-1 mask to choose one of two palette roles for
each dashed edge: bit `1` north, bit `2` south, bit `8` east, bit `4` west.
The environment-strided EGA source table exposes pairs desert `11/4`, tropic
`11/15`, arctic `10/14`. Comparison against the supplied original captures
locks the active visual branch used by the viewer to desert `15/4`, tropic
`15/11`, arctic `8/14`; the active PAL then supplies their RGB values. The dash
phase is `(frame & 7) - 4`, with five-pixel runs on an eight-pixel stride and
opposite travel directions on opposite edges. Each run is clipped to its own
edge extent, not merely to the larger map rectangle, so it cannot cross a
corner during animation.

The C++ viewer now applies this contract consistently to `STATUS.SCR`,
`MAP.SCR`, and the cockpit MFD. Original setup uses the fixed boundary center
and one GRD cell per virtual pixel; manual diagnostic placement retains the
older terrain/object bounds fit. Snapshot metadata and rendering still do not
evaluate an escape, end a battle, or apply campaign settlement.

## C++ integration guidance

Current C++ integration status:

1. `BattleStartParams` now carries setup-derived diagnostic metadata through
   immutable `BattleSnapshot`: player slot `0`, four opposing-side slots,
   objective slot `opposing:1`, terrain tiles/mode, and initial facing.
2. `BattleStartParams` also carries campaign-derived contract metadata from the
   accepted `ContractOffer`: employer house, target house, target planet,
   target planet terrain code, derived battle environment id, estimated H/M/L
   force, and negotiated price/salvage/advance. This is data-only; rare
   zero-opposition/garrison auto-resolve remains deferred.
3. `BattleSnapshot::combatants` remains the single source for player/enemy
   blips. The cyan target is now `BattleSnapshot::objective` with provenance
   `setup_derived_diagnostic` for original setup or `manual_diagnostic` for
   explicit command-map probes.
4. User-provided original battle/viewer captures identify the cyan objective's
   world visual as the static factory model in `OTHPCK.TBL`, runtime record
   `014`. The snapshot now carries this resource reference and the C++ viewer
   places its unchanged `GpuBatch` at the objective transform. This is visual
   evidence only; destructibility, collision, and objective completion remain
   unresolved.
5. Non-player `BattleSnapshot::combatants` now have independent catalog-based
   3D visual instances. Their `mechPresetId` selects the resource/model and
   their snapshot transform selects the world placement, so red map blips and
   world mechs share the same owner. This is a static presentation bridge only;
   it does not add NPC movement or AI.
6. `src/battle/battle_opposition.*` now preserves the proven fresh-battle
   BTECH-side request sequence as `BattleOppositionSpawnPlan`. It records source
   counts, context offsets, candidate type ranges, the four-object cap,
   unconsumed counts, and the proven `runtime 4..7 -> opposing 0..3` placement
   mapping. The MW_MAIN writer also proves `context +3/+4/+5 = estimated
   heavy/medium/light`, so each request carries that typed estimate class.
   The plan still creates no combatants.
7. The Phase 10 explicit launch roster now preserves `BattleTeam`, optional
   faction house, provenance, and source slot through `BattleStartParams` into
   every immutable `CombatantSnapshot`. One explicitly passed opposing entry
   creates exactly one static non-player combatant at its supplied transform;
   H/M/L estimates still create none. Player/enemy map filtering uses the team
   field while `playerControlled` remains only a control flag. The eight-entry
   BTECH type table above now converts candidate ranges into exact preset lists;
   Wasp and Wolverine remain excluded.
8. Keep the proven `DS:0B55 -> context[8]` graphics-detail handoff separate
   from `SNARIO.DAT` terrain mode. Preserve `0xccee` as a separately owned
   special target. Mode-4 setups use the proven dedicated transform; other
   setups retain diagnostic `opposing:1` until their target source is proven.
9. Keep `--battle-placement original` opt-in until visual comparison across
   several original mission starts confirms the selector/slot mapping.
10. `BattlefieldBoundaryState` now preserves the fixed BTECH perimeter, exact
    actor exit/status encoding, mode-1 masks from `SNARIO[14..15]`, and raw
    player exit results through `BattleStartParams` into immutable
    `BattleSnapshot`. The three map surfaces render its dashed edge geometry,
    animation phase, and environment palette indices directly from the
    snapshot. Runtime crossing detection, mission termination, and campaign
    outcome handling remain deferred.

### Mission-role boundary

Placement mode `4` is not enough to infer protect/destroy semantics. The
mission-title selectors for factory assault (`22..26`) use modes `3/3`, while
the mode-4 set is `0..6, 8, 14, 17, 20`. BTECH also sets `0xeea` for selectors
`10..13`; `FUN_1000_500b` refuses damage to the special object while that flag
is set. C++ therefore derives typed briefing intent only from exact MW_MAIN
titles: `protect` for `2..7`, `destroy` for `22,24,25,29,31`, and `disable`
for `23,26`, with a normalized target kind. Selectors `2..6` are the narrow
intersection where the exact defense title and dedicated mode-4 object coexist;
their intent is bound to that transform and `missionSemanticsProven=true`.
Selector `7` and every assault selector retain proven title intent but an
unbound diagnostic transform. The proven damage branch remains separate as
snapshot-owned `damagePolicyProven=true` and
`damageSuppressed=(selector in 10..13)`.

`BattleWorld::create` enforces the ownership boundary: a valid original setup
selector must match the mission id, proven intent must match the catalog entry,
and a bound intent requires the matching dedicated setup slot. It still does
not evaluate damage, completion, or mission outcome.

The special-object depletion outcome is now independently proven. In
`BTECH.EXE.c:31885..31919`, `FUN_2000_25e8` sets the player win-condition flag
at `DS:7D88` when player placement mode is `3` and `DS:CCF0` reaches zero; the
same test for the opposing mode sets the player loss-condition flag at
`DS:7D89`. The surrounding actor scan establishes the direction: active side-1
actors clear `7D88`, while active side-0 actors clear `7D89`. After the delay,
the main loop passes `1 - DS:7D88` to `FUN_1000_37eb`, so simultaneous flags
resolve through the player-win flag to raw result code `0`.

`FUN_1000_37eb` writes that byte to shared battle context `+0x1f`.
`MW_MAIN.EXE.c:2649..2813` consumes the corresponding local `DS:0467`: zero
enables contract payment and the employer reputation-success branch, while a
nonzero value suppresses payment. This proves raw code `0` as player success
and code `1` as player loss for the automatic BTECH outcome path. C++ stores
the result as `depletionPolicyProven`, the two condition flags, and
`depletionResultCode`; it does not decrement `CCF0`, apply damage, end the
mission, or perform campaign settlement.

Do not implement final NPC AI, objective rules, collision, LOS, or victory
conditions as part of this placement bridge.

## Open xrefs

Remaining hard proof needed:

- the coordinate relationship between the separate objective transform table,
  mission semantics, and the current diagnostic cyan `opposing:1` slot.
- the runtime side/transform relationship for non-mode-4 `destroy`, `disable`,
  and retrieval objectives.
- the final campaign-facing name and reputation treatment for raw player-exit
  result code `2`; the BTECH escape source and non-success path are proven, but
  settlement semantics should not be inferred from the debrief wording alone.
