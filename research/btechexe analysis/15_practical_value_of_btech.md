# 15. Практическая ценность BTECH.EXE

## Что уже можно извлечь

### Загрузчик ресурсов

- Dynamix LZW;
- far-pointer directory;
- rebasing;
- runtime shape slots;
- global shape index.

### Каркас боя

- fixed-step update;
- combatant slots;
- расстояния и видимость;
- player update;
- AI update;
- projectiles;
- damage/effects;
- mission state.

### Управление

- плавный поворот;
- отдельный торс;
- ускорение;
- ограничения;
- зависимость от повреждений.

### Боевая система

- оружейные слоты;
- cooldown;
- боезапас;
- тепло;
- projectiles;
- попадания.

### Рендер-контракт

- shape records;
- descriptors;
- command dispatch;
- scale;
- sorting;
- animation transforms.

## Что не стоит переносить напрямую

- Ghidra-C;
- DOS API;
- segmented memory;
- software rasterizer;
- глобальные адреса;
- жёсткие пулы без abstraction;
- fixed-point во внутренних API.

## Главная ценность

BTECH.EXE описывает контракт:

```text
данные типа меха
→ runtime instance
→ управление и AI
→ оружие и повреждения
→ pose и shape assembly
→ render commands
```

Именно этот контракт позволяет сделать реимплементацию, а не визуальный viewer.
