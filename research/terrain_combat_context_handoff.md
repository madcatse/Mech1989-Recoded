# Terrain combat-context handoff notes

Date: 2026-08-10.

Scope: Phase 5 terrain/battlefield resource generation. This note follows the
palette/biome handoff from the campaign executable toward `BTECH.EXE` and the
temporary C++ viewer/dump bridge that accepts the normalized environment id.

## Current result

The BTECH side is now strong evidence, but the campaign-side producer is still
only partially mapped:

- `BTECH.EXE` receives a far pointer to a combat context through its startup
  handoff path, stores it at `0xc93e:0xc940`, and reads terrain/battle fields
  from that context.
- Context byte `2` is a normalized environment id. BTECH clamps it to `0..2`
  and uses it for `DESERT/TROPIC/ARCTIC.PAL` selection.
- `MW_MAIN.EXE` computes a three-way campaign terrain/environment candidate
  from a mission/contract table entry: `(entry[2] - 1) % 3`.
- The extracted planet table now records the same normalized BTECH environment
  id for each planet as `(terrain_code - 1) % 3`, with the sibling
  `terrain_band = (terrain_code - 1) // 3`.
- A direct xref from that campaign candidate to `battle_context + 2` has not
  yet been found, so this remains a high-probability producer, not a proven
  implementation contract.

Do not drive C++ auto-biome selection from `SNARIO.DAT` tail bytes. The proven
BTECH consumer is the normalized combat context, not the scenario-layout table.
The C++ tools therefore accept `--terrain-environment-id 0|1|2`, mapped as
`0 -> desert`, `1 -> green`, `2 -> snow`; `--terrain-color` remains a manual
debug/screenshot override.

## BTECH context pointer and fields

`research/disassembly/BTECH.EXE/BTECH.EXE.c:7787..7790` initializes the context
far pointer:

```text
local_8 = CONCAT22(*(u16 *)0x4b5f, *(u16 *)0x5d)
uVar5 = *local_8
*(u16 *)0xc93e = *(u16 *)0x5f
*(u16 *)0xc940 = uVar5
```

The exact DOS/loader mechanics around `0x5c..0x5f` and `0x4b5f` are still not
named. Treat this as "startup handoff pointer plumbing" until the loader path
is fully decoded.

`research/disassembly/BTECH.EXE/BTECH.EXE.c:8877..8973` then reads stable
terrain-related context fields:

```text
battle_context + 0x000..0x12b  -> checksum seed region, first 300 bytes
battle_context + 0x001         -> copied to 0xc34c as a boolean/side flag
battle_context + 0x002         -> normalized environment id, clamped to 0..2
battle_context + 0x006         -> modulo-20 seed source for 0x5336
battle_context + 0x007         -> scenario/mission selector minus 1 -> 0xc96e
battle_context + 0x008         -> graphics terrain-detail level -> 0xc352
battle_context + 0x00d..0x01c  -> 16 bytes initialized to 0xff on fresh battle
battle_context + 0x01f         -> battle result; automatic path writes 0 success / 1 loss
battle_context + 0x03b..0x042  -> 8-byte signature/check against DS 0x978
battle_context + 0x0c2..0x0c9  -> four u16 mech/entity slot ids
battle_context + 0x0ea..       -> indirection to mech type/asset table
battle_context + 0x102..       -> per-mech combat loadout/status block
battle_context + 0x25e..       -> per-mech/per-slot ammo or component state
battle_context + 0x2ee..       -> per-mech u16 values written back after battle
battle_context + 0x5e2         -> persistent battle/result flag
```

The field names above are intentionally conservative. They are enough to keep
terrain palette and scenario selection separated, but not enough to define the
full battle-save structure.

## Palette consumer proof

The palette note records the direct BTECH consumer:

```text
BTECH.c:8967..8970
bVar7 = *(byte *)(battle_context + 2)
*(uint *)0x1e86 = bVar7
if (2 < bVar7) *(u16 *)0x1e86 = 2

BTECH.c:8283..8292
func_0x00015ee2(0x1000, *(u16 *)(0xd1fc + 2 * *(int *)0x1e86))
```

The PAL handle table order is:

```text
0 -> DESERT.PAL
1 -> TROPIC.PAL
2 -> ARCTIC.PAL
```

Therefore BTECH expects byte `2` to already be normalized. If the campaign
passed raw planet terrain codes above `2`, the clamp would collapse most values
to arctic, which does not match the observed three-biome design.

## MW_MAIN environment producer

`research/disassembly/MW_MAIN.EXE/MW_MAIN.EXE.c:2595..2602` derives several
mission/contract fields from a table pointed to by `0x6570`:

```text
iVar2 = *(int *)(*(int *)0x469 * 2 + 0x6570)
bVar1 = *(byte *)(iVar2 + 2) - 1
*(uint *)0x46f = bVar1 / 3
*(uint *)0x46b = bVar1 % 3
*(uint *)0x46d = *(byte *)(iVar2 + 1)
*(uint *)0x471 = *(byte *)(iVar2 + 4)
*(uint *)0x473 = *(byte *)(iVar2 + 5)
*(uint *)0x475 = *(byte *)(iVar2 + 6)
```

The accepted-contract writer now proves that `(entry[2] - 1) % 3` is copied to
combat-context byte `2`, the exact normalized `0..2` domain that BTECH consumes
for PAL selection. The sibling quotient `(entry[2] - 1) / 3` feeds a separate
group/scaling path, so the raw table byte packs at least two concepts.

The generated planet table mirrors this split in a more directly visible form:

```text
terrain_band         = (terrain_code - 1) // 3
btech_environment_id = (terrain_code - 1) % 3
```

User contract screenshots `screenshots/terrain/image0062.png` and
`screenshots/terrain/image0063.png` provide a useful concrete check: the
contract text targets the Davion planet Okefenokee and the resulting battle is
green/jungle. The extracted planet row is:

```text
OKEFENOKEE, house=Davion, terrain_code=11, environment=Tropical,
terrain_band=3, btech_environment_id=1, btech_palette=TROPIC.PAL
```

This supports the model that combat biome comes from the target planet in the
accepted contract/battle context. It does not require, and currently does not
support, deriving biome from `SNARIO.DAT` tail bytes.

Known follow-on uses:

- `0x46d` is used heavily for faction/region/economy-style tables.
- `0x471`, `0x473`, and `0x475` feed mission/layout offsets and bounds.
- `0x46b` was not yet seen at the sampled xrefs after assignment, which may
  mean it is copied through an indirect/overlay path, or that the relevant
  producer uses another normalized environment field.

## MW_MAIN startup/load context

`research/disassembly/MW_MAIN.EXE/MW_MAIN.EXE.c:143..147` checks an incoming
large block for a signature:

```text
uVar3 = *(u16 *)0x5d
iVar1 = *(int *)0x5f
if (*(char *)(iVar1 + 2000) != 'M' ||
    *(char *)(iVar1 + 0x7d4) != 'W' ||
    *(byte *)(iVar1 + 0x7da) != 0x52) {
  ...
}
```

`research/disassembly/MW_MAIN.EXE/MW_MAIN.EXE.c:2177..2189` later preserves the
same startup pointer and copies the first `0x718` bytes into its local working
area:

```text
uVar2 = *(u16 *)0x5d
puVar3 = *(u16 *)0x5f
*(u16 *)0x1225 = uVar2
*(u16 *)0x1227 = puVar3
if (*(char *)0x5c != 1) {
  copy source[0..0x717] -> DS:0x448..0xb5f
  *(byte *)0x468 = 0
}
```

This is now tied to the battle-launch copy-back at
`Original/MW_MAIN.EXE:0x00225A..0x002273`. That loop copies the complete local
working range `DS:0448..0B5F` back to the saved external far pointer before
setting return code `1`. Therefore local byte `DS:0448+n` is exactly BTECH
combat-context byte `n` on the next child launch.

The accepted-contract writer at file offsets `0x005B0D..0x005B48` proves the
leading fields:

```text
DS:044A / context[2] = (selected_planet_entry[2] - 1) % 3
DS:044B / context[3] = accepted_offer estimated heavy count
DS:044C / context[4] = accepted_offer estimated medium count
DS:044D / context[5] = accepted_offer estimated light count
DS:044E / context[6] = accepted_offer DS:0956 seed candidate + 1
DS:044F / context[7] = accepted_offer mission id/selector DS:0A2B
```

Immediately before copy-back, `DS:0450 / context[8]` is copied from `DS:0B55`.
The producer and meaning are now proven by the raw `MW_MAIN.EXE` paths:

```text
file 0x006EB8: C6 06 55 0B 02       DS:0B55 = 2 (default HIGH)
file 0x0075B0: 80 06 55 0B 01       increment DS:0B55
file 0x0075B5: 80 3E 55 0B 03       compare with 3
file 0x0075BC: C6 06 55 0B 00       wrap to 0
file 0x0075DD: 8A 1E 55 0B          select text by DS:0B55
DS:A079: LOW\0 MED\0 HIGH\0
```

Thus context byte `8` is the campaign graphics detail setting used by BTECH as
a terrain-detail level. In `FUN_1000_242c`, `0xc352 == 0` skips the four GRD
tile loads, nonzero values load them, and level `1` takes an additional
processing branch. This field is unrelated to the separate `terrainMode` byte
stored in each `SNARIO.DAT` record.

## MW.EXE wrapper evidence

`research/disassembly/MW.EXE/MW.EXE.c` is a small wrapper/loader rather than
normal game logic. Its extracted resource strings include only the two child
executables:

```text
research/extract/maps/MW.EXE.map.csv
0x000A43 -> MW_MAIN.EXE
0x000A4F -> BTECH.EXE
```

The loop at `MW.EXE.c:176..199` repeatedly prepares a DOS exec parameter block
with the current far pointer:

```text
*(byte *)0x808 = *(byte *)0x9eb or *(byte *)0x9ea
*(u16 *)0x7df = current_segment
*(ptr *)0x7e1 = current_shared_block
iVar5 = 0x7f9
int 21h
```

This makes the current model:

```text
MW.EXE wrapper keeps/points at shared campaign-combat block
  -> launches MW_MAIN.EXE for campaign/contract work
  -> launches BTECH.EXE for combat
  -> both children recover the shared block from startup handoff fields
```

The producer does not call `BTECH` directly. It writes the local campaign block,
copies it back through the saved far pointer, returns nonzero, and lets the
wrapper launch `BTECH.EXE` with the same block.

## Next focused probes

1. Map the table at `MW_MAIN` DS `0x6570` far enough to label `entry[1]`,
   `entry[2]`, and `entry[4..7]` against known planet/contract data.
2. Keep C++ biome selection snapshot-owned through target planet terrain code
   and normalized environment id. Do not infer biome from `SNARIO.DAT`.
