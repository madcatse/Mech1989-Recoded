# Cheat list

This list documents development-only tester commands for the in-game debug console.

## Enable or disable

Cheats are compiled only when debug tools are enabled:

```powershell
cmake -S . -B build -DMW_ENABLE_DEBUG_TOOLS=ON
```

For release builds, turn them off:

```powershell
cmake -S . -B build -DMW_ENABLE_DEBUG_TOOLS=OFF
```

The default local development build currently enables them.

## How to use

- Start `mw_main_recomp.exe`.
- Press `~` / tilde to open the debug console shutter at the top of the game window.
- Type a command and press `Enter`.
- Press `~` again, or `Esc`, to close the console.

The same debug messages are also written to `mwlog.txt` next to the executable. The file is recreated on every launch.

## Commands

| Command | Effect |
| --- | --- |
| `money` | Sets the player account to 10,000,000 C-Bills. |
| `exp1` | Sets all hired pilots under player command to `POOR` gunnery and piloting. |
| `exp2` | Sets all hired pilots under player command to `AVERAGE` gunnery and piloting. |
| `exp3` | Sets all hired pilots under player command to `GOOD` gunnery and piloting. |
| `exp4` | Sets all hired pilots under player command to `EXCELLENT` gunnery and piloting. |
| `mech_all` | Adds one of each playable mech, stopping at the 12-Mech ownership limit. |
| `mech_locust` | Adds one extra `LOCUST` mech to the unit inventory. |
| `mech_jenner` | Adds one extra `JENNER` mech to the unit inventory. |
| `mech_phoenixhawk` | Adds one extra `PHOENIX HAWK` mech to the unit inventory. |
| `mech_shadowhawk` | Adds one extra `SHADOW HAWK` mech to the unit inventory. |
| `mech_rifleman` | Adds one extra `RIFLEMAN` mech to the unit inventory. |
| `mech_warhammer` | Adds one extra `WARHAMMER` mech to the unit inventory. |
| `mech_marauder` | Adds one extra `MARAUDER` mech to the unit inventory. |
| `mech_battlemaster` | Adds one extra `BATTLEMASTER` mech to the unit inventory. |
| `rep1` | Sets player unit reputation to `RISKY`. |
| `rep2` | Sets player unit reputation to `WORTH WATCHING`. |
| `rep3` | Sets player unit reputation to `VETERAN`. |
| `rep4` | Sets player unit reputation to `ELITE`. |
| `month` | Moves the campaign date to the first day of the next month. |

## Adding new cheats

New commands should be added inside the `MW_DEBUG_TOOLS` block in `src/main.cpp`.

Add a small handler function, then register it in the `cheats` table inside `executeDebugConsoleCommand()`. This keeps all tester-only commands in one place and makes them easy to remove before release.
