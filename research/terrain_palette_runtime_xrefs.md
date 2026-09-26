# Terrain palette runtime xrefs

Date: 2026-08-10.

Scope: Phase 5 terrain/battlefield resource generation. This note records the
BTECH decompile xrefs that connect combat biome/environment to terrain PAL
resources. It does not change any C++ runtime behavior.

## Summary

BTECH does not appear to choose combat terrain palette from `SNARIO.DAT`.
Instead, it receives a small environment id from the battle context, clamps it
to `0..2`, and uses that id to select one of three already-loaded PAL handles:

```text
environment id 0 -> d1fc -> desert.pal
environment id 1 -> d1fe -> tropic.pal
environment id 2 -> d200 -> arctic.pal
```

`d202` stores `dmgpal.pal` and is used separately by another setup path. It is
not a normal biome.

## Palette load

`research/disassembly/BTECH.EXE/BTECH.EXE.c:7804..7814` loads all four combat
PAL resources during battle initialization:

```text
d1fc = FUN_1000_5df8(..., 0x61a)  -> desert.pal
d200 = FUN_1000_5df8(..., 0x625)  -> arctic.pal
d1fe = FUN_1000_5df8(..., 0x630)  -> tropic.pal
d202 = FUN_1000_5df8(..., 0x63b)  -> dmgpal.pal
func_0x00015ee2(..., d200)        -> initial/default palette apply
```

The pointer values are three bytes before the ASCII filenames:

```text
0x61a + 3 = 0x61d -> desert.pal
0x625 + 3 = 0x628 -> arctic.pal
0x630 + 3 = 0x633 -> tropic.pal
0x63b + 3 = 0x63e -> dmgpal.pal
```

Using the established BTECH data mapping from earlier PCK filename work,
`file_offset = ds_offset + 0x26A54`, these correspond to the strings at:

```text
0x027071 desert.pal
0x02707C arctic.pal
0x027087 tropic.pal
0x027092 dmgpal.pal
```

## Runtime palette select

`research/disassembly/BTECH.EXE/BTECH.EXE.c:8968..8970` derives the terrain
environment id from the battle-context record:

```text
bVar7 = *(byte *)(battle_context + 2)
*(uint *)0x1e86 = bVar7
if (2 < bVar7) *(undefined2 *)0x1e86 = 2
```

This means BTECH expects byte `2` of the combat context to already be a
normalized environment id. It is not the raw planet `terrain_code` value
`1..12`; raw values above `2` would all collapse to `2`/arctic.

`research/disassembly/BTECH.EXE/BTECH.EXE.c:8283..8292` later applies the active
terrain palette:

```text
func_0x00015ee2(0x1000, *(u16 *)(0xd1fc + 2 * *(int *)0x1e86))
```

Because the handles were stored in the order `d1fc`, `d1fe`, `d200`, the
runtime selector is:

```text
0 -> DESERT.PAL
1 -> TROPIC.PAL
2 -> ARCTIC.PAL
```

This matches the C++/starmap environment ordering if the campaign side passes a
normalized enum (`Desert`, `Tropical`, `Ice`) into combat.

The planet-table extractor now emits the normalized campaign-to-BTECH fields
explicitly. For example, user-provided Okefenokee contract captures show a
green/jungle battle; `research/analysis/mw_main_planet_table.csv` has:

```text
OKEFENOKEE terrain_code=11 btech_environment_id=1 btech_palette=TROPIC.PAL
```

That is consistent with a target-planet/contract-derived biome, not a
`SNARIO.DAT` palette field.

## Relation to SNARIO.DAT

The terrain loader still uses `SNARIO.DAT` for tile ids and terrain mode:

```text
bytes 0..3 -> four TILE ids
byte 8     -> terrain mode / B-variant selection
```

No xref found so far connects unresolved `SNARIO.DAT` tail bytes to
`0x1e86`, the PAL handle table, or `func_0x00015ee2`.

Therefore the dump/viewer should keep palette modes explicit for now. A future
automatic mode should take a normalized combat/campaign environment id, not a
guessed `SNARIO.DAT` byte.

## Remaining questions

- The battle-context producer is now proven: the accepted-contract writer in
  `MW_MAIN.EXE` stores `(selected_planet_entry[2] - 1) % 3` at local `DS:044A`,
  and the copy-back loop transfers it to BTECH context byte `2`. See
  `research/terrain_combat_context_handoff.md` for the instruction bytes.
- Confirm which PAL bank/runtime remap is applied inside `func_0x00015ee2`.
- Keep `dmgpal.pal` separate from biome selection until the damage/status
  palette path is named.
