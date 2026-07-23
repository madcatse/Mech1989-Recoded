# MechWarrior 1989 GPU Viewer: документация текущей версии

Документ описывает текущую рабочую версию смотровика из папки `mech89GL_viewer`.

## Назначение

`mech89GL_viewer` — это интерактивный просмотрщик 3D-shape ресурсов MechWarrior 1989 из `*PCK.TBL`. Он основан на прежнем Tk viewer, но вместо software/z-buffer rasterizer использует GPU viewport:

- геометрия загружается в OpenGL VBO;
- треугольники и линии рисуются через EBO;
- сохранены загрузка `TBL`, список records, MWA animation editor, OBJ export, мышь и горячие клавиши;
- OpenGL-контекст создаётся нативно через Windows WGL внутри Tk-окна.

## Состав папки

```text
mech89GL_viewer/
  mw1989_viewer.py            главный Tk UI, загрузка, список records, MWA editor
  mw1989_gpu.py               OpenGL viewport, VBO/EBO renderer, triangulation
  mw1989_runtime_shape.py     runtime-oriented parser для PCK/TBL records
  mw1989_tbl_to_mesh_v3.py    legacy parser/export helpers
  validate_gpu_viewer.py      headless проверки импорта, GPU batch и animation preview
  run_viewer.bat              быстрый запуск viewer
  README.md                   короткая справка
  GPU_VIEWER_GUIDE_RU.md      этот документ
```

## Требования

Проверенная среда:

- Windows;
- Python 3.12 x64;
- `tkinter`;
- `numpy`;
- OpenGL драйвер с поддержкой VBO/EBO.

Для удобной дальнейшей разработки полезно поставить:

```bat
python -m pip install numpy pillow PyOpenGL PyOpenGL_accelerate pygame moderngl glcontext pyrr
```

Текущий viewer не требует PyOpenGL для своего WGL renderer, но эти библиотеки удобны для экспериментов, прототипов, отладки матриц и будущих вариантов renderer.

Проверка установленного окружения:

```bat
python -c "mods=['numpy','OpenGL','OpenGL_accelerate','pygame','moderngl','glcontext','pyrr','PIL']; import importlib.util as u; [print(f'{m}:', 'ok' if u.find_spec(m) else 'missing') for m in mods]"
```

## Запуск

Из папки проекта:

```bat
cd C:\Users\MadCat\Documents\MechwarriorRecomp
python mech89GL_viewer\mw1989_viewer.py --runtime --backend gpu "Sorted Original Files\TBL\viewer8\JENPCK.TBL"
```

Или из папки viewer:

```bat
cd C:\Users\MadCat\Documents\MechwarriorRecomp\mech89GL_viewer
run_viewer.bat "..\Sorted Original Files\TBL\viewer8\JENPCK.TBL"
```

Можно открывать и другие ресурсы:

```bat
run_viewer.bat "..\Sorted Original Files\TBL\viewer8\LOCPCK.TBL"
run_viewer.bat "..\Sorted Original Files\TBL\viewer8\MARPCK.TBL"
run_viewer.bat "..\Sorted Original Files\TBL\viewer8\SHAPCK.TBL"
```

Если запустить без аргументов, файл можно выбрать кнопкой `Open TBL...`.

## Проверка без открытия окна

Быстрая проверка текущего pipeline:

```bat
cd C:\Users\MadCat\Documents\MechwarriorRecomp\mech89GL_viewer
python validate_gpu_viewer.py
```

Проверка конкретного ресурса:

```bat
python validate_gpu_viewer.py "..\Sorted Original Files\TBL\viewer8\MARPCK.TBL"
```

Скрипт проверяет:

- импорт runtime records;
- fan-triangulation polygons в triangle EBO;
- корректность индексов EBO относительно VBO;
- нормализацию цветов `stored`, `lit`, `ega`;
- светлый цвет wireframe edges;
- наличие и формат `4x4` part/group transform matrices;
- сборку MWA animation preview record в GPU batch.

Успешный вывод выглядит примерно так:

```text
import: ok
record: #000 vertices=11 primitives=4
triangulation: ok triangles=7 line_segments=15
colors: ok stored/lit/ega/wire color sets=3/5/3/3
part transforms: ok matrices=1
animation gpu batch: ok triangles=38 sources=3
```

## Интерфейс

Левая панель:

- `Open TBL...` — открыть ресурс `*PCK.TBL`;
- `Export current OBJ...` — экспортировать текущий record/animation preview;
- `Show animation` / `Show model` — переключить обычный просмотр и MWA preview;
- `Reset view` — сбросить камеру;
- список `TBL records` — выбрать raw/runtime record;
- `Add to animation list` — добавить выбранный record в текущий MWA frame;
- блок `MWA animation` — редактирование кадров, длительности и набора records.

Viewport:

- рисует текущую модель через `backend=gpu-vbo-ebo`;
- показывает статистику: число triangles, lines, groups, colors, render time;
- отображает текущий режим: solid/wire, perspective/ortho, shade/cull/zoom.

## Управление мышью

- левая кнопка + drag — вращение модели;
- колесо — zoom;
- средняя или правая кнопка + drag — pan.

## Горячие клавиши

Навигация:

- `N`, `PageDown`, `Down`, `Right` — следующий record;
- `B`, `PageUp`, `Up`, `Left` — предыдущий record;
- `[`, `,` — предыдущий frame внутри найденной sequence;
- `]`, `.` — следующий frame внутри найденной sequence;
- `Esc` — закрыть viewer.

Вид:

- `W` — solid/wireframe;
- `P` — perspective/orthographic projection;
- `A` или `Space` — auto-rotate;
- `+` / `-` — zoom in/out;
- `R` — reset view;
- `1` — game-like view preset;
- `2` — side view;
- `3` — front view;
- `V` — показать/скрыть vertices;
- `X` — показать/скрыть axes.

Диагностика renderer:

- `L` — показать/скрыть line primitives;
- `S` — переключить shade mode: `stored` → `lit` → `ega`;
- `C` — переключить cull mode: `off`, `nz`, `pz`, `screen_cw`, `screen_ccw`;
- `D` — переключить depth order flag в UI state;
- `Z` — подтвердить активный GPU backend.

Файлы и режимы:

- `O` — открыть TBL;
- `E` — экспорт OBJ;
- `T` — переключить model/animation mode;
- `F` — auto frame animation по найденным frame sequences.

## MWA animation editor

MWA editor в этой версии — диагностический редактор preview-анимации, а не оригинальный формат игры.

Поля:

- `Frame` — номер кадра;
- `ms` — длительность кадра в миллисекундах;
- `Records` — список record indices через запятую, пробел, `+`, `;` или диапазоны вроде `0-6`.

Кнопки:

- `Preview/apply` — собрать текущий MWA frame из указанных records и показать его;
- `Add frame` — добавить пустой следующий frame;
- `Delete` — удалить выбранные components из frame или сам frame;
- `Play MWA` / `Stop MWA` — проигрывать непустые MWA frames;
- `Save` — сохранить `.mwa` JSON;
- `Load` — загрузить `.mwa` JSON.

GPU-анимация работает так:

1. выбранные raw records объединяются в composite `LoadedRecord`;
2. `triangulate_record()` строит VBO/EBO batch для текущего кадра;
3. `GpuRenderer` загружает batch в GPU buffers;
4. playback меняет текущий composite frame по длительности `duration_ms`.

## Renderer pipeline

Основной путь находится в `mw1989_gpu.py`.

`triangulate_record()`:

- принимает `LoadedRecord`;
- фильтрует invalid vertex indices;
- превращает polygon primitives в fan triangles;
- создаёт отдельные vertices для flat color на primitive;
- создаёт `triangle_indices` и `line_indices`;
- создаёт `group_matrices`;
- нормализует RGB в диапазон `0..1`;
- в wireframe режиме загружает светлый edge color.

`GpuRenderer`:

- создаёт WGL context на Tk `OpenGLViewport`;
- получает OpenGL функции, включая `glGenBuffers`, `glBindBuffer`, `glBufferData`;
- создаёт VBO и два EBO: triangle EBO и line EBO;
- загружает данные при смене record или visual state;
- рисует `GL_TRIANGLES` и `GL_LINES`;
- использует depth test;
- строит projection/modelview matrices под текущие yaw/pitch/roll/zoom/pan.

Текущий renderer использует fixed-function OpenGL pipeline. Это намеренно простой и переносимый для старого стиля путь. Следующий естественный шаг — shader pipeline с VAO, но текущая версия уже выполняет цель переноса моделей на GPU через VBO/EBO.

## Цвета

Поддерживаются три diagnostic shade modes:

- `stored` — яркость берётся из `shade_level`, с учётом `material_or_flags`;
- `lit` — простое flat lighting от фиксированного `light_dir`;
- `ega` — EGA-like palette mapping для проверки shade bytes.

В solid mode polygon edges тёмные, чтобы читался контур.

В wireframe mode edges светлые, чтобы модель была видна на чёрном фоне.

## Transform matrices частей

Текущие `group_matrices` — это проверяемая структура `group_index -> 4x4 identity matrix`. Она нужна как стабильный контракт GPU pipeline для дальнейшего перехода к настоящим local transforms/pivots частей.

Важно: на текущем этапе viewer сохраняет поведение старой сборки records. Он ещё не реконструирует полный parent-child graph меха из оригинального runtime.

## Экспорт OBJ

`Export current OBJ...` экспортирует текущий объект:

- для runtime/composite records используется локальный `write_loaded_obj()`;
- для legacy records используется exporter из `mw1989_tbl_to_mesh_v3.py`.

Для MWA animation mode имя по умолчанию содержит номер frame.

## Известные ограничения

- WGL-контекст сейчас рассчитан на Windows.
- Renderer использует fixed-function OpenGL, не shader/VAO pipeline.
- Part/group matrices пока identity; настоящие pivots/local transforms ещё предстоит восстановить.
- `depth_order` остался как UI/debug state, но GPU path фактически полагается на depth buffer.
- `show_vertices` и `show_axes` хранятся в состоянии UI, но отдельная GPU-отрисовка точек/осей пока не реализована после удаления software canvas overlay.
- Auto-assembly и MWA preview — диагностические эвристики, не финальная игровая сборка мехов.

## Типовой рабочий цикл

1. Запустить viewer на `JENPCK.TBL`, `LOCPCK.TBL`, `MARPCK.TBL` или `SHAPCK.TBL`.
2. Проверить, что overlay показывает `backend=gpu-vbo-ebo`.
3. Вращать модель мышью и смотреть render time.
4. Нажать `W` и проверить светлый wireframe.
5. Нажать `S` и сравнить `stored`, `lit`, `ega`.
6. Собрать MWA frame из нескольких records и нажать `Play MWA`.
7. Перед изменениями renderer запускать `python validate_gpu_viewer.py`.

## Критерии готовности текущей версии

На момент этой документации выполнено:

- импорт viewer модулей проверяется;
- runtime import `TBL` проверяется;
- triangulation проверяется;
- colors проверяются;
- transform matrices частей проверяются;
- MWA animation preview собирается в GPU batch;
- живой GPU smoke test открывает окно и отрисовывает frame;
- wireframe edges исправлены на светлый цвет.
