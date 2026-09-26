# 17. Общая стратегия разработки

## Принцип

Не переписывать BTECH.EXE построчно. Разделить проект на совместимый data/runtime layer и современный renderer/platform layer.

## Этапы

### Этап A — ресурсы

```text
TBL/PCK
→ распаковка
→ pointer table
→ records
→ descriptors
→ normalized intermediate representation
```

### Этап B — shape interpreter

Реализовать runtime-сборку частей и command dispatch.

### Этап C — battle sandbox

Минимальная сцена:

- terrain;
- игрок;
- один AI;
- движение;
- поворот торса;
- одно энергетическое оружие;
- одно projectile weapon;
- heat;
- damage;
- death.

### Этап D — перенос параметров

Извлекать характеристики из оригинальных таблиц, не подбирать вручную.

### Этап E — сравнение

Сопоставлять:

- скорость;
- время поворота;
- дальность;
- cooldown;
- heat;
- траекторию;
- AI reaction;
- damage result;
- pose.

## Архитектурная граница

```text
Legacy compatibility layer
    ↓
Normalized game data
    ↓
Battle simulation
    ↓
Presentation snapshot
    ↓
OpenGL renderer / audio / UI
```
