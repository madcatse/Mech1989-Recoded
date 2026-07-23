# Text material storage findings

Задача: найти, где в оригинальных файлах MechWarrior 1989 лежат игровые тексты из BattleTechWiki PDF: NewsNet, Main Story, Personal Messages, Rumors и прочие текстовые материалы.

Оригинальные файлы в `Original/` не изменялись. Рабочие извлечения и таблицы лежат только в `research/analysis/`.

## Главный вывод

Основной корпус сюжетного текста, слухов, персональных сообщений и NewsNet хранится прямо в:

- `Original/MW_MAIN.EXE`

Формат хранения: обычный встроенный ASCII-текст внутри DOS MZ executable. Тексты не выглядят как внешний `.DAT/.TBL/.BIN` ресурс и не выглядят сжатыми. Внутри блоков:

- строки разделяются байтом `0x0D` (`CR`);
- пустые строки часто представлены как `0x0D 0x0D`;
- записи обычно заканчиваются `0x00`, часто `0x00 0x00`;
- перед некоторыми записями встречаются 1-2 служебных байта или символы-маркеры (`-`, `/`, `0`, tab), которые лучше считать метаданными/артефактами границы, а не частью чистого текста;
- для дизассемблера: у `MW_MAIN.EXE` MZ-header равен `0x400`, поэтому file offset `0x01EAD2` соответствует смещению loaded image `0x01E6D2`.

Контрольные фразы из PDF находятся только в `MW_MAIN.EXE`: `ANDER'S MOON ECONOMY REELING`, `PERSONAL MESSAGE FROM: JORDAN ROWE`, `BRIEF HEADLINES`, `DARK WING BASE`, `HEIR APPARENT ON THE RUN`, `MCBRIN SUCCESSFUL! CHALICE RETURNED`.

## Подтвержденные области в MW_MAIN.EXE

| Область | Примерный file offset | Содержимое |
| --- | ---: | --- |
| Pilot bios / hiring blurbs | `0x012001`-`0x012938` | имена и реплики нанимаемых пилотов |
| UI/save/hire text | `0x012939`-`0x012E3F` | save/restore, hire limits, disk errors |
| Rumors / early story leads | `0x012F40`-`0x013841` | bartender rumors about skull-and-wings, Grig Griez |
| Main Story scenes | `0x013842`-`0x0183C9` | Grig, Dustball, Stone Arrow, Kearney, Tasha, Operation Inroad |
| Personal Messages / Matabushi files | `0x0183CA`-`0x018C4D` | Tasha message, Matabushi memo chain, Jordan Rowe message |
| NewsNet / story-adjacent articles | `0x018C4E`-`0x01C9C6` | gangland/news article, Galahad/Thor, Gray Death, Wolf's Dragoons, Heir Apparent |
| Late Grig / main story branch | `0x01C9C7`-`0x01DBD7` | mule/landing-site scenes and branches |
| Reputation/location flavor | `0x01DC5F`-`0x01E040` | "Blazing Aces" faction reputation lines |
| NewsNet articles | `0x01E041`-`0x01F82D` | Ander's Moon economy/politics, Matabushi image, editorials |
| Brief Headlines | `0x01F82E`-`0x020514` | short dated Inner Sphere headlines |
| Endgame NewsNet | `0x020515`-`0x020AAA` | new Duke, chalice returned, McBrin victory |
| Endgame / Dark Wing prompts | `0x020AAB`-`0x020E9F` | Dark Wing base attack/delay/death/continue prompts |

Полная таблица длинных блоков: `research/analysis/text_material_mw_main_blocks.csv`.

## Другие файлы

- `Original/BTECH.EXE` содержит текст боевого модуля: названия миссий, HUD/UI, `YOUR NEXT MISSION:`. Пример: mission-name table начинается около `0x027463` и хранится как `0x00`-terminated ASCII strings.
- `Original/SNARIO.DAT` не содержит обычных printable ASCII strings.
- `Original/MW_PICS.BIN`, `MW_1PICS.BIN`, `MW_2PICS.BIN`, `MW_CPICS.BIN`, `MW_TPICS.BIN` дают много случайных printable runs, но контрольные фразы NewsNet/Main Story там не найдены; это похоже на графические данные.
- `.GAM` файлы содержат сохранения/состояние и отдельные имена, но не корпус сюжетных сообщений.
- `.TBL/.BMP/.SCR/.PAL/.FNT/.MUS/.SND` по контрольным фразам не являются источником NewsNet/Main Story/Personal/Rumors.

## Созданные рабочие файлы

- `research/analysis/text_pdf_extract/*.txt` - локально извлеченный текст из PDF-подсказок для сравнения.
- `research/analysis/text_material_mw_main_blocks.csv` - длинные ASCII-блоки из `MW_MAIN.EXE`, включая смещения и грубые совпадения с PDF-разделами.

## Следующий технический шаг

Чтобы сделать полноценный extractor, нужно разобрать таблицы ссылок/индексов вокруг этих текстовых областей. По байтам видно, что тексты уже лежат готовыми ASCII-блоками, но логика выбора сообщения, дат и условий показа, вероятно, находится в коде/таблицах указателей самого `MW_MAIN.EXE`, а не в отдельном текстовом ресурсе.
