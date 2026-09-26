# BTECH combat screenshot findings

Дата: 2026-06-25.

## Новые боевые кадры

По contact sheet новые combat screenshots:

- `screenshots/image0028.png`
- `screenshots/image0029.png`
- `screenshots/image0030.png`
- `screenshots/image0031.png`
- `screenshots/image0032.png`
- `screenshots/image0033.png`

Все шесть кадров показывают боевой HUD игрока на легком мехе.

## Проверка SCR cockpit base

Новый инструмент:

```powershell
python .\tools\match_btech_combat_templates.py --screenshot image0028.png --screenshot image0029.png --screenshot image0030.png --screenshot image0031.png --screenshot image0032.png --screenshot image0033.png --top 5 --min-area 60000 --max-area 64000 --out .\research\analysis\btech_combat_scr_matches.csv
```

Результат:

- `LIGHT.SCR` - rank 1 для всех шести combat screenshots.
- `MEDIUM.SCR` - rank 2.
- `HEAVY.SCR` - rank 3.

Это подтверждает, что текущий `render-scr` правильно декодирует cockpit base
для легкого меха, а `LIGHT.SCR` является подходящим template для этих кадров.

Визуальная сверка:

- `research/analysis/btech_combat_light_scr_comparison.png`

## Малые BTECH-шаблоны

Coarse matcher:

```powershell
python .\tools\match_btech_combat_templates.py --screenshot image0028.png --screenshot image0029.png --screenshot image0030.png --screenshot image0031.png --screenshot image0032.png --screenshot image0033.png --top 20 --step 8 --max-area 5000 --out .\research\analysis\btech_combat_template_matches_small.csv
```

Наблюдения:

- На верхних местах стабильно появляются `DIGITS`, `HUD_CYAN`, `COCKPIT` и
  `SM_MECHS` resources.
- `SM_MECHS_*` candidates попадают в область внешнего viewport примерно около
  `x_320=240`, `y_320=12`, что соответствует enemy/mech marker в боевой части.
- `DIGITS_*` и `HUD_CYAN_*` candidates попадают в верхнюю шкалу/приборы и левый
  weapons panel.

Это не финальная координатная разметка: matcher использует masked RGB score и
coarse step, поэтому результаты нужно использовать как triage-кандидаты.

## Палитры

`screenshot_pal_ega_fit.csv` пересобран по 34 скриншотам. Боевые кадры, как и
предыдущие, используют стандартные EGA RGB colors. Это подтверждает EGA palette
set, но конкретный `PAL:EGA` bank по одним RGB-цветам всё еще не различается,
потому что несколько банков покрывают те же используемые цвета.

## Следующий шаг

1. Сделать специализированный extractor/render для боевого кадра:
   `LIGHT.SCR` base + terrain horizon/ground + mech sprites + HUD overlays.
2. Улучшить matcher: фиксированные зоны HUD, отдельная маска viewport, точный
   step 1 для малых шаблонов внутри ожидаемых областей.
3. Найти, откуда берется terrain outside viewport в combat screenshots:
   вероятно, это runtime drawing, а не готовый fullscreen SCR.
