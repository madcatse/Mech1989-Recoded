# 18. План реализации нового OpenGL-движка

## Цель

Создать современную реимплементацию боевого движка MechWarrior 1989, использующую оригинальные модели, terrain-ресурсы и параметры игры, с возможностью точного и улучшенного визуального режима.

---

## Фаза 0. Инфраструктура проекта

### Задачи

- C++20 или Rust;
- CMake/Meson;
- SDL3 или GLFW;
- OpenGL 4.1 Core как переносимый базовый профиль;
- GLAD;
- glm или собственная math library;
- Catch2/GoogleTest;
- Dear ImGui для debug UI;
- CI для Windows/Linux;
- deterministic fixed-step loop.

### Результат

Пустое окно, render loop, fixed simulation tick, logging и unit tests.

### Критерий готовности

- стабильный запуск;
- resize;
- input;
- debug overlay;
- fixed tick без зависимости от FPS.

---

## Фаза 1. Legacy resource library

### Задачи

- чтение Dynamix wrapper;
- LZW/RLE unpack;
- far-pointer table;
- shape slot abstraction;
- binary reader с bounds checks;
- JSON diagnostic dump;
- golden tests для JENPCK/LOCPCK/MARPCK;
- SHA-256 фиксация входов.

### Результат

Библиотека `mwlegacy` с безопасным API.

### Критерий готовности

Все известные PCK-файлы распаковываются детерминированно, число records совпадает с эталоном.

---

## Фаза 2. Shape intermediate representation

### Задачи

- raw record;
- prefix;
- part descriptors;
- command records;
- vertices;
- primitive lists;
- source offsets;
- flags;
- scale;
- nested references;
- unknown fields без потери данных.

### Результат

`LegacyShapeIR`, пригодный для анализа и последующего runtime interpretation.

### Критерий готовности

Повторная сериализация диагностического представления не теряет ни одного байта, важного для parser.

---

## Фаза 3. Draw-command interpreter

### Задачи

- реверс функций command type 0–3;
- трассировка оригинала;
- transform stack;
- parent-child assembly;
- pivot;
- sort keys;
- visibility;
- line/polygon/special primitives;
- material mapping.

### Результат

Game-accurate сборка Jenner, Locust и Marauder.

### Критерий готовности

Силуэт, части и pose совпадают с контрольными скриншотами в нескольких ракурсах.

---

## Фаза 4. OpenGL renderer

### Задачи

- VAO/VBO/EBO;
- shader pipeline;
- depth buffer;
- indexed polygons;
- line primitives;
- palette emulation;
- flat shading;
- wireframe/debug modes;
- camera;
- offscreen framebuffer;
- legacy 320×200 mode;
- enhanced resolution mode.

### Результат

Интерактивный viewer с правильной model assembly.

### Критерий готовности

Все контрольные модели отображаются устойчиво, без пропавших или смещённых частей.

---

## Фаза 5. Terrain и карта

### Задачи

- найти и разобрать terrain/tile resources;
- tile map;
- heightfield;
- tile materials;
- collision footprint;
- spawn points;
- zones/triggers;
- legacy draw distance.

### Результат

Статическая игровая карта с перемещаемой камерой.

### Критерий готовности

Карта совпадает по структуре тайлов и высот с оригинальным полем боя.

---

## Фаза 6. Entity-component runtime

### Задачи

- EntityId;
- Transform;
- Combatant;
- MechRuntime;
- WeaponRuntime;
- Projectile;
- Effect;
- Team;
- Health/Damage;
- Animation;
- AI;
- deterministic object lifecycle.

### Результат

Независимая от renderer симуляционная сцена.

### Критерий готовности

Headless-тест может проигрывать бой без графики.

---

## Фаза 7. Движение меха

### Задачи

- target/current speed;
- acceleration/deceleration;
- body yaw;
- torso yaw;
- bounds;
- terrain following;
- collisions;
- leg damage penalties;
- walk pose driver.

### Результат

Управляемый мех с оригинальным ощущением инерции.

### Критерий готовности

Время разгона, торможения и полного разворота совпадает с измерениями оригинала в пределах заданного допуска.

---

## Фаза 8. Оружие и тепло

### Задачи

- weapon definitions;
- mount points;
- cooldown;
- ammo;
- heat generation;
- heat dissipation;
- shutdown;
- hitscan;
- projectile launch;
- muzzle effects;
- HUD status.

### Результат

Игрок может вести огонь всеми базовыми классами оружия.

### Критерий готовности

Темп огня, расход ammo и нагрев совпадают с эталонными сценариями.

---

## Фаза 9. Снаряды, столкновения и damage

### Задачи

- swept projectile collision;
- hit location;
- armor;
- internal structure;
- component damage;
- movement penalties;
- weapon destruction;
- death/fall;
- impact effects.

### Результат

Полная цепочка выстрел → попадание → последствия.

### Критерий готовности

Сценарные тесты повреждений дают повторяемый результат.

---

## Фаза 10. AI

### Задачи

- FSM;
- target selection;
- approach;
- attack;
- desired range;
- aiming;
- weapon selection;
- evade;
- retreat;
- obstacle handling;
- deterministic reaction timing.

### Результат

AI способен обнаружить, преследовать и атаковать игрока.

### Критерий готовности

AI проходит набор сценариев без зависаний и демонстрирует поведение, сравнимое с оригиналом.

---

## Фаза 11. Камера, cockpit и HUD

### Задачи

- оригинальный FOV;
- camera offsets;
- torso/body coupling;
- cockpit mask;
- radar;
- heat;
- armor;
- weapon status;
- target marker;
- warning states;
- legacy and enhanced UI.

### Результат

Полноценный боевой экран.

### Критерий готовности

Игрок может провести бой без debug UI.

---

## Фаза 12. Миссии и интеграция с основной игрой

### Задачи

- mission input format;
- spawn configuration;
- teams;
- victory/defeat conditions;
- scripted events;
- переход данных из MW.EXE;
- сохранение результата боя.

### Результат

BTECH-compatible battle session, запускаемая из внешнего launcher или новой campaign shell.

### Критерий готовности

Минимум одна оригинальная миссия запускается и завершается корректно.

---

## Фаза 13. Совместимость и полировка

### Задачи

- record/replay inputs;
- deterministic RNG;
- frame capture;
- comparison harness;
- profiling;
- asset validation;
- crash-resistant parsing;
- packaging;
- mod-friendly normalized formats.

### Результат

Стабильный публичный prototype.

---

## Приоритеты

### P0 — блокирует проект

- точный ресурсный loader;
- draw-command interpreter;
- model assembly;
- fixed-step simulation;
- движение;
- базовое оружие;
- damage;
- AI.

### P1 — необходимо для полноценной игры

- terrain;
- cockpit/HUD;
- mission loading;
- все weapon types;
- death animations;
- effects.

### P2 — улучшения

- enhanced lighting;
- shadows;
- modern controls;
- mod tools;
- network play;
- editor.

---

## Рекомендуемые milestones

### M1 — Accurate Model Viewer

Правильная сборка и анимация трёх мехов.

### M2 — Battle Sandbox

Игрок, один AI, движение, стрельба, heat и damage.

### M3 — Original Mission Prototype

Одна карта и одна миссия.

### M4 — Full Combat Runtime

Все основные мехи, оружие, AI и HUD.

### M5 — Campaign Integration

Связь с campaign layer или новым launcher.

---

## Оценка зависимостей

```text
Resource Loader
    ↓
Shape IR
    ↓
Command Interpreter
    ↓
Renderer
    ↓
Animation
```

```text
Mech Definitions
    ↓
Movement ──→ AI
    ↓          ↓
Weapons ──→ Projectiles
    ↓          ↓
Heat      Damage
```

Terrain требуется одновременно для movement, collision, visibility и миссий.
