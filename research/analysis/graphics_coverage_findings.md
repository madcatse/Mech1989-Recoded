# Graphics coverage findings

Дата: 2026-06-25.

## Что обновлено

В `screenshots/` добавлен `MW1FinalPic.jpg`. Pipeline теперь учитывает
`*.png`, `*.jpg`, `*.jpeg` screenshots.

Обновленные/новые артефакты:

- `research/analysis/screenshots_inventory.csv`
- `research/analysis/screenshots_contact_sheet.png`
- `research/analysis/screenshot_fullscreen_matches.csv`
- `research/analysis/screenshot_pal_ega_fit.csv`
- `research/analysis/graphics_coverage_matches.csv`
- `research/analysis/graphics_coverage_summary.csv`
- `research/analysis/mw1_finalpic_coverage_matches.csv`
- `research/analysis/mw_pics_raw_candidates_full.csv`
- `research/analysis/mw_pics_raw_candidates_contact_sheet.png`

Новый инструмент:

```powershell
python .\tools\check_graphics_coverage.py
```

Он сравнивает screenshots со всеми текущими rendered extracted images,
нормализуя размер до 320x200 и квантуя цвета к ближайшей EGA palette. Это
triage-метрика, а не доказательство совпадения для composite screens.

## Общий статус покрытия

`graphics_coverage_summary.csv`:

- `strong`: 12 screenshots.
- `partial`: 11 screenshots.
- `weak_or_missing`: 12 screenshots.

Слабое покрытие ожидаемо для composite screens, combat screens и экранов, где
изображение собирается из нескольких слоев. Для них нужны специализированные
matchers, а не full-frame similarity.

## Финальный экран

Файл:

- `screenshots/MW1FinalPic.jpg`

Размер: `603x472`, JPEG. Это не тот же формат, что DOSBox PNG screenshots
`1600x1200`; для сравнения кадр приводится к 320x200 и nearest-EGA colors.

Финальный текст найден в `Original/MW_MAIN.EXE`:

- `0x00A737`: `AS OILY SMOKE BEGINS TO RISE FROM THE LAST`
- далее идут строки про `DARKWING`, `CHALICE OF HERNE`, `ANDER'S MOON`,
  `VANDENBURG NAME`.

Графика финального экрана среди текущих rendered extracted images не найдена.

Проверено:

- подтвержденные `MW_*PICS.BIN` archive entries;
- подтвержденные `MW_PICS.BIN` raw 320x200 records;
- полный raw scan по `MW_PICS.BIN` с 139 крупными candidates;
- BTECH `BMP`/`SCR`;
- FNT glyph sheets.

Лучший full-coverage candidate для `MW1FinalPic.jpg` остается слабым:

- `research/extract/rendered_pics_bin_png_display/MW_GPICS/entry_023_64x4.png`
- `rmse_nearest_ega=114.0073`
- `exact_pixel_coverage=0.232703`

Это не визуальное совпадение, а случайная близость по общим EGA-цветам.

## Новый raw PICS record

Полный scan `MW_PICS.BIN` нашел ранее не включенный 320x200 record:

- offset `0x000680D5`
- rendered as `research/extract/rendered_pics_raw_png_display/MW_PICS/raw_000680D5_320x200.png`

Это hangar/mech-complex scene, не финальная картинка. Manifest `MW_PICS` был
пересобран с пятью known raw fullscreen offsets:

- `0x00000280`
- `0x00007F89`
- `0x0000FC92`
- `0x0001F6A5`
- `0x000680D5`

## Вероятные следующие места поиска финальной графики

1. `MW_MAIN.EXE` data regions near final text and ending-state code.
2. `*PCK.TBL` / `TERPCK.GI` formats, which are not yet decoded as image
   containers.
3. Runtime composition using a picture block not currently recognized by
   `MW_*PICS.BIN` archive/raw parsers.

## Следующий шаг

Начать с `*PCK.TBL` / `TERPCK.GI`:

- определить header/offset/record structure;
- искать image-sized 4bpp payloads или compressed chunks;
- добавить map/extract/render probe;
- повторить `check_graphics_coverage.py` после новых renders.
