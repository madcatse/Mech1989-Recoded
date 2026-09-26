# MARPCK Marauder Model Mapping

Purpose: collect enough human-readable mapping data to turn `MARPCK.TBL` into a real `marauder` catalog entry.

Source resource:

```text
Sorted Original Files/TBL/viewer8/MARPCK.TBL
```

Useful viewer commands:

```bat
build\Release\mw_battle_viewer.exe --resource "Sorted Original Files\TBL\viewer8\MARPCK.TBL" --record 0
build\Release\mw_battle_viewer.exe --mech-preset marauder --animation walk
build\Release\mw_battle_viewer.exe --resource "Sorted Original Files\TBL\viewer8\MARPCK.TBL" --record 22 --assembled --assembly-records 22,18 --animation-sequence 0:22,23,24,25:180 --animation-sequence 1:18,19,20,21:180
```

Viewer controls useful for mapping:

```text
[/]  switch single records
F2   show component bounds/pivots in assembled mode
F3   pose rotation demo
F4   cycle pose target component
F5   cycle pivot candidate: definition / bounds_center / bounds_bottom / bounds_top
F6   cycle rotation axis: x / y / z
P    screenshot
```

## How To Fill This

For each visible record or record range, fill only what you can tell with reasonable confidence.

Good labels are practical, not final art names:

```text
torso
head_or_cockpit
left_leg
right_leg
left_arm
right_arm
left_weapon_pod
right_weapon_pod
hip_or_pelvis
unknown_detail_a
```

Use `confidence` like this:

```text
high    visually obvious
medium  likely, but mirrored side or parent is uncertain
low     just a candidate
```

Leave unknown fields as `?`.

## Current Confirmed Mapping

Imported from `MARPCK_model_mapping_done.xlsx` on 2026-08-09.

| component_id | label | side | parent_label | bind_record | confidence | notes |
| ---: | --- | --- | --- | ---: | --- | --- |
| 1 | right_arm | right | torso | 0 | high | Right arm attached to torso. Corrected from earlier left/right swap after visual pose debug. |
| 5 | left_arm | left | torso | 1 | high | Left arm attached to torso. Corrected from earlier left/right swap after visual pose debug. |
| 4 | right_leg | right | torso | 2 | high | Right leg attached to torso. Corrected from earlier left/right swap after visual pose debug. |
| 0 | left_leg | left | torso | 3 | high | Left leg attached to torso. Corrected from earlier left/right swap after visual pose debug. |
| 2 | torso | center | root | 4 | high | Main body/root. |
| 3 | cockpit | center | torso | 5 | high | Cockpit attached to torso. |

### walk

| component_label | component_id | records | frame_ms | confidence |
| --- | ---: | --- | ---: | --- |
| right_arm | 1 | 6,7,8,9,10,11,12,13 | 180 | high |
| left_arm | 5 | 14,15,16,17,18,19,20,21 | 180 | high |
| right_leg | 4 | 22,23,24,25,26,27,28,29 | 180 | high |
| left_leg | 0 | 30,31,32,33,34,35,36,37 | 180 | high |
| torso | 2 | 38,39,40,41,42,43,44,45 | 180 | high |
| cockpit | 3 | 46,47,48,49,50,51,52,53 | 180 | high |

### damage_or_destroyed

These records are suspicious as clean anatomical components. Treat them as death/debris assembly frames, not as reliable pose components. This is now represented in C++ as `ModelAssemblyFrameAnimationDefinition`.

User verification 2026-08-09 with `destroyed_slot_probe`: hiding a single logical segment makes the Marauder flicker across death frames, especially around the arms. This confirms that the `54..83` slot interpretation is not safe for per-component masking on Marauder; some frames likely contain mixed geometry or debris from multiple logical parts.

| frame | records |
| ---: | --- |
| 0 | 54,59,64,69,74,79 |
| 1 | 55,60,65,70,75,80 |
| 2 | 56,61,66,71,76,81 |
| 3 | 57,62,67,72,77,82 |
| 4 | 58,63,68,73,78,83 |

Earlier component-slot interpretation, kept only as research context:

| slot_label | component_id | records | frame_ms | confidence | notes |
| --- | ---: | --- | ---: | --- | --- |
| right_arm_destroyed_slot | 1 | 54,55,56,57,58 | 180 | high | Slot name follows bind component; actual geometry may be mixed debris. |
| left_arm_destroyed_slot | 5 | 59,60,61,62,63 | 180 | high | Slot name follows bind component; actual geometry may be mixed debris. |
| right_leg_destroyed_slot | 4 | 64,65,66,67,68 | 180 | high | Slot name follows bind component; actual geometry may be mixed debris. |
| left_leg_destroyed_slot | 0 | 69,70,71,72,73 | 180 | high | Slot name follows bind component; actual geometry may be mixed debris. |
| torso_destroyed_slot | 2 | 74,75,76,77,78 | 180 | high | Includes odd cases such as torso/cannon/cockpit split. |
| cockpit_destroyed_slot | 3 | 79,80,81,82,83 | 180 | high | Includes odd cases such as cockpit/debris split. |

Implementation note: death now uses explicit per-frame assembled-record lists, so component damage remains free to hide/disable real components independently.

Catalog flag:

```text
destroyedSlotQuality = AssemblyOnly
```

## Damage Rules

Imported rule from original-game behavior: destroying either leg kills the mech immediately. It should not continue walking or standing with one leg.

| component_label | component_id | destroyed_action | notes |
| --- | ---: | --- | --- |
| left_leg | 0 | DestroyMech | Switch to death/debris assembly-frame animation. |
| right_arm | 1 | HideComponent | Arm can be disabled/hidden without immediate death. |
| torso | 2 | DestroyMech | Core body destruction kills mech. |
| cockpit | 3 | DestroyMech | Cockpit destruction kills mech. |
| right_leg | 4 | DestroyMech | Switch to death/debris assembly-frame animation. |
| left_arm | 5 | HideComponent | Arm can be disabled/hidden without immediate death. |

## Pivot / Local Transform Notes

Marauder currently uses identity local matrices and zero definition pivots in the C++ catalog. That is acceptable for the confirmed original record-frame walk, because the original records are already pre-positioned. The next improvement is to add meaningful component pivots for pose/debug work.

Use the viewer to gather pivot observations:

```bat
build\Release\mw_battle_viewer.exe --mech-preset marauder --animation walk
```

Controls:

```text
F2   show component bounds/pivots
F3   pose rotation demo
F4   cycle target component
F5   cycle pivot candidate: definition / bounds_center / bounds_bottom / bounds_top
F6   cycle axis: x / y / z
```

Record observations in this shape:

| component_label | best_F5_mode | best_F6_axis | visual_result | notes |
| --- | --- | --- | --- | --- |
| left_arm | bounds_bottom | y | shoulder stays attached | User observation 2026-08-09: target `left_arm id=5`; animated walk candidate drifted roughly `pivot_local=169..220,112.5,-19..34`; manual pivot likely needed at shoulder socket slightly inside torso side; `local_matrix: identity ok`. |
| right_arm | bounds_bottom | y | shoulder stays attached | User observation 2026-08-09: target `right_arm id=1`; animated walk candidate drifted roughly `pivot_local=-169..-220,112.5,24.0`; example `pivot_world=-183.5,112.5,24.0`; manual pivot likely needed at shoulder socket slightly inside torso side; `local_matrix: identity ok`. |
| left_leg | bounds_center | x | hip drifts slightly up/down | User observation 2026-08-09: target `left_leg id=0`; animated walk candidate drifted roughly `pivot_local=133.5,-124.0,-75.0..230`; hip appears sometimes inside torso and sometimes outside; manual hip pivot likely needed; `local_matrix: identity ok`. |
| right_leg | bounds_center | x | hip drifts slightly up/down | User observation 2026-08-09: target `right_leg id=4`; animated walk candidate drifted roughly `pivot_local=-134.0,-111.0,-75.0..230`; hip appears sometimes inside torso and sometimes outside; manual hip pivot likely needed; `local_matrix: identity ok`. |
| torso | bounds_center | y | stays centered | User observation 2026-08-09: target `torso id=2`; candidate `pivot_local=6.5,292.5,123.0`; looks acceptable; `local_matrix: identity ok`. |
| cockpit | bounds_bottom | y | slightly separates from torso | User observation 2026-08-09: target `cockpit id=3`; candidate `pivot_local=-28.0,217.5,216.0`; likely needs manual pivot or parented torso-following treatment so it does not visually detach; `local_matrix: identity ok`. |

Important: the ranges above are not final catalog `localPivot` constants. They are frame-varying F5 bounds candidates observed while the original walk record-frame animation was running. Use them as guides for fitting stable manual pivots from bind-frame bounds/screenshots.

## Catalog Pivot V1

Imported into `src/mech3d/mech_catalog.cpp` on 2026-08-09 as first-pass `localPivot` values. Verify with `F5=definition`; adjust after visual comparison against `bounds_center` / `bounds_bottom` candidates.

| component_label | component_id | localPivot | defaultPoseAxis |
| --- | ---: | --- | --- |
| right_arm | 1 | -194.5,112.5,24.0 | y |
| left_arm | 5 | 194.5,112.5,7.5 | y |
| right_leg | 4 | -134.0,-111.0,77.5 | x |
| left_leg | 0 | 133.5,-124.0,77.5 | x |
| torso | 2 | 6.5,292.5,123.0 | y |
| cockpit | 3 | -28.0,217.5,216.0 | y |

If none of the built-in F5 candidates look good, write a plain-language note such as:

```text
manual pivot needed: upper-left corner of hip block
manual pivot needed: shoulder socket, slightly inside torso side
```

Exact numeric `Vec3f` values can be fitted later in C++ from bounds and screenshots. The user does not need to write matrix math by hand.

## Local Transform Rule

Most original component records are already authored in model/world-relative coordinates. Leave `local_matrix` as identity unless a component is visually in the right shape but placed in the wrong location in assembled mode.

Use notes like this instead of hand-writing matrices:

```text
local_matrix: identity ok
local_matrix: needs small offset toward torso
local_matrix: record appears pre-positioned; no local offset needed
```

## Next Mapping Work

For the next mech after Marauder, prepare another spreadsheet like `MARPCK_model_mapping_done.xlsx`:

- bind pose records for the standing mech;
- logical component labels;
- parent component labels;
- walk record sequences per component;
- death as explicit per-frame assembled record lists;
- damage rule per component;
- rough pivot notes from `F2`/`F3`/`F4`/`F5`/`F6`.

Do not force death records into clean component anatomy. If a death frame is weird but looks close to the original, keep it as an assembly-frame record list.
