# BattleMech economy research

This note captures the current working knowledge for the `MW_MAIN.EXE`
campaign economy, especially buying and selling BattleMechs. It is meant to be
used before implementing the Mech Lab `BUY MECHS` / `SELL` flow.

Canonical project sources:

- `Original/MW_MAIN.EXE` for original UI strings, planet records, and economy
  code/tables.
- `docs/STARMAP_INTEGRATION.md` for the current planet-record layout.
- `docs/MECH_REPAIR_AND_STATUS_RESEARCH.md` for Mech Lab, repair, reload, and
  status-screen research.
- The user-provided BattleTechWiki economy summary and price-table screenshot.

## Confirmed / high-confidence wiki behavior

MW1 uses four economy tiers for planets. The tier affects:

- BattleMech buy prices.
- BattleMech sell prices.
- Ammo reload / purchase prices.
- Repairs.
- Travel costs and/or related planetary economy costs may be nearby in the
  original economy system, but the current recomp travel-cost implementation is
  separately traced in `STARMAP_INTEGRATION.md`.

Tier behavior:

- Tier 1 planets have the cheapest Mechs, ammo, and repairs.
- Tier 1 planets also tend to have more spare parts, more Mechs for sale, and
  more missions.
- Tier 4 planets have the highest prices.
- Buying Mechs on tier 1 or tier 2 planets and selling on tier 3 or tier 4
  planets is a viable in-game trading strategy.
- Tier 1 to tier 4 resale gives about 71.4% profit per Mech.
- The capital planets of each region are tier 1.
- `ANDER'S MOON` is tier 4, but visiting it before finishing the game causes an
  immediate loss, so trading there is only possible after the game is completed.

Ammo note:

- Ammo is not included in Mech buy/sell value.
- A fully repaired Jenner with full SRM-4 ammo sells for the same price as a
  top-condition Jenner with no ammo.
- Ammo/reload costs increase from tier 1 to tier 4 by about 50% overall and can
  vary slightly at random between planets within the same tier.

## Trading rules

The player can own at most 12 Mechs.

Sell acceptance by planet tier:

| planet tier | sell limit per visit |
| --- | ---: |
| 1 | up to all owned Mechs |
| 2 | up to all owned Mechs |
| 3 | random 2 to 4 Mechs |
| 4 | random 1 to 3 Mechs |

After the local sell limit is reached, the original game displays:

```text
WE DON'T NEED ANY MORE MECHS
AT THIS TIME, THANKS.
```

Leaving the planet and returning resets this limit to a new random number.

Mechs available for purchase:

- Which Mechs are available on a planet is random.
- Any planet can sell any Mech.
- Tier 1 and tier 2 planets tend to have more Mechs available than tier 3 and
  tier 4 planets.
- Smaller Mechs are much more common than heavier Mechs.
- A tier 1 planet often offers mostly Locusts and Jenners, with a few Phoenix
  Hawks and Shadow Hawks.
- It is possible for any planet, including tier 1 planets, to have zero Mechs
  for sale. The original string is:

```text
SORRY, NO MECHS FOR SALE!
TRY BACK NEXT WEEK.
```

Buy-vs-sell spread on fully repaired Mechs:

| planet tier | buy markup over sell |
| --- | ---: |
| 1 | about 11.1% |
| 2 | about 9.1% |
| 3 | about 8.3% |
| 4 | about 7.1% |

## BattleMech prices

Prices are for fully repaired, top-condition Mechs. Actual cost to buy and then
fully repair a damaged Mech may differ by a few hundred C-bills at most.

| Tier / transaction | Locust | Jenner | Phoenix Hawk | Shadow Hawk | Rifleman | Warhammer | Marauder | BattleMaster |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 - Buy price | 1,504,000 | 3,183,000 | 4,022,000 | 4,622,000 | 5,500,000 | 6,021,000 | 6,729,000 | 8,410,000 |
| 1 - Sell price | 1,353,000 | 2,864,000 | 3,619,000 | 4,159,000 | 4,950,000 | 5,418,000 | 6,056,000 | 7,569,000 |
| 2 - Buy price | 1,804,000 | 3,819,000 | 4,826,000 | 5,546,000 | 6,600,000 | 7,225,000 | 8,074,000 | 10,092,000 |
| 2 - Sell price | 1,654,000 | 3,501,000 | 4,424,000 | 5,084,000 | 6,050,000 | 6,623,000 | 7,401,000 | 9,251,000 |
| 3 - Buy price | 1,955,000 | 4,137,000 | 5,228,000 | 6,008,000 | 7,150,000 | 7,827,000 | 8,747,000 | 10,933,000 |
| 3 - Sell price | 1,804,000 | 3,819,000 | 4,826,000 | 5,546,000 | 6,600,000 | 7,225,000 | 8,074,000 | 10,092,000 |
| 4 - Buy price | 2,256,000 | 4,774,000 | 6,033,000 | 6,933,000 | 8,250,000 | 9,031,000 | 10,093,000 | 12,615,000 |
| 4 - Sell price | 2,105,000 | 4,456,000 | 5,630,000 | 6,470,000 | 7,700,000 | 8,429,000 | 9,420,000 | 11,774,000 |

The runtime economy now exposes all eight normal playable chassis:

```text
Locust, Jenner, Phoenix Hawk, Shadow Hawk, Rifleman, Warhammer, Marauder,
BattleMaster
```

Wasp and Wolverine are known campaign-side/internal strings and table entries
from `MECH_REPAIR_AND_STATUS_RESEARCH.md`, but should not be exposed as normal
buyable/sellable Mechs unless later research confirms their intended playable
status.

## Original strings already found in MW_MAIN.EXE

Useful Mech economy UI strings:

| string | file offset | note |
| --- | ---: | --- |
| `BUY MECHS` | `0x00B319` | Mech Lab menu item |
| `MECHS FOR SALE` | `0x00B622` | buy-list title |
| `WE DON'T NEED ANY MORE MECHS / AT THIS TIME, THANKS.` | near `0x00B4D0` | sell-limit refusal message |
| `I AM PREPARED TO / OFFER YOU THE SUM OF / C-BILLS / FOR YOUR MECH.` | near `0x00BEF0` | sell-offer dialog |
| `ACCEPT` | near sell-offer block | sell-offer button |
| `REJECT` | near sell-offer block | sell-offer button |
| `SORRY, NO MECHS FOR SALE! / TRY BACK NEXT WEEK.` | `0x00BEBB` | empty market message |
| `YOU CAN'T AFFORD THIS!` | `0x00D030` | buy failure |
| `THE LAW ALLOWS A CITIZEN TO / OWN NO MORE THEN 12 MECHS!` | `0x00D047` | max-owned failure; original typo is `THEN` |

The Mech status screen already has a `SELL` row in the current rebuilt UI.

## Tier and planet-record byte 3

Current planet records from `Original/MW_MAIN.EXE` include `unknown_byte_3`, also
called `byte_3` in earlier notes. See `docs/STARMAP_INTEGRATION.md`.

## Wiki tier-map extraction

The user-provided wiki tier-map image was matched against the current
`MW_MAIN.EXE` planet coordinates. The image uses the same starmap coordinates
with this exact scale/offset:

```text
wiki_image_x = planet.map_x * 5 - 33
wiki_image_y = planet.map_y * 5 - 213
```

Extracted data:

```text
research/analysis/planet_tiers_from_wiki_image.csv
```

The CSV contains all `145` unique planets from
`research/analysis/mw_main_planet_table.csv`, with no missing planet
coordinates. The tier distribution extracted from the image is:

| wiki tier | planet count |
| ---: | ---: |
| 1 | 44 |
| 2 | 50 |
| 3 | 21 |
| 4 | 30 |

Distribution by House:

| House | tier 1 | tier 2 | tier 3 | tier 4 | total |
| --- | ---: | ---: | ---: | ---: | ---: |
| Kurita | 7 | 9 | 4 | 10 | 30 |
| Steiner | 7 | 11 | 5 | 7 | 30 |
| Marik | 10 | 13 | 3 | 4 | 30 |
| Liao | 11 | 9 | 4 | 1 | 25 |
| Davion | 9 | 8 | 5 | 8 | 30 |

Known checks from the image extraction:

| planet | extracted wiki tier | unknown_byte_3 |
| --- | ---: | ---: |
| LUTHIEN | 1 | 0 |
| THARKAD | 1 | 0 |
| ATREUS | 1 | 0 |
| SIAN | 1 | 0 |
| NEW AVALON | 1 | 0 |
| GALAX | 1 | 3 |
| ANDER'S MOON | 4 | 0 |
| BAXLEY | 4 | 0 |
| NOATAK | 4 | 1 |

This confirms that the wiki image covers all planets represented by the current
145-record unique planet table. The extraction uses nearest tier-color pixels
around the scaled EXE coordinate. `NANKING` is the only close-neighbor case
where another tier color appears in the sampling window; the nearest color at
the planet center is still tier 1.

Old observation:

```text
unknown_byte_3 values in the 145 unique planet records: 0..4
```

Distribution in `research/analysis/mw_main_planet_table.csv`:

| unknown_byte_3 | planet count |
| ---: | ---: |
| 0 | 9 |
| 1 | 19 |
| 2 | 10 |
| 3 | 37 |
| 4 | 70 |

Known examples checked against the planet table:

| planet | wiki/economy expectation | unknown_byte_3 | note |
| --- | ---: | ---: | --- |
| LUTHIEN | tier 1 | 0 | capital planet |
| THARKAD | tier 1 | 0 | capital planet |
| ATREUS | tier 1 | 0 | capital planet |
| SIAN | tier 1 | 0 | capital planet |
| NEW AVALON | tier 1 | 0 | capital planet |
| GALAX | tier 1 according to trading example | 3 | Davion tier 1 trading route example |
| ANDER'S MOON | tier 4 | 0 | home planet, pre-ending loss condition |
| BAXLEY | tier 4 according to trading example | 0 | Davion tier 4 trading route example |
| NOATAK | tier 4 according to trading example | 1 | Davion tier 4 trading route example |

Conclusion:

- `unknown_byte_3` is very unlikely to be the economy tier in direct form.
- It has five values, not four.
- Even if value `0` marks many capitals, it also appears on `ANDER'S MOON` and
  `BAXLEY`, which should behave as tier 4 worlds.
- `GALAX` being a tier 1 trading-route example with `unknown_byte_3 = 3`
  further argues against direct mapping.

Open hypothesis:

- `unknown_byte_3` may still participate in economy generation as a scarcity,
  market, contract, or regional weight.
- The actual tier may be computed from a combination of fields, stored in
  another table, or derived by code paths near the Mech market generation.

## Local reverse-engineering leads

The decompiled `MW_MAIN.EXE` has candidate Mech market generation around:

```text
FUN_101b_52ee
FUN_101b_4f6e
```

Observed behavior in that region:

- `FUN_101b_52ee` appears to initialize the Mech-for-sale pool.
- It writes a count-like value at `DS:0948`.
- It calls `FUN_101b_4f6e` once per candidate market entry.
- It changes behavior by campaign date, with thresholds around years `3028`
  and `3029`.
- `FUN_101b_4f6e` writes per-market-entry fields around:

```text
DS:094A
DS:0956
DS:0962
DS:096E
DS:097A
DS:0986
DS:0992
DS:099E
DS:09C2
DS:0A2B
```

Likely candidates from context:

- Chassis id / selected Mech type.
- Damage/condition fields for market Mechs.
- Buy price.
- Sell/offer price.
- Available count or availability class.

These names are not confirmed yet. Before implementing tier-perfect economy,
trace these functions against the original strings and the data around the
Mech buy/status/sell screens.

## Implementation guidance for the next pass

Implemented status:

- The first sale pass implements the `MECH STATUS -> SELL` offer dialog shown in
  `screenshots/image0283.png` and `screenshots/image0284.png`.
- `ACCEPT` adds the offer price to player wealth, removes the Mech from owned
  inventory, clears any pilot assignment by removing the Mech itself, stores the
  sold Mech in the current planet's local market inventory, and returns to
  `MECH COMPLEX`.
- `REJECT` returns to `MECH STATUS`.
- The sale path supports all eight playable chassis. Sell value uses the
  current planet tier and ignores ammo, matching the documented original-game
  behavior.
- The buy pass implements `MECH COMPLEX -> BUY MECHS` for the eight playable
  chassis: Locust, Jenner, Phoenix Hawk, Shadow Hawk, Rifleman, Warhammer,
  Marauder, and BattleMaster. Each planet lazily generates a tier-weighted local
  assortment, sold Mechs are appended to the same planet market, `BUY` checks
  wealth and the 12-Mech ownership limit, then moves the purchased Mech into the
  player inventory.
- The `MECHS FOR SALE` list can display up to 12 entries and now sizes the
  overlay window to the number of visible market rows. Empty markets use a small
  dialog with the original two-line refusal text instead of a full-height blank
  panel.
- The buy-status screen uses the Mech Status layout with `PRICE`, `WEALTH`, and
  the active options `BUY`, `DAMAGE LEVELS`, and `DONE`. `DAMAGE LEVELS` is a
  read-only component/weapon/armor view.
- Reloadable ammo capacities are temporarily set to 25 packs for every ammo type
  on every Mech until original-game testing confirms the per-chassis values.

Done in the current bridge implementation:

1. Store the full eight-chassis price table.
2. Support buying and selling all eight playable chassis.
3. Keep ammo out of Mech buy/sell value.
4. Enforce the 12-Mech ownership limit.
5. Return sold Mechs to the current planet market at that planet's buy price.

Still pending:

1. Enforce original per-visit sell limits by tier.
2. Replace the temporary tier-weighted market generator with the traced
   original market generation once `FUN_101b_52ee` / `FUN_101b_4f6e` are fully
   understood.
3. Replace temporary 25-pack ammo capacities with original per-chassis ammo
   values.

Do not do yet:

- Do not treat `unknown_byte_3` as confirmed tier.
- Do not hard-code a final planet-tier table unless it is clearly marked as a
  temporary research bridge.
- Do not expose Wasp or Wolverine as normal market Mechs.

Best next research steps:

1. Trace reads/writes of `DS:0948..0A2B` in `FUN_101b_52ee` and
   `FUN_101b_4f6e`.
2. Find where current planet data is read when generating the Mech market.
3. Search for formulas that reproduce the price table values, likely as scaled
   values rather than literal C-bill dwords.
4. Use controlled original-game saves/screenshots on known route planets:
   `NEW AVALON`, `GALAX`, `NOATAK`, `BAXLEY`, and `ANDER'S MOON` after the
   ending.
