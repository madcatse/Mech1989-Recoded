# 10. Столкновения и видимость

## Вывод

Движок использует несколько классов геометрических тестов:

- парные проверки видимости;
- расчёт расстояний;
- проверки попадания в прямоугольные области;
- sweep-проверки снарядов;
- специальные callbacks для зон карты.

Это скорее 2.5D collision model, чем полноценная общая физика.

## Практическая реализация

Для первого совместимого движка достаточно:

- capsule/circle для мехов;
- AABB или convex footprint для зданий;
- heightfield для земли;
- raycast для линии видимости;
- swept sphere или segment test для снарядов.

## API

```cpp
struct SpatialQueryService {
    bool hasLineOfSight(EntityId a, EntityId b) const;
    float distance(EntityId a, EntityId b) const;
    std::optional<Hit> raycast(const Ray& ray) const;
    std::optional<Hit> sweepSphere(const Vec3& from, const Vec3& to, float radius) const;
};
```

## Важная деталь

Расстояния и видимость лучше рассчитывать до AI и кэшировать на текущий тик. Это повторяет организацию оригинала.

## Задачи восстановления

- формы collision bounds мехов;
- высотные ограничения;
- возможность стрельбы через объекты;
- влияние terrain;
- столкновения мех-мех;
- скольжение вдоль препятствий;
- damage от столкновений.

## Уверенность

**Высокая** для кэширования парных отношений.  
**Средняя** для конкретной геометрии bounds.
