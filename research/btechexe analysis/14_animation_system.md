# 14. Анимационная система

## Вывод

В игре используются как минимум два типа анимации:

1. трансформации частей 3D-модели;
2. короткие последовательности кадров для эффектов.

Некоторые функции временно добавляют угол одной части нескольким связанным объектам перед отрисовкой и затем вычитают его. Это похоже на применение общего torso/part rotation к нескольким shape instances.

## Современная модель

```cpp
struct PartPose {
    float yaw;
    float pitch;
    float roll;
    Vec3 translation;
};

struct MechPose {
    PartPose torso;
    PartPose leftLeg;
    PartPose rightLeg;
    PartPose leftArm;
    PartPose rightArm;
};
```

## Возможные источники pose

- скорость;
- направление движения;
- состояние шага;
- угол торса;
- recoil;
- damage state;
- падение;
- смерть.

## Sprite/effect animation

Отдельный пул обновляет frame index по таблице до terminal code. Его следует реализовать независимо от skeletal/part animation.

## Задачи восстановления

- соответствие частей;
- parent-child graph;
- pivots;
- walk cycle;
- torso rotation;
- weapon recoil;
- damage variants;
- death/fall sequence;
- effect frame tables.

## Уверенность

**Высокая** для наличия part transforms и frame-анимаций.  
**Средняя** для конкретных связей между shape instances.
