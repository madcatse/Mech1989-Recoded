# 13. Рендеринг и камера

## Вывод

Оригинальный боевой renderer программный и рассчитан на экран 320×200. Он включает:

- настройку камеры;
- преобразование world → view;
- clipping;
- backface/edge tests;
- сортировку частей и primitives;
- перспективную проекцию;
- polygon/line rasterization;
- HUD;
- копирование viewport.

## Что следует воспроизвести семантически

- field of view;
- положение камеры относительно меха;
- высоту камеры;
- направление корпуса и торса;
- near/far clipping;
- palette/material mapping;
- cockpit framing;
- размеры HUD;
- дальность отображения.

## Что следует заменить

- software rasterizer → OpenGL;
- painter sorting → depth buffer;
- segmented buffers → GPU resources;
- ручной clipping → стандартный clip space;
- 8-bit framebuffer → palette-aware shader или RGBA.

## Рекомендуемые режимы renderer

1. **Legacy-compatible**  
   Ближайшее соответствие палитре, FOV и low-resolution виду.

2. **Enhanced**  
   Современное разрешение, фильтрация и освещение при сохранении геометрии и камеры.

3. **Debug**  
   Wireframe, normals, pivots, bounds, shape IDs, command types.

## Камера

```cpp
struct BattleCamera {
    Vec3 localOffset;
    float bodyYawWeight;
    float torsoYawWeight;
    float pitch;
    float verticalOffset;
    float fovY;
};
```

## Уверенность

**Высокая** для общего render pipeline и разрешения.  
**Средняя** для точной модели камеры и проекции.
