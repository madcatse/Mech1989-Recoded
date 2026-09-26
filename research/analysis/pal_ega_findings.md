# PAL:EGA findings

Дата: 2026-06-24.

## Что проверено

Файлы:

- `Original/ARCTIC.PAL`
- `Original/DESERT.PAL`
- `Original/DMGPAL.PAL`
- `Original/TROPIC.PAL`

Все четыре файла имеют одинаковую tagged-структуру:

- `PAL:` container, nested flag set, payload 306 bytes.
- `EGA:` payload 128 bytes.
- `CGA:` payload 162 bytes.

## Текущая гипотеза по EGA

`EGA:` payload структурно раскладывается как 64 пары байтов. Во всех четырех
файлах каждая пара дублированная: `64/64` pairs have identical bytes.

Это дает аккуратное разбиение на четыре 16-цветных банка:

- bank 0: bytes 0..31
- bank 1: bytes 32..63
- bank 2: bytes 64..95
- bank 3: bytes 96..127

Каждый цвет сейчас декодируется в режиме `ega_pair_banks_tentative`:

- сохраняется raw byte;
- отдельно показываются low nibble и high nibble;
- для preview можно выбрать `--palette-interpretation low|high`.

Важно: это подтвержденная структурная разметка, но не окончательное описание
DOS/EGA palette-register semantics. Экстрактор намеренно помечает формат как
tentative.

## Добавлено в mw_extract

Новая команда:

```powershell
python .\tools\mw_extract.py describe-pal .\Original\DESERT.PAL --json out.json --csv out.csv --swatches swatches --image-format png
```

Новые параметры для `render-bmp`, `render-scr`, `render-pics-bin`:

```powershell
--palette .\Original\DESERT.PAL
--palette-bank 0
--palette-interpretation low
```

По умолчанию рендеры по-прежнему используют стандартную EGA palette, так что
старые команды не меняют поведение.

## Сгенерированные артефакты

- `research/extract/palettes/*.palette.json`
- `research/extract/palettes/*.palette.csv`
- `research/extract/palettes/swatches/*.png`
- `research/analysis/pal_ega_banks.csv`
- `research/analysis/screenshot_pal_ega_fit.csv`
- `research/extract/palette_render_probe/`
- `research/analysis/palette_render_probe_contact_sheet.png`

Probe-рендеры используют `Original/BAT.BMP`, отрисованный через все 4 банка
каждой из 4 PAL-палитр.

## Наблюдения

- Bank 0 и bank 2 у всех четырех terrain PAL начинаются как почти стандартная
  EGA-index таблица, но index 3 содержит `0x00`, а index 5 содержит `0x33`.
- Bank 1 и bank 3 различаются между terrain PAL и выглядят как кандидаты на
  terrain-specific remap.
- `ARCTIC.PAL` и `DMGPAL.PAL` совпадают по EGA-банкам; различие между ними,
  если оно есть, нужно искать в `CGA:` или в runtime-логике выбора палитры.
- Все текущие DOSBox screenshots после downscale до 320x200 используют только
  стандартные EGA RGB colors. Это подтверждает, что текущий набор скриншотов не
  содержит нестандартных DAC-цветов, но не доказывает конкретный terrain bank.

## Воспроизводимые команды

```powershell
python .\tools\analyze_palette_screenshot_fit.py
python .\tools\build_resource_catalog.py
```

## Следующий шаг

Сопоставить palette-bank варианты с DOSBox-скриншотами боевого экрана:

1. добавить или выбрать чистые combat screenshots с desert/arctic/tropic
   terrain;
2. рендерить один и тот же набор combat resources через banks 0..3;
3. сравнить не только цветовую похожесть, но и роль ресурсов на экране;
4. после подтверждения зафиксировать mapping terrain -> PAL file -> bank.
