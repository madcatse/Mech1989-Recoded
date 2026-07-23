# MechWarrior 1989 GPU Viewer

GPU-backed version of the MechWarrior 1989 `*PCK.TBL` model viewer.

The viewer keeps the old Tk UI, record browser, OBJ export, and MWA preview editor, but the viewport now renders through OpenGL VBO/EBO buffers.

Full Russian documentation:

```text
GPU_VIEWER_GUIDE_RU.md
```

## Run

```bat
run_viewer.bat "..\Sorted Original Files\TBL\viewer8\JENPCK.TBL"
```

or:

```bat
python mw1989_viewer.py --runtime --backend gpu "..\Sorted Original Files\TBL\viewer8\JENPCK.TBL"
```

## Validate Without Opening A Window

```bat
python validate_gpu_viewer.py
```

The validation checks importer output, triangulation, EBO ranges, colors, wireframe edge color, part/group transform matrices, and MWA animation preview conversion into a GPU batch.
