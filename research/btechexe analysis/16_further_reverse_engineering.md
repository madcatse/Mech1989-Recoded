# 16. Необходимые дальнейшие исследования

## Приоритет 1 — draw command interpreter

Исследовать главный draw traversal и четыре обработчика типов команд.

Результат:

- спецификация command records;
- nested references;
- transforms;
- primitive semantics;
- material/shading flags.

## Приоритет 2 — runtime combatant layout

Для таблиц `8 × 0x0e` и `8 × 0x55` составить field map:

- readers;
- writers;
- тип;
- единицы;
- жизненный цикл.

## Приоритет 3 — mech definition layout

Восстановить блок типа меха:

- скорость;
- масса;
- turn rate;
- heat sinks;
- оружие;
- ammo;
- shape indices;
- armor;
- animation parameters.

## Приоритет 4 — damage pipeline

Проследить:

```text
collision
→ hit location
→ armor
→ internal structure
→ component damage
→ movement/weapon penalties
→ destruction
```

## Приоритет 5 — динамическая трассировка

Нужны:

- watchpoints;
- логирование draw calls;
- дампы runtime records;
- запись input/state;
- покадровое сравнение;
- фиксация RNG seed, если возможно.

## Артефакты исследования

Для каждой подтверждённой функции создавать:

- human-readable имя;
- сигнатуру;
- список xrefs;
- таблицу полей;
- псевдокод;
- тест;
- уровень уверенности.
