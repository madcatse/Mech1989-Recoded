# LOCPCK Locust Model Mapping

Purpose: preserve the imported Locust mapping from `LOCPCK_model_mapping_done.xlsx` for the C++ `locust` catalog preset.

Source resource:

```text
Sorted Original Files/TBL/viewer8/LOCPCK.TBL
```

Imported from `LOCPCK_model_mapping_done.xlsx` on 2026-08-09.

## Current Confirmed Mapping

| component_id | label | side | parent_label | bind_record | confidence | notes |
| ---: | --- | --- | --- | ---: | --- | --- |
| 0 | left_leg | left | torso | 3 | high | Left leg attached to torso. Corrected from earlier left/right swap after visual pose debug. |
| 1 | right_arm | right | torso | 0 | high | Right arm attached to torso. Corrected from earlier left/right swap after visual pose debug. |
| 2 | torso | center | root | 4 | high | Main body/root. |
| 3 | cockpit | center | torso | 5 | high | Cockpit attached to torso. |
| 4 | right_leg | right | torso | 2 | high | Right leg attached to torso. Corrected from earlier left/right swap after visual pose debug. |
| 5 | left_arm | left | torso | 1 | high | Left arm attached to torso. Corrected from earlier left/right swap after visual pose debug. |

## Walk

| component_label | component_id | records | frame_ms | confidence |
| --- | ---: | --- | ---: | --- |
| right_arm | 1 | 6,7,8,9,10,11,12,13 | 180 | high |
| left_arm | 5 | 14,15,16,17,18,19,20,21 | 180 | high |
| right_leg | 4 | 22,23,24,25,26,27,28,29 | 180 | high |
| left_leg | 0 | 30,31,32,33,34,35,36,37 | 180 | high |
| torso | 2 | 38,39,40,41,42,43,44,45 | 180 | high |
| cockpit | 3 | 46,47,48,49,50,51,52,53 | 180 | high |

## Damage Or Destroyed

Unlike Marauder, the user observed that Locust death/destroyed records appear to stay properly segmented by component. C++ still imports death as an assembly-frame animation for consistency with the current renderer path, while keeping component-slot probe sequences available for future per-component damage work.

User verification 2026-08-09 with `destroyed_slot_probe`: the Locust remains visually clean when logical segments are hidden during the death-slot animation; the animation continues without the missing part and without cross-frame flicker. Treat the Locust `54..83` slots as component-clean enough for future targeted damage experiments.

Assembly-frame death:

| frame | records |
| ---: | --- |
| 0 | 54,59,64,69,74,79 |
| 1 | 55,60,65,70,75,80 |
| 2 | 56,61,66,71,76,81 |
| 3 | 57,62,67,72,77,82 |
| 4 | 58,63,68,73,78,83 |

Component-slot interpretation:

| slot_label | component_id | records | frame_ms | confidence |
| --- | ---: | --- | ---: | --- |
| right_arm_destroyed_slot | 1 | 54,55,56,57,58 | 180 | high |
| left_arm_destroyed_slot | 5 | 59,60,61,62,63 | 180 | high |
| right_leg_destroyed_slot | 4 | 64,65,66,67,68 | 180 | high |
| left_leg_destroyed_slot | 0 | 69,70,71,72,73 | 180 | high |
| torso_destroyed_slot | 2 | 74,75,76,77,78 | 180 | high |
| cockpit_destroyed_slot | 3 | 79,80,81,82,83 | 180 | high |

Catalog flag:

```text
destroyedSlotQuality = ComponentClean
```

## Damage Rules

| component_label | component_id | destroyed_action | notes |
| --- | ---: | --- | --- |
| left_leg | 0 | DestroyMech | Switch to death/debris assembly-frame animation. |
| right_arm | 1 | HideComponent | Arm can be disabled/hidden without immediate death. |
| torso | 2 | DestroyMech | Core body destruction kills mech. |
| cockpit | 3 | DestroyMech | Cockpit destruction kills mech. |
| right_leg | 4 | DestroyMech | Switch to death/debris assembly-frame animation. |
| left_arm | 5 | HideComponent | Arm can be disabled/hidden without immediate death. |

## Pivot Notes

Pending user visual pass.

Use:

```bat
build\Release\mw_battle_viewer.exe --mech-preset locust --animation walk
```

Helpful controls:

```text
Space  pause/resume animation
F9     previous animation frame and pause
F10    next animation frame and pause
F2     show component bounds/pivots
F3     pose demo
F4     cycle target component
F5     cycle pivot candidate
F6     cycle axis
```

Record:

| component_label | best_F5_mode | best_F6_axis | visual_result | notes |
| --- | --- | --- | --- | --- |
| left_arm | bounds_center | y | shoulder stays attached | User observation 2026-08-09: target `left_arm id=5`; candidate `pivot_local=138.0,240.0,-27.5`, `pivot_world=138.0,240.0,-27.5`; manual pivot likely needed at shoulder socket slightly inside torso side; `local_matrix: identity ok`. |
| right_arm | bounds_center | y | shoulder stays attached | User observation 2026-08-09: target `right_arm id=1`; candidate `pivot_local=-141.0,240.0,-3.5`, `pivot_world=-141.0,240.0,-3.5`; manual pivot likely needed at shoulder socket slightly inside torso side; `local_matrix: identity ok`. |
| left_leg | bounds_center | x | hip drifts slightly up/down | User observation 2026-08-09: target `left_leg id=0`; candidate `pivot_local=100.0,-75.0,15.0`, `pivot_world=100.0,-75.0,15.0`; hip appears sometimes inside torso and sometimes outside; manual hip pivot likely needed; `local_matrix: identity ok`. |
| right_leg | bounds_center | x | hip drifts slightly up/down | User observation 2026-08-09: target `right_leg id=4`; candidate `pivot_local=-101.0,-53.5,-38.0`, `pivot_world=-101.0,-53.5,-38.0`; hip appears sometimes inside torso and sometimes outside; manual hip pivot likely needed; `local_matrix: identity ok`. |
| torso | bounds_center | y | stays centered | User observation 2026-08-09: target `torso id=2`; candidate `pivot_local=0.0,195.0,17.0`, `pivot_world=0.0,195.0,17.0`; looks acceptable; `local_matrix: identity ok`. |
| cockpit | bounds_bottom | y | slightly separates from torso | User observation 2026-08-09: target `cockpit id=3`; candidate `pivot_local=11.5,120.0,59.0`, `pivot_world=11.5,120.0,59.0`; likely needs manual pivot or parented torso-following treatment so it does not visually detach; `local_matrix: identity ok`. |

Important: these values are visual F5 candidates, not final catalog `localPivot` constants. Use them as guides for fitting stable manual pivots from bind-frame bounds/screenshots.

## Catalog Pivot V1

Imported into `src/mech3d/mech_catalog.cpp` on 2026-08-09 as first-pass `localPivot` values. Verify with `F5=definition`; adjust after visual comparison against `bounds_center` / `bounds_bottom` candidates.

| component_label | component_id | localPivot | defaultPoseAxis |
| --- | ---: | --- | --- |
| right_arm | 1 | -141.0,240.0,-3.5 | y |
| left_arm | 5 | 138.0,240.0,-27.5 | y |
| right_leg | 4 | -101.0,-53.5,-38.0 | x |
| left_leg | 0 | 100.0,-75.0,15.0 | x |
| torso | 2 | 0.0,195.0,17.0 | y |
| cockpit | 3 | 11.5,120.0,59.0 | y |

User verification 2026-08-09 with `F5=definition`:

| component_label | component_id | axis | result |
| --- | ---: | --- | --- |
| right_arm | 1 | y | Pivot looks ok, but large pose-demo rotation intersects other model parts. |
| left_arm | 5 | y | Pivot looks ok, but large pose-demo rotation intersects other model parts. |
| right_leg | 4 | x | Pivot looks ok, but large pose-demo rotation intersects other model parts. |
| left_leg | 0 | x | Pivot looks ok, but large pose-demo rotation intersects other model parts. |
| torso | 2 | y | Pivot looks ok, but large pose-demo rotation intersects other model parts. |
| cockpit | 3 | y | Pivot looks ok, but large pose-demo rotation intersects other model parts. |

Follow-up user verification 2026-08-09: with smaller `F11` pose-demo amplitude, the v1 pivots look more or less normal. Keep the v1 values as acceptable for now; the visible intersections at large debug angles are probably exaggerated pose-demo motion rather than a mapping failure.
