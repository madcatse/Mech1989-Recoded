# MW_*PICS.BIN manual image viewer v3.1

Одностраничный HTML‑viewer для ручного просмотра и разметки decoded‑потоков MechWarrior `MW_*PICS.BIN`.

## Что нового в v3.1

- Исправлена ошибка `TypeError: can't access property "innerHTML" ... null` при загрузке другого файла: восстановлен блок `Текущая картинка` и безопасное обновление DOM.
- Добавлен режим `Auto: по имени/таблице`, чтобы `.decoded.bin` не распаковывался ошибочно как nibble-RLE.
- Можно выбрать другой `MW_*PICS.BIN` прямо в окне viewer-а.
- Viewer умеет сам распаковывать nibble‑RLE `MW_*PICS.BIN`:
  - literal nibble: любое значение кроме `F`;
  - run token: `F, count_minus_one, value`;
  - исходные nibbles читаются low/high, decoded bytes собираются high/low.
- Можно загрузить уже распакованный `*.decoded.bin`, выбрав режим `Уже decoded/raw stream`.
- Для table-based `MW_*PICS.BIN` viewer пытается автоматически распознать таблицу `offsets + u16 sizes` и заполнить список `Known records`.
- Для `MW_PICS.BIN` по умолчанию встроена последняя проверенная карта из 67 изображений.
- Разметка хранится отдельно для каждого источника данных в `localStorage`; JSON также сохраняет `source_name`, `source_kind`, `source_signature` и `decoded_size`.
- Рендер нечётных ширин исправлен: строки считаются byte-aligned, padding-nibble конца строки не становится первым пикселем следующей строки.

## Как пользоваться

1. Открой `mw_pics_manual_viewer.html` в браузере.
2. Для встроенного `MW_PICS.BIN` можно сразу работать с картой known/unknown.
3. Чтобы изучить другой файл:
   - нажми `Загрузить другой файл`;
   - оставь `Auto: по имени/таблице` для обычного случая;
   - выбери `MW_*PICS.BIN: nibble-RLE compressed`, если точно загружаешь оригинальный BIN;
   - выбери `Уже decoded/raw stream`, если точно загружаешь уже распакованный поток;
   - нажми `Загрузить выбранный`.
4. Если таблица ресурсов распознана, `Known records` заполнится автоматически.
5. Если таблица не распознана, viewer покажет весь decoded-поток как unknown range; дальше работай вручную через offset/width/height/phase.
6. Используй `Mark image` или `Mark covered/non-image` для разметки.
7. Сохраняй JSON кнопкой `Скачать JSON`.

## Формат preview

Viewer рендерит подтверждённый EGA bitmap-формат:

```text
headered image:
  u16le width
  u16le height
  packed 4bpp pixels, high nibble first, byte-aligned scanlines

raw image:
  packed 4bpp pixels only
```

`nibble phase` полезен для manual-записей, где видимая картинка начинается со второго nibble первого байта.

## Ограничения

Viewer предназначен для plain packed 4bpp картинок. Если участок является tilemap, command stream, planar EGA, маской или ресурсом другого видеорежима, он может не выглядеть как осмысленное изображение.
