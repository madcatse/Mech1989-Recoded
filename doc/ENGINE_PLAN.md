# MW_MAIN replacement engine plan

Goal: build a clean Windows x64 executable that reproduces `MW_MAIN.EXE`
behavior while reading the original extracted game files from `Original/`.

## Operating rules

- Keep original files as read-only data.
- Reimplement behavior from observed game behavior, disassembly notes, and
  documented resource formats.
- Build small executable vertical slices that can be compared against DOSBox
  captures.
- Keep enhanced rendering optional and separate from the compatibility path.

## Milestones

1. Boot shell
   - Native Windows x64 window.
   - Fixed 320x200 compatibility framebuffer.
   - Resource root discovery and command-line override.

2. MW_MAIN picture pipeline
   - Decode `MW_*PICS.BIN` nibble-RLE.
   - Parse archive offset and size tables.
   - Decode 48-byte DAC palettes.
   - Render packed 4bpp image records.

3. Text/UI primitives
   - Decode original `.FNT` files.
   - Draw strings, boxes, cursors, and menus with original metrics.
   - Match title/menu screens against DOSBox screenshots.

4. Campaign state
   - Decode and round-trip `.GAM` saves.
   - Model player, date, location, money, reputation, inventory, and contracts.
   - Reproduce original menu flow.

5. World simulation
   - Implement travel, hiring, markets, news, rumors, events, and story gates.
   - Load planet/starmap data from `Original/MW_MAIN.EXE` according to
     `docs/STARMAP_INTEGRATION.md`.
   - Use the confirmed original jump, cost, and travel-time formulas, validated
     against `POULSBO.GAM`, `OKEFENO.GAM`, and `ZANZIZ.GAM`.
   - Reproduce the first NEWS NET slice from confirmed captures: draw the
     full-screen GPICS frame, load article text from `MW_MAIN.EXE`, unlock
     messages by campaign date, and keep only the last seven messages visible.
   - Build deterministic tests from known saves and captured sessions.

6. Combat handoff
   - Reproduce the original `MW.EXE` protocol in-process.
   - Exchange campaign state with the later BTECH replacement module.

7. Compatibility validation
   - Record DOSBox reference sessions.
   - Compare screenshots and save-state transitions.
   - Track every known mismatch in research notes.

## Current MW_MAIN flow slice

The current executable is a playable campaign-shell slice, not yet a full game.
It uses a fixed 320x200 compatibility framebuffer and reads original resources
from `Original/`.

Implemented:

- Startup flow: Activision splash, intro/title/authorization/campaign intro,
  skipped by `Space` or left mouse click.
- Picture and text pipeline: `MW_*PICS.BIN`, selected raw `MW_PICS.BIN`
  screens, original `.FNT` files, and verified large text blocks loaded from
  `MW_MAIN.EXE` at runtime.
- Main planet shell: planet icon navigation, status panel, bar menu, mechlab
  menu, system menu, starmap, and shuttle travel animation.
- Mouse support: planet icons, starmap planets/buttons, NEWS NET buttons, and
  vertical menu rows can be clicked. Keyboard navigation remains active.
- Display scaling: sharp integer scaling modes are used. Default is `1280x1000`
  (`4x5` source pixels) for 1080p desktops; exact DOSBox-style `1600x1200`
  (`5x6` source pixels) is selected when the window is large enough.
- Starmap: planet data is loaded from `MW_MAIN.EXE`, current/selected planets
  are drawn, jump count is kept internal, cost is shown, and travel starts with
  the original-style white route line before the shuttle screen.
- Travel economy/time: confirmed jump, cost, and date-advance formulas are in
  use. Travel advances fixed 60-day displayed months and updates the current
  year/month/day state.
- NEWS NET first slice: the original GPICS frame is drawn, text is loaded from
  `MW_MAIN.EXE`, messages unlock by campaign date, only the last seven messages
  remain visible, and `Previous`/`Next`/`Done` match the observed edge behavior.
- Mechbay economy/status slice: `REVIEW MECHS`, `MECH STATUS`, `SELL`, and
  `BUY MECHS` are implemented for the eight normal playable chassis. The player
  inventory is capped at 12 Mechs, sale returns the Mech to the current planet
  market, buy/sell prices use the current temporary tier bridge, and the
  inventory/market list windows size themselves to the number of visible rows.
- Mech status art: large `MW_PICS.BIN` Mech Status images and crew miniatures
  are loaded for Locust, Jenner, Phoenix Hawk, Shadow Hawk, Rifleman,
  Warhammer, Marauder, and Battlemaster. Armor overlays use the hand-authored
  region JSON as a temporary visual model.

Known stubs and follow-up work:

- `ORDER DRINK`, `RECRUIT CREW`, `SAVE GAME`, and `RESTORE GAME` are selectable
  UI rows but still have no implemented gameplay screen/action.
- `CREW` assignment and `EXTRA AMMO` have first-pass interfaces/data, but still
  need original-behavior validation and completion.
- System menu actions implemented so far are `TURN SOUND`, `DETAIL`,
  `RESTART`, `EXIT TO DOS`, and `CONTINUE`.
- Travel currently does not finish the full economy loop: insufficient funds,
  final wealth deduction, and related warnings still need original-behavior
  validation.
- NEWS NET currently contains the confirmed early article set through April
  3025. Personal messages, including birthday messages, are not yet active
  because their delivery conditions differ from simple article-date unlocks.
- The full original NEWS NET publication table and gating rules still need to
  be completed from additional saves/disassembly. Current reverse-engineering
  notes are in `docs/NEWS_NET_RESEARCH.md`.
- Save/load UI is not wired to `.GAM` round-trip yet, even though save-field
  research exists for date and planet state.
- Hiring, contracts, rumors, story gates, exact original market generation, and
  combat handoff remain future vertical slices.
- Compact `4x5` display scaling is intentionally sharp and desktop-friendly,
  but slightly narrower than the exact `5x6` DOSBox pixel aspect.

## Needed reference material

- DOSBox recordings or screenshot sequences for: first launch, new game,
  loading saves, travel, hiring, market, contract acceptance, and launching a
  battle.
- Known-good saves before and after one action in each major menu.
- Notes on desired scope for the first playable target: title/menu only,
  campaign navigation, or campaign plus combat handoff.
- Confirmation whether the replacement executable should be named
  `MW_MAIN.EXE` at packaging time or kept as `mw_main_recomp.exe` during
  development.
