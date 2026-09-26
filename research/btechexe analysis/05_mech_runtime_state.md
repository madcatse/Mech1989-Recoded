# 5. Runtime-состояние меха

## Вывод

BTECH.EXE разделяет статические характеристики типа меха и изменяемое состояние конкретного экземпляра. Статические блоки имеют повторяющийся крупный шаг, близкий к `0xe0` байт, а runtime-структуры содержат позиции, углы, скорость, оружие, повреждения и AI.

## Предлагаемая современная модель

```cpp
struct MechDefinition {
    std::string id;
    float mass;
    float maxSpeed;
    float turnRate;
    float heatDissipation;
    std::vector<WeaponMountDefinition> mounts;
    ModelAssemblyDefinition model;
};

struct MechRuntime {
    const MechDefinition* definition;
    Transform transform;
    MechMotionState motion;
    MechControlState controls;
    HeatState heat;
    DamageState damage;
    std::vector<WeaponRuntime> weapons;
    AnimationState animation;
    AIState ai;
};
```

## Предварительные смещения

Некоторые поля многократно используются как углы, скорость, параметры оружия или состояния подсистем. Эта карта пока кандидатная и не должна становиться жёсткой спецификацией без трассировки.

## Правила имплементации

- отделить immutable definition от runtime;
- не копировать монолитную packed-структуру в игровой код;
- хранить raw legacy record отдельно;
- вести таблицу соответствия `legacy offset → semantic field`;
- добавлять тест для каждого подтверждённого поля.

## Уверенность

**Высокая** для разделения definition/runtime.  
**Низкая/средняя** для назначения большинства конкретных смещений.
