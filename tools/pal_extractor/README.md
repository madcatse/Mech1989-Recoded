# PAL Extractor

`pal_extract.py` экспортирует содержимое `.PAL` файлов MechWarrior из папки `Sorted Original Files/PAL`.

Файлы `.PAL` являются маленькими tagged-контейнерами:

```text
PAL:
  EGA: 128 bytes
  CGA: 162 bytes
```

## Структура PAL

Каждый файл начинается с родительского чанка `PAL:`. Внутри него находятся два payload-чанка:

- `EGA:` - таблицы для EGA-режима;
- `CGA:` - lookup-таблицы для CGA-режима.

Все четыре исходных файла имеют одинаковую контейнерную структуру:

- `PAL:` payload: 306 байт;
- `EGA:` payload: 128 байт;
- `CGA:` payload: 162 байта.

## EGA часть

`EGA:` payload состоит из 64 продублированных пар байтов. Это раскладывается на 4 банка по 16 цветов:

```text
4 banks * 16 colors * 2 duplicated bytes = 128 bytes
```

Для каждого цвета сохраняются:

- raw byte;
- low nibble;
- high nibble;
- RGB для low/high по стандартной 16-цветной EGA/RGBI таблице;
- признак дублирования пары.

## CGA часть

`CGA:` payload полностью раскладывается так:

```text
u16le control_word
5 tables * 16 entries * u16le cga_word
```

Итого:

```text
2 + 5 * 16 * 2 = 162 bytes
```

Каждый `cga_word` - это две CGA-байтовые строки. Один CGA-байт содержит четыре 2-битных пикселя в порядке от старших битов к младшим:

```text
bits 7..6, bits 5..4, bits 3..2, bits 1..0
```

Поэтому один `u16le cga_word` разворачивается в 8 CGA pixel indexes `0..3`.

Примеры:

```text
0x0000 -> 00000000
0x5555 -> 11111111
0xAAAA -> 22222222
0xFFFF -> 33333333
0xCCCC -> 30303030
0x3333 -> 03030303
```

`control_word` сохраняется отдельно. В имеющихся файлах он равен `0x0004` для `ARCTIC`, `DESERT`, `TROPIC` и `0x0003` для `DMGPAL`. Его младшая тетрада соответствует RGBI color index, который CGA использует как программируемый color 0/background в 320x200 4-color режиме; сами lookup-таблицы хранят не RGB, а 2-битные CGA pixel indexes.

## Быстрый запуск

Из корня проекта:

```powershell
python "Sorted Original Files\PAL\pal_extract\pal_extract.py" --force
```

По умолчанию инструмент читает все `.PAL` файлы из родительской папки:

```text
Sorted Original Files/PAL
```

и пишет результат сюда:

```text
Sorted Original Files/PAL/pal_extract/out
```

## Что создается

Для каждого файла создается отдельная папка:

```text
pal_extract/out/ARCTIC/
  chunks.csv
  ega_banks.csv
  cga_tables.csv
  manifest.json
  raw/*.bin
  swatches/*.bmp
```

Назначение файлов:

- `chunks.csv` - карта tagged-чанков: `PAL:`, `EGA:`, `CGA:`, смещения и размеры.
- `raw/*.bin` - payload каждого чанка без 8-байтного заголовка.
- `ega_banks.csv` - 4 EGA-банка, 16 индексов в каждом, raw byte, low/high nibble и RGB.
- `cga_tables.csv` - `control_word` и 5 CGA lookup-таблиц по 16 слов; каждое слово развернуто в 8 pixel indexes.
- `swatches/*_ega_*.bmp` - визуальные полоски цветов для EGA-банков в вариантах `low` и `high`.
- `swatches/*_cga_*.bmp` - визуализация CGA 2-битных lookup-паттернов. Цвета на этих BMP являются индексной подсветкой `0..3`, а не доказательством конкретного RGB на мониторе.
- `manifest.json` - машинно-читаемое описание всего экспорта.

В верхней папке результата также создается `summary.json`.

## Полезные команды

Экспортировать один файл:

```powershell
python "Sorted Original Files\PAL\pal_extract\pal_extract.py" "Sorted Original Files\PAL\DESERT.PAL" --force
```

Записать результат в отдельную папку:

```powershell
python "Sorted Original Files\PAL\pal_extract\pal_extract.py" --out "Sorted Original Files\PAL\pal_extract\out_test" --force
```

Создать PPM вместо BMP:

```powershell
python "Sorted Original Files\PAL\pal_extract\pal_extract.py" --image-format ppm --force
```

Создать BMP, PPM и PNG:

```powershell
python "Sorted Original Files\PAL\pal_extract\pal_extract.py" --image-format all --force
```

PNG требует установленный Pillow. BMP и PPM пишутся самим инструментом без внешних зависимостей.

Изменить масштаб CGA-пикселей в свотчах:

```powershell
python "Sorted Original Files\PAL\pal_extract\pal_extract.py" --cga-pixel-scale 4 --force
```

Пропустить создание визуальных свотчей:

```powershell
python "Sorted Original Files\PAL\pal_extract\pal_extract.py" --no-swatches --force
```

## Проверенные наблюдения

- `ARCTIC.PAL` и `DMGPAL.PAL` совпадают по EGA-банкам, но различаются по CGA payload.
- В `CGA:` все 80 lookup-слов являются валидными 2-битными CGA-паттернами.
- Различия terrain-палитр находятся в EGA bank 1/3 и в CGA lookup tables 1/3/4.
- Таблицы `0` и `2` у `ARCTIC`, `DESERT`, `TROPIC` совпадают; `DMGPAL` использует отдельный CGA-набор.
