# JENPCK Jenner Model Mapping

Purpose: preserve the imported Jenner mapping from `JENPCK_model_mapping_done.xlsx` for the C++ `jenner` catalog preset.

Source resource:

```text
Sorted Original Files/TBL/viewer8/JENPCK.TBL
```

Imported from `JENPCK_model_mapping_done.xlsx` on 2026-08-09.

## Original Behavior Constraint

The original game behavior is intentionally simple:

- mechs walk or die;
- destroying an arm hides that arm from rendering and walk animation;
- destroying a leg, torso, or cockpit kills the mech immediately;
- mech heading changes by rotating the whole model around its vertical axis;
- movement speed changes the interval between walk animation frames;
- there is no torso twist, weapon aiming pose, or damage reaction pose.

## Current Confirmed Mapping

| component_id | label | side | parent_label | bind_record | confidence | notes |
| ---: | --- | --- | --- | ---: | --- | --- |
| 1 | left_arm | left | torso | 0 | high | Left arm attached to torso. |
| 5 | right_arm | right | torso | 1 | high | Right arm attached to torso. |
| 4 | left_leg | left | torso | 2 | high | Left leg attached to torso. |
| 0 | right_leg | right | torso | 3 | high | Right leg attached to torso. |
| 2 | torso | center | root | 4 | high | Main body/root. |
| 3 | cockpit | center | torso | 5 | high | Cockpit attached to torso. |

## Walk

| component_label | component_id | records | frame_ms | confidence |
| --- | ---: | --- | ---: | --- |
| left_arm | 1 | 6,7,8,9,10,11,12,13 | 180 | high |
| right_arm | 5 | 14,15,16,17,18,19,20,21 | 180 | high |
| left_leg | 4 | 22,23,24,25,26,27,28,29 | 180 | high |
| right_leg | 0 | 30,31,32,33,34,35,36,37 | 180 | high |
| torso | 2 | 38,39,40,41,42,43,44,45 | 180 | high |
| cockpit | 3 | 46,47,48,49,50,51,52,53 | 180 | high |

## Death Forward

120-record mechs have two death animations. The default `death` catalog
animation aliases this first/forward death. This first bank is stored from the
settled pose back toward the standing pose, so runtime playback reads frames
`5,4,3,2,1,0`. The resulting playback starts with records
`59,65,71,77,83,89` and ends with `54,60,66,72,78,84`.

Raw assembly-frame storage:

| frame | records |
| ---: | --- |
| 0 | 54,60,66,72,78,84 |
| 1 | 55,61,67,73,79,85 |
| 2 | 56,62,68,74,80,86 |
| 3 | 57,63,69,75,81,87 |
| 4 | 58,64,70,76,82,88 |
| 5 | 59,65,71,77,83,89 |

Component-slot interpretation:

| slot_label | component_id | records | frame_ms | confidence |
| --- | ---: | --- | ---: | --- |
| left_arm_death_forward_slot | 1 | 54,55,56,57,58,59 | 180 | high |
| right_arm_death_forward_slot | 5 | 60,61,62,63,64,65 | 180 | high |
| left_leg_death_forward_slot | 4 | 66,67,68,69,70,71 | 180 | high |
| right_leg_death_forward_slot | 0 | 72,73,74,75,76,77 | 180 | high |
| torso_death_forward_slot | 2 | 78,79,80,81,82,83 | 180 | high |
| cockpit_death_forward_slot | 3 | 84,85,86,87,88,89 | 180 | high |

## Death Backward

Assembly-frame death:

| frame | records |
| ---: | --- |
| 0 | 90,95,100,105,110,115 |
| 1 | 91,96,101,106,111,116 |
| 2 | 92,97,102,107,112,117 |
| 3 | 93,98,103,108,113,118 |
| 4 | 94,99,104,109,114,119 |

Component-slot interpretation:

| slot_label | component_id | records | frame_ms | confidence |
| --- | ---: | --- | ---: | --- |
| left_arm_death_backward_slot | 1 | 90,91,92,93,94 | 180 | high |
| right_arm_death_backward_slot | 5 | 95,96,97,98,99 | 180 | high |
| left_leg_death_backward_slot | 4 | 100,101,102,103,104 | 180 | high |
| right_leg_death_backward_slot | 0 | 105,106,107,108,109 | 180 | high |
| torso_death_backward_slot | 2 | 110,111,112,113,114 | 180 | high |
| cockpit_death_backward_slot | 3 | 115,116,117,118,119 | 180 | high |

Note: the `Records` sheet has one likely typo for the final backward-death frame, but the `Animations` sheet gives the consistent torso sequence `110..114`; C++ uses frame 4 as `94,99,104,109,114,119`.

Catalog flag:

```text
destroyedSlotQuality = ComponentClean
```

## Damage Rules

| component_label | component_id | destroyed_action | notes |
| --- | ---: | --- | --- |
| right_leg | 0 | DestroyMech | Switch to death assembly-frame animation. |
| left_arm | 1 | HideComponent | Arm can be disabled/hidden without immediate death. |
| torso | 2 | DestroyMech | Core body destruction kills mech. |
| cockpit | 3 | DestroyMech | Cockpit destruction kills mech. |
| left_leg | 4 | DestroyMech | Switch to death assembly-frame animation. |
| right_arm | 5 | HideComponent | Arm can be disabled/hidden without immediate death. |

## Pivot Notes

Pivots are not fitted yet. The C++ catalog currently uses zero `localPivot` values for Jenner and first-pass default axes:

| component_label | component_id | localPivot | defaultPoseAxis |
| --- | ---: | --- | --- |
| left_arm | 1 | 0.0,0.0,0.0 | y |
| right_arm | 5 | 0.0,0.0,0.0 | y |
| left_leg | 4 | 0.0,0.0,0.0 | x |
| right_leg | 0 | 0.0,0.0,0.0 | x |
| torso | 2 | 0.0,0.0,0.0 | y |
| cockpit | 3 | 0.0,0.0,0.0 | y |

Useful viewer commands:

```bat
build\Release\mw_battle_viewer.exe --mech-preset jenner --animation walk
build\Release\mw_battle_viewer.exe --mech-preset jenner --animation death_forward
build\Release\mw_battle_viewer.exe --mech-preset jenner --animation death_backward
build\Release\mw_battle_viewer.exe --mech-preset jenner --animation death_forward_slot_probe
build\Release\mw_battle_viewer.exe --mech-preset jenner --animation death_backward_slot_probe
```
