# FNT findings

Дата: 2026-06-25.

## Проверенные файлы

- `Original/6X6.FNT` - raw FNT payload, 580 bytes.
- `Original/6X6B.FNT` - tagged `FNT:` chunk, payload 580 bytes.
- `Original/8X8B.FNT` - tagged `FNT:` chunk, payload 772 bytes.
- `Original/FOX88.FNT` - raw FNT payload, 772 bytes.

## Подтвержденная структура

Payload начинается с 4-байтного header:

```text
u8 width
u8 height
u8 first_code
u8 glyph_count
```

Дальше идут glyph rows:

```text
glyph_count * height bytes
```

Для текущих файлов:

- `6X6*.FNT`: `width=6`, `height=6`, `first_code=0x20`, `glyph_count=0x60`.
- `8X8*.FNT` / `FOX88.FNT`: `width=8`, `height=8`, `first_code=0x20`, `glyph_count=0x60`.

Каждая строка glyph хранится в одном байте, старшие биты идут слева направо.
Это подтверждено визуальным рендером ASCII glyph sheet.

## Добавлено в mw_extract

Новая команда:

```powershell
python .\tools\mw_extract.py render-fnt .\Original\8X8B.FNT --out .\research\extract\rendered_fnt_png_display\8X8B --image-format png --glyph-scale 6
```

`scan` для tagged `FNT:` теперь добавляет поля:

- `fnt_layout`
- `fnt_width`
- `fnt_height`
- `fnt_first_code`
- `fnt_glyph_count`
- `fnt_last_code`

## Сгенерированные артефакты

- `research/extract/rendered_fnt_png_display/`
- `research/analysis/rendered_fnt_contact_sheet.png`
- `research/analysis/resource_catalog.csv`

## Следующий шаг

Связать FNT-файлы с UI-экранами:

- найти, какой EXE/экран использует `6X6`, `6X6B`, `8X8B`, `FOX88`;
- добавить text-render helper для проверки строк интерфейса;
- сравнить glyph metrics с DOSBox screenshots, особенно терминалы и меню.
