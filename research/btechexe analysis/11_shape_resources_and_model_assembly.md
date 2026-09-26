# 11. Shape/PCK-ресурсы и сборка моделей

## Подтверждённые факты

После распаковки `*PCK.TBL` начинается с таблицы DOS far pointers:

```cpp
struct ShapePointerEntry {
    std::uint16_t offset;
    std::uint16_t segment;
};
```

Линейный адрес:

```cpp
linear = segment * 16 + offset;
```

Таблица завершается `0000:0000`.

Загруженные shape-ресурсы хранятся в runtime-слотах размером шесть байт:

```cpp
struct LoadedShapeSlot {
    std::uint16_t ptrOff;
    std::uint16_t ptrSeg;
    std::uint16_t recordCount;
};
```

Runtime shape index является глобальным индексом по нескольким загруженным ресурсам.

## Record prefix

Подтверждённые или сильные кандидатные поля:

```cpp
struct ShapeRecordPrefix {
    std::uint8_t flags;
    std::uint8_t scaleShift;
    std::uint16_t extentOrRadius;
    std::uint16_t zero04;
    std::uint16_t zero06;
    std::uint8_t unknown08;
    std::uint8_t edgeCount;
    std::uint16_t partCount;
    std::uint16_t partTableBase;
};
```

## Runtime draw traversal

Главная draw-функция:

1. разрешает global shape index;
2. читает `partCount` по `+0x0a`;
3. проходит по 8-байтным descriptors;
4. вычисляет transform/sort key;
5. сортирует части;
6. находит command record;
7. dispatch по type `0..3`.

## Главный вывод

Record нельзя автоматически считать целой моделью. Он может быть частью, позой, LOD, damage-вариантом или набором draw-команд.

## Требования к новому importer

- сохранять raw bytes каждого descriptor;
- не сливать records без доказанного assembly graph;
- поддерживать nested shape references;
- хранить pivots и локальные transforms;
- экспортировать диагностический JSON;
- сохранять исходные индексы и flags.

## Уверенность

**Высокая** для pointer table, rebasing, slot manager и `partCount`.  
**Средняя** для структуры descriptor.  
**Низкая/средняя** для семантики command types.
