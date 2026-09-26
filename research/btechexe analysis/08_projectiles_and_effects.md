# 8. Снаряды и визуальные эффекты

## Доказанный runtime-контракт

`FUN_1000_5654` выделяет один из 32 projectile records. В записи хранятся как
минимум предыдущая/текущая позиция, направление, raw speed, оставшееся время,
тип projectile, владелец, а для наведённого выстрела — цель и сохранённая
target-local точка удара.

Таблица weapon → projectile в `DS:0x232E`:

- `AC/5 -> 0`;
- `LRM5 -> 1`;
- `SRM2 -> 2`;
- `SRM4 -> 3`;
- `SRM6 -> 4`;
- laser, PPC и machine gun: `-1`, урон доставляется немедленно.

Пять строк projectile definition в `DS:0x0E6E` дают соответственно
`speed/lifetime/damageClass/visualClass`:

- AC/5: `60/57/5/0`;
- LRM5: `40/67/6/1`;
- SRM2: `50/28/7/1`;
- SRM4: `50/28/8/1`;
- SRM6: `50/28/9/1`.

`FUN_1000_578b` обновляет target-bound projectile до пяти movement substeps за
original battle update. Текущая мировая точка вычисляется из сохранённой
target-local точки и актуального transform цели; поворот ограничен `0x38E`
binary-angle units за substep. Проверки «только для ракет» нет: наведённый AC/5
проходит ту же функцию. Незавязанный miss продолжает полёт прямо.

## Урон при прилёте

`FUN_1000_5d76` выполняется на impact, а не на launch. AC/5 наносит фиксированные
5 единиц. LRM/SRM делают индексированный 2d6 cluster roll:

- LRM5: `1,2,2,3,3,3,3,4,4,5,5`, по 1 damage;
- SRM2: `1,1,1,1,1,1,2,2,2,2,2`, по 2 damage;
- SRM4: `1,2,2,2,2,3,3,3,3,4,4`, по 2 damage;
- SRM6: `2,2,3,3,4,4,4,5,5,6,6`, по 2 damage.

После этого вызывается один single-target damage path. Цикла по соседним мехам,
radius test или второго damage call в найденном пути нет; evidence для splash
damage отсутствует.

PPC остаётся немедленным оружием. Его `FUN_1000_bc5e` создаёт временную
beam/bolt-геометрию, но не переносит момент authoritative damage.

## Пока не закрыто

- точный wall-clock одного original battle update и масштаб raw speed к world
  units;
- последовательность глобального original PRNG;
- terrain/obstacle swept collision и точные miss/ground-impact правила;
- launch hardpoints, visual-resource ids, trail/impact animation timing;
- gravity и friendly-fire/occlusion policy, если они существуют в других
  вызывающих путях.

Поэтому replacement может владеть deterministic projectile lifecycle уже
сейчас, но обязан маркировать scale/PRNG как compatibility policy и не добавлять
splash, gravity или современную BattleTech-баллистику без нового evidence.

## DOS capture audit 2026-08-31

The controlled captures and notes in
`screenshots/weapon_effects_screenshots/answers.txt` close the two projectile
visual classes. AC/5 visual class `0` is `OTHPCK.TBL` record `000`, a rotating
red solid with a yellow outline. LRM5 and every SRM visual class `1` use the
same `OTHPCK.TBL` record `001`; one launcher activation creates one visible
rocket object regardless of rack size. The decoded records contain 32/248 GPU
vertices and 6/64 triangles respectively. Both objects appear on the firing
update, rotate around the flight axis, retain one model until termination and
can coexist with other projectiles and effects.

The captures also corroborate the recovered target binding: changing the
crosshair or scanner target after launch does not redirect a projectile, while
both missiles and AC/5 steer toward the target captured at launch. A miss
continues straight, and no ballistic drop was observed. Terrain can intercept
AC/missile flight and prevents damage to the intended target. The exact swept
collision algorithm is still not decoded.

Additional empirical evidence, not yet closed to a decompile branch, shows
that an in-flight missile can be destroyed by laser or AC fire, producing an
airburst and no target damage. This is an authoritative gameplay requirement,
not merely a presentation effect, but the collision threshold and eligible
interceptor weapon set remain unresolved; no projectile hit points are to be
invented.

Impact presentation has six observed consecutive states: white flash, yellow
shape, red/pink shape, then three shrinking grey/brown smoke shapes. Laser and
PPC are immediate one-update filled wedges, yellow and cyan respectively.
Machine-gun cockpit flashes use `COCKPIT.BMP` records `006..011`, arranged as
left/right triples. Exact impact resource records, original wall-clock stage
duration, numeric launch hardpoints, axial spin rate and non-target mech
collision remain open.

Replacement beta alignment uses the decoded rear bounds as launch-origin
offsets: `45` units for record `000` and `405` for record `001`. This places the
model rear at the existing aim origin and is deterministic, but it is not proof
of the original per-mech hardpoint transform. A close target caps the offset
before the target with one raw-speed substep remaining. Axial rotation is currently
tuned to `-22.5 degrees` per replacement fixed tick; only clockwise rotation,
not that rate, is original evidence.

## Impact type-1 display correction — 2026-09-02

The six impact records are now mapped to `OTHPCK.TBL:002..007`. Their type-1
commands contain respectively one, then five groups of three radius
primitives. Within each complete record the second shade byte is stable and
forms the observed flat sequence `15,14,12,8,8,8`; treating the first byte as
directional lighting incorrectly mixes neighbouring phase colours. The exact
DOS type-1 rasterizer is still unresolved, so flat use of the stable byte is a
compatibility interpretation.

Every one of these records has header `scaleShift=0`. The replacement applies
an explicitly provisional presentation factor `0.5` to centres and radii based
on controlled screen-size comparison; no original numeric scale is claimed.
The factor is world-space, so perspective—not a screen-space override—continues
to determine apparent size with distance.

## Machine-gun temporary effect class — 2026-09-02

`FUN_1000_aae0` distinguishes machine gun weapon index `3` from the other
immediate weapons. It skips the beam call, applies damage through
`FUN_1000_4ece`, then calls `FUN_1000_5f0e` with class argument `1`; other
immediate contacts use class `0`. `FUN_1000_08d8` advances the class sequence
once per battle update and frees the temporary object at value `0x17`.

The initialized BTECH data at file offsets `0x278E4..0x278F7` contains the
terminal lists `2,3,4,5,6,7,0x17` and `8,9,0x17`. The latter maps the MG hit to
OTHPCK records `008` then `009`. Record 8 contains five two-index yellow
strokes; record 9 contains five three-index filled yellow triangles. There is
no third record in one MG hit effect.
