# Graphics extractor status and plan

Дата оценки: 2026-06-24.

## Текущее состояние

Экстрактор перешёл из стадии ручных проб в воспроизводимый инструмент. Основная
реализация находится в `tools/mw_extract.py`; вспомогательные batch/reporting
скрипты лежат в `tools/`.

Подтверждено:

- Tagged resource-файлы распознаются по контейнерным блокам `BMP`, `PAL`,
  `FNT`, `SCR`, `SND` и вложенным chunk-записям.
- `INF` используется как источник размеров для части графических ресурсов.
- `BIN` codec 1 и codec 2 декодируются инструментом `decode-bin`.
- Codec 2 описан как LZW-подобный поток: LSB-first, стартовая ширина 9 бит,
  динамический рост до 12 бит, без обязательного clear/end-кода.
- Декодированные BTECH `BMP`/`SCR` payload'ы рендерятся как packed 4bpp
  high/low nibble pixels.
- MW_*PICS.BIN nibble-RLE потоки декодируются, мапятся, извлекаются и
  рендерятся для подтвержденных entry-записей.
- `PAL:EGA` структурно декодируется как tentative `ega_pair_banks`: четыре
  16-цветных банка по 32 байта.
- Для части raw/full-screen PICS-графики есть эвристический сканер и рендер.
- Вывод поддерживает PPM/PNG/BMP, масштабирование и display-подготовку.
- Скриншоты из DOSBox включены в сверку: есть contact sheets, inventory и
  template-matching отчеты.

Текущие визуальные результаты:

- `research/extract/rendered_btech_bmp_png_display/` - 131 PNG-превью.
- `research/extract/rendered_btech_scr_png_display/` - 5 PNG-превью.
- `research/extract/rendered_pics_bin_png_display/` - 92 PNG-превью.
- `research/extract/rendered_pics_raw_png_display/` - 4 PNG-превью.

Ключевые аналитические файлы:

- `research/analysis/codec2_lzw_findings.md`
- `research/analysis/graphics_bin_findings.md`
- `research/analysis/btech_loader_findings.md`
- `research/analysis/mw_pics_bin_format.md`
- `research/analysis/graphics_preview_pipeline.md`
- `research/analysis/template_matching_notes.md`
- `research/analysis/pal_ega_findings.md`
- `research/analysis/fnt_findings.md`
- `research/analysis/btech_combat_findings.md`
- `research/analysis/graphics_coverage_findings.md`
- `research/analysis/resource_catalog.csv`
- `research/analysis/resource_catalog_summary.md`
- `research/analysis/rendered_btech_preview_inventory.csv`
- `research/analysis/rendered_preview_inventory.csv`

## Полнота выполнения

Высокая готовность:

- Карта resource-контейнеров и tagged chunk'ов.
- Извлечение payload-диапазонов без встраивания оригинальных данных игры.
- Декодирование основных BTECH BIN-потоков, включая codec 2.
- Рендер BTECH packed 4bpp BMP/SCR в визуально проверяемые изображения.
- Декодирование и рендер значительной части MW_*PICS.BIN архивной графики.
- Описание `PAL:EGA`, экспорт банков в JSON/CSV и probe-рендеры с выбором
  palette bank.
- Единый `resource_catalog.csv` по текущим render manifest'ам.
- Декодирование и glyph-sheet render для raw/tagged `FNT`.
- Сопоставление новых combat screenshots с `LIGHT.SCR` и BTECH HUD/BMP
  candidates.
- Coverage-анализ по 35 screenshots, включая `MW1FinalPic.jpg`; финальная
  графика пока не найдена среди текущих extracted renders.
- Регрессионные тесты на CLI и декодеры.

Средняя готовность:

- Автоматическая классификация ресурсов по назначению.
- Сопоставление извлеченных картинок со скриншотами.
- Raw scan внутри `MW_PICS.BIN`: полезен для поиска, но еще эвристический.
- Batch-пайплайн превью: работает, но еще не является единым "build all"
  сценарием.

Низкая готовность / не закрыто:

- Точная DOS/EGA register-семантика `PAL:EGA`, формат `PAL:CGA` и привязка
  палитр к сценам.
- Привязка конкретных `FNT` к UI-экранам и runtime text renderer.
- Полная композиция combat viewport: terrain horizon/ground, runtime sprites,
  weapon/status overlays и точные coordinates.
- Финальная уникальная графика из `MW1FinalPic.jpg`.
- Понимание правил композиции экранов, спрайтов, оверлеев и UI.
- Стабильная схема resource catalog с человекочитаемыми именами.
- Полное описание структуры raw PICS-записей и возможных offset/index tables.

## Систематизация проекта

Рекомендуемая роль каталогов:

- `Original/` - исходные игровые файлы пользователя, не менять.
- `screenshots/` - визуальные reference-материалы из работающей игры.
- `graphics_converter/` - reference-инструмент и примеры конвертации.
- `tools/` - воспроизводимые инструменты анализа и извлечения.
- `tests/` - регрессионные тесты.
- `research/inventory/` - файл-каталог, хэши, базовая forensic-таблица.
- `research/triage/` - первичный технический осмотр файлов.
- `research/disassembly/` - внешние дизассемблерные выгрузки.
- `research/methodology/` - методики и проектные заметки.
- `research/analysis/` - компактные выводы и таблицы для принятия решений.
- `research/extract/` - пересоздаваемые извлечения, декоды и превью.
- `research/extract/_obsolete/` - архив старых временных результатов.

Устаревшие generated-папки перенесены в архив:

- `codec2_layout_probe/`
- `png_check/`
- `rendered_btech_bmp_check/`
- `rendered_btech_scr_check/`
- `decoded_btech_codec2/`

Причина: они заменены текущими renderer-папками, `btech_bin_samples/` и
аналитическими документами по codec 2.

## Следующий план работ

1. Final picture search: начать разбор `*PCK.TBL` / `TERPCK.GI`, так как
   финальная графика не найдена в текущих PICS/BTECH renders.
2. Combat renderer: собрать проверочный кадр из `LIGHT.SCR` base, найденных HUD
   resources и runtime/terrain candidates.
3. `PAL:CGA`: разобрать 162-байтный payload и понять, нужен ли он современной
   реконструкции или только forensic-каталогу.
4. Шрифты: добавить text-render helper и сопоставить `FNT` с UI-скриншотами.
5. Resource catalog: расширить `resource_catalog.csv` ручными semantic labels,
   screenshot match полями и confidence.
5. PICS raw: заменить эвристический scan на структурный разбор, если в EXE или
   дизассемблерных выгрузках подтвердятся offset/index tables.
6. Сцены и композиция: по скриншотам отметить, какие ресурсы являются фоном,
   портретами, cockpit/HUD, small mech sprites, меню и системными экранами.
7. Batch regeneration: добавить единый сценарий `build_all_graphics.py`, который
   пересоздает maps, decoded streams, PNG previews, inventories и contact sheets.
8. CLI-документация: зафиксировать стабильные команды `mw_extract` и схемы
   выходных CSV/JSON.
9. Тесты: расширить покрытие на `PAL:CGA`, FNT, PICS raw records и
   batch-пайплайн.

Ближайший практический пункт: подтвердить palette bank mapping по скриншотам, а
затем перейти к FNT.
