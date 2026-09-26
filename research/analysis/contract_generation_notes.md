# Contract And Recruiting EXE Findings

Дата: 2026-07-31.

Цель: первый проход по следам контрактов, mission names, найма и передачи боя в
оригинальных `MW_MAIN.EXE`, `MW.EXE` и `BTECH.EXE`.

## Созданные Артефакты

- `research/analysis/contract_string_xrefs.csv` - простой word-xref scan по
  строкам, содержащим `contract`.
- `research/analysis/contract_string_contexts.md` - byte contexts по результатам
  простого scan. Важно: содержит шум и ложные совпадения.
- `research/analysis/mission_string_xrefs.csv` - простой scan по строкам,
  содержащим `mission`.
- `research/analysis/mission_string_contexts.md` - byte contexts по mission
  xrefs.
- `research/analysis/mw_main_contract_recruit_mission_offset_xrefs.csv` -
  xrefs на полные loaded offsets для категорий `contract_ui`, `mission_name`,
  `crew_recruitment`.
- `research/analysis/mw_main_contract_recruit_mission_lowword_xrefs.csv` -
  low-word scan для тех же категорий. Нужен для строк выше первого 64K window,
  но содержит много ложных совпадений.
- `research/analysis/mission_name_table.csv` - 34 mission title строки из
  `MW_MAIN.EXE` с family hints из fan-wiki исследования.

## `MW.EXE`

`MW.EXE` выглядит как launcher/orchestrator:

- запускает `MW_MAIN.EXE`;
- если child return code не нулевой, запускает `BTECH.EXE`;
- после возврата из `BTECH.EXE` снова запускает `MW_MAIN.EXE`;
- передает в child state только графику/звук/memory class/state block.

Практический вывод: генерация контрактов и найм почти наверняка живут в
`MW_MAIN.EXE`; `BTECH.EXE` нужен для боевого исполнения миссии и результата.

## `MW_MAIN.EXE`: Контрактный Генератор

Главный найденный кандидат: `FUN_101b_4f6e`.

`FUN_101b_52ee` вызывает `FUN_101b_4f6e` несколько раз и выглядит как генератор
списка доступных контрактов. Ключевые поля:

- `DS:0948` - количество/наличие предложений контрактов.
- `DS:0B52` - позднеигровой флаг, влияющий на шанс extended/campaign contract.
- `DS:0479` - day counter within displayed month.
- `DS:047B` - displayed month, zero-based.
- `DS:047D` - displayed year.

Предварительная логика `FUN_101b_52ee`:

```text
DS:0B52 = 0

if year < 3028:
    DS:0948 = 0
    if random() < 4:
        no contracts
    else:
        offer_count = random() + 2
else:
    if year == 3028 and date is before roughly August day-counter 40:
        same early path as above
    else if year > 3029:
        DS:0B52 = 0x4B
        same early offer-count path as above
    else:
        DS:0B52 = 0x32
        offer_count = random() + 3

DS:0948 = offer_count
for slot in offer_count:
    FUN_101b_4f6e(slot)
```

Нужно подтвердить `random()` range через `FUN_101b_0c17`, но форма уже хорошо
объясняет, почему контрактов может не быть и почему поздняя игра чаще дает
campaign-like контракты.

### Отказ Дома От Контрактов

В начале `FUN_101b_4f6e` есть проверка:

```text
if house_negative_counter[current_house] > 7:
    return no offer
```

В псевдокоде это:

```c
if (7 < *(uint *)(*(int *)0x46d * 2 + 0x4a9)) {
    return;
}
```

`DS:046D` уже используется как текущий House id. Пары массивов около `DS:049F`
и `DS:04A9` согласуются с wiki-правилом: частые действия против государства
могут привести к отказу заключать контракты.

### Оценка Силы Контракта

`FUN_101b_4f6e` считает score в `DS:4B79`:

- базовый уровень зависит от campaign rank/progress `DS:04B3`;
- добавляется сила lance игрока через assigned mech/pilot данные;
- некоторые offer slots получают множитель:
  - slot `1` и `4`: примерно `+50%`;
  - slot `2` и `5`: примерно `/2`.

Таблица силы мехов восстановлена полностью. `DS:0532` содержит указатели на
записи принадлежащих игроку мехов, а первый байт каждой записи адресуется
от базы `DS:4B55`. В исходном `MW_MAIN.EXE` сама десятиэлементная таблица видна
по file offset `0x00D855`:

```text
Locust, Wasp, Jenner                    -> 1
Phoenix Hawk, Shadow Hawk, Wolverine    -> 2
Rifleman, Warhammer, Marauder,
BattleMaster                            -> 3
```

Учитываются только мехи с назначенным пилотом. Базовая формула теперь доказана
целиком:

```text
reputation_strength = 0
if reputation_points > 8:  reputation_strength = 1
if reputation_points > 20: reputation_strength = 2
if reputation_points > 35: reputation_strength = 3

score = (reputation_strength + sum(assigned_mech_strength)) * 10
```

Порог `DS:04B3` здесь относится к накопленным очкам репутации, а не к году и
не к уже вычисленному рангу. Эта переменная увеличивается после успешного
контракта и отдельно преобразуется в отображаемый ранг через пороги `5/10/20`.

Затем score раскладывается на три счетчика предполагаемых врагов:

```text
count_a = score / 25
rem     = score % 25
count_b = rem / 15
count_c = (rem % 15) / 7
```

Сумма принудительно держится в диапазоне `1..4`: при переполнении сначала
удаляется light, затем medium и только затем heavy. Нулевая сумма заменяется
одним light. Если после этого получены четыре heavy, inclusive roll `0..10`
оставляет `4H` на значениях `0..4`, меняет состав на `3H+1M` при `5..7` и на
`2H+2M` при `8..10`.

Реализация находится в `battle_opposition.cpp`: контрактный экран использует
эту формулу, а боевой адаптер больше не берёт только первый запрос. Он повторяет
BTECH round-robin `H -> M -> L`, выбирает типы исходными битовыми масками и
создаёт до четырёх противников в opposing slots `0..3`. Для extended campaign
на этапе с индексом `i` применяется оригинальная формула
`(max(0, count - i) + 2) / 3`; утроенные контрактные оценки поэтому равномерно
переходят в каждый из трёх боёв.

Доказанное назначение по contract-screen строкам и handoff writer
`MW_MAIN.EXE:0x005B0D..0x005B2A`:

- `DS:097A + slot*2` - estimated heavy count, copied to BTECH context `+3`.
- `DS:0986 + slot*2` - estimated medium count, copied to BTECH context `+4`.
- `DS:0992 + slot*2` - estimated light count, copied to BTECH context `+5`.

Логика score делает первый счетчик самым дорогим, а BTECH выбирает для этих
трёх buckets type ranges `4..7`, `2..3`, `0..1` соответственно.

### Цена, Advance, Salvage

Цена:

```text
base_price = ((score >> 1) + 1) * 100
price = round10(((random + 1 + house_price_bias[current_house])
        * base_price * house_price_multiplier[current_house]) / 100
        + base_price)
price = clamp(price, 100, 9990)
DS:09C2 + slot*2 = price
```

`0x2706` decimal = `9990`, что совпадает с максимальной ценой из wiki
(`9,990k C-bills`).

Есть еще вычисления:

```text
DS:099E + slot*2 = max(100, (price / 40) * 10)

standing_delta = clamp((positive[current_house] - negative[current_house]) * 4,
                       -10, 10)

term_a = max(0, standing_delta + house_term_a[current_house])
DS:09CE + slot*2 = term_a
DS:09AA + slot*2 = term_a >> 2

term_b = max(0, standing_delta + house_term_b[current_house])
DS:09DA + slot*2 = term_b
DS:09B6 + slot*2 = term_b >> 2
```

`term_a/term_b` почти наверняка связаны с salvage percent и advance percent, но
какой из них какой пока не доказано.

### Выбор Mission Type

`FUN_101b_4f6e` выбирает:

- mission/target category: `DS:0962 + slot*2`;
- mission class bucket: `DS:096E + slot*2`, обычно `1..4`;
- mission text/data pointer: `DS:094A + slot*2`;
- mission id/first byte: `DS:0A2B + slot*2`;
- scenario/terrain/target candidate: `DS:0956 + slot*2`.

Уточнённая трассировка raw assembly `0x005710..0x00589D` доказывает связанный
маршрут. `DS:0A02` — таблица `5 Houses x 8 target categories`; ноль запрещает
категорию, а `1..4` задаёт максимальный mission class. Таблицы mission pointers
содержат соответственно `10/4/15/5` записей для classes `1/2/3/4`.

Для class 1 либо special category `5..7` одна из пяти House-таблиц `8 x 8`
возвращает one-based planet table order. Для classes `2..4` и category `0..4`
выбирается планета из соответствующего House range; выбор повторяется, пока
planet record byte 3 равен нулю или меньше mission class, который сохранён в
`DL`. Итоговый zero-based planet index записывается в `DS:0956`.

Map seed не выбирается независимо:

```text
DS:0956 = target_planet_table_order - 1
context[6] = DS:0956 + 1
BTECH seed = context[6] % 20
           = target_planet_table_order % 20
```

Это одновременно доказывает, что terrain scenario records — первые `0..19`
SNARIO, включая записи с raw byte 8 равным нулю. Byte 8 не является enable
flag; данные после record 19 относятся к position banks.

Важная логика:

```text
if DS:0B52 != 0 and random() >= DS:0B52:
    mission_class = 4

mission_ptr = table_by_class[mission_class][random_even]
mission_id = mission_ptr[0]

if mission_id is 0x0F, 0x10, or 0x12:
    this is an extended/campaign mission candidate
    if player has enough lance/mech capacity:
        price *= 3
        enemy counts *= 3
```

Это подтверждает, что extended campaigns являются особыми mission ids и
увеличивают цену/оценку противников примерно в 3 раза.

## `MW_MAIN.EXE`: Post-Mission Payment And Status

Кандидат: `FUN_101b_1cd4`.

Найдены признаки:

- начисляет основную оплату из `DS:09FC`;
- применяет коэффициент `DS:123F`, по умолчанию `100`;
- отдельно начисляет advance/payment из `DS:0A00`;
- после результата контракта меняет house counters около `DS:049F` и
  `DS:04A9`;
- вызывает `FUN_101b_21d9` для пилотов после миссии;
- вызывает `FUN_101b_52ee` для обновления/генерации контрактов после
  campaign update.

Это место нужно возвращать при реализации оплаты, штрафов за бегство и изменения
репутации.

## `MW_MAIN.EXE`: Найм И Пилоты

Найденный кандидат инициализации: `FUN_101b_6cd5`.

Функция проходит ровно `42` записи:

```c
for (i = 1; i != 0x2B; ++i) {
    ptr = *(word *)(i * 2 - 0x6EDF);
    *(byte *)(i + 0xA37) = *(byte *)(ptr + 1);
    *(byte *)(i + 0xA62) = *(byte *)(ptr + 2);
    *(byte *)(i + 0xA8D) = 0;
}
```

Текущее восстановление в `src/main.cpp` уже подтвердило, что перед именем пилота
в `MW_MAIN.EXE` лежат три байта:

```text
portrait, gunnery, piloting
```

Следовательно, `FUN_101b_6cd5` использует таблицу указателей на pilot metadata
и переносит два skill byte в рабочие массивы `DS:0A37` и `DS:0A62`. `DS:0A8D`
не availability-флаг: следующий найденный код показывает, что это счетчик
миссий/опыта до повышения.

### Улучшение Пилотов

Кандидат: `FUN_101b_21d9`.

Функция вызывается после mission flow для назначенных пилотов. Она:

- берет pilot index из lance/mech assignment;
- увеличивает `DS:0A8D[pilot]`;
- проверяет текущий skill в `DS:0A37[pilot]`;
- повышает skill после порогов:
  - skill `0`: после `3` миссий;
  - skill `1`: после `10` миссий;
  - skill `2`: после `15` миссий;
  - skill `3`: не повышается;
- при повышении записывает новый skill и в `DS:0A37[pilot]`, и в
  `DS:0A62[pilot]`;
- обновляет skill fields назначенного mech/pilot slot около `DS:04F2` и
  `DS:04FA`;
- сбрасывает `DS:0A8D[pilot] = 0`.

Это важное отличие от текущей временной логики: оригинал содержит явный
пост-миссионный опыт пилотов.

### Что Еще Не Найдено По Найму

Пока не найден собственно алгоритм bar/recruit selection:

- сколько кандидатов появляется в баре;
- зависит ли выбор от планеты/населения/дома;
- где помечается hired/unavailable;
- как используется строка `THERE'S NOBODY AROUND RIGHT NOW.`;
- где проверяется лимит нанятых пилотов.

Следующий проход по найму стоит вести от:

- `DS:0A37`, `DS:0A62`, `DS:0A8D`;
- строк `0x011D31..0x012939`;
- ui strings `THERE'S NOBODY AROUND RIGHT NOW.`, `SORRY!`;
- save block `0x0500..0x05B8`, где уже отмечен candidate roster/pilot/mission
  and combat-transfer block.

## `BTECH.EXE`

В `BTECH.EXE` найдены mission-related strings около `0x027463..0x0277E5`:

- `DEFENSE OF A WATER FACTORY`
- `DEFENSE OF A FUEL DUMP`
- `DEFENSE OF FIELD COM UNIT`
- `DEFENSE OF A SUPPLY DEPOT`
- `DEFENSE OF PORT FACILITIES`
- `SUPPRESSION OF REBELLION`
- `A PLANETARY ASSAULT`
- `DISABLING OF A FIELD COM CENTER`
- `YOUR NEXT MISSION:`

Практический вывод: `BTECH.EXE` знает часть названий/экранов миссий и, вероятно,
показывает `YOUR NEXT MISSION:` между боями extended campaign. Полный набор
34 названий пока надежнее брать из `MW_MAIN.EXE`, но боевую семантику mission
ids нужно восстанавливать в `BTECH.EXE`.

## Следующие Шаги

1. Разметить `FUN_101b_4f6e`, `FUN_101b_52ee`, `FUN_101b_1cd4`,
   `FUN_101b_21d9`, `FUN_101b_6cd5` локальными именами в отдельной
   pseudo-notes таблице.
2. Сделать save-diff тесты оригинала:
   - планета с no contracts;
   - плохие отношения с домом;
   - accepted contract before/after negotiation;
   - completed single mission;
   - fled mission;
   - extended campaign.
3. Найти код UI переговоров:
   - `REQUEST MISSION`;
   - `MERCENARY CONTRACT`;
   - `SUBMIT/ACCEPT`;
   - `YOUR OFFER IS UNACCEPTABLE!`;
   - `YOUR CONTRACT IS ACCEPTED.`;
   - Kurita third rejection/reset behavior.
4. Найти bar/recruit selection:
   - xrefs к pilot metadata pointer table;
   - xrefs к `THERE'S NOBODY AROUND RIGHT NOW.`;
   - save writes around `0x0500..0x05B8`.
5. В `BTECH.EXE` трассировать mission id/objective fields:
   - structure destroy;
   - structure touch/retrieval;
   - defense target destroyed;
   - sprint boundary failure;
   - result code back to `MW_MAIN.EXE`.
