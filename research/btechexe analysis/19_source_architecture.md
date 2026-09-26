# 19. Предлагаемая архитектура исходного кода

```text
src/
  app/
    main.cpp
    game_app.*
  platform/
    window.*
    input.*
    timer.*
    filesystem.*
  legacy/
    binary_reader.*
    dynamix_lzw.*
    resource_wrapper.*
    shape_pointer_table.*
    shape_parser.*
    shape_ir.*
    legacy_units.*
  render/
    renderer.*
    shader.*
    mesh_gpu.*
    palette.*
    camera.*
    debug_draw.*
    legacy_viewport.*
  world/
    world.*
    entity.*
    transform.*
    terrain.*
    spatial_queries.*
  combat/
    battle_simulation.*
    combatant.*
    mech_definition.*
    mech_runtime.*
    movement_system.*
    weapon_system.*
    heat_system.*
    projectile_system.*
    damage_system.*
  ai/
    ai_state.*
    target_selection.*
    mech_ai_system.*
  animation/
    model_assembly.*
    pose.*
    mech_animation.*
    effect_animation.*
  ui/
    cockpit.*
    hud.*
    radar.*
    debug_panels.*
  missions/
    mission_definition.*
    mission_runtime.*
    triggers.*
  tests/
    legacy/
    render/
    combat/
    replay/
```

## Основные правила

- `legacy/` ничего не знает об OpenGL;
- `combat/` ничего не знает о renderer;
- renderer получает immutable presentation snapshot;
- parser сохраняет unknown fields;
- каждый reverse-engineered field имеет источник и confidence;
- headless simulation является обязательной;
- debug UI может показывать raw offsets и runtime values.

## Формат normalized data

Рекомендуется JSON или собственный versioned binary cache:

```text
original TBL
→ parser
→ validated normalized cache
→ runtime
```

Оригинальные файлы остаются источником истины, cache можно пересоздать.
