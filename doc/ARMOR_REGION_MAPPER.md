# MW_PICS Armor Region Mapper

This document describes `tools/armor_region_mapper.html` and the exported data
contract for agents that need to use the mapped mechbay armor regions.

## Purpose

`armor_region_mapper.html` is a local, single-file browser tool for marking which
parts of the MW_PICS mechbay mech images belong to BattleTech armor sections:

- `RA`: right arm
- `RL`: right leg
- `RT`: right torso
- `CT`: center torso
- `LT`: left torso
- `LL`: left leg
- `LA`: left arm

The tool is intended for these MW_PICS records:

- `known_007_0002A00D_header_phase1_111x182.bmp`
- `known_008_0002C7E6_header_phase1_125x165.bmp`
- `known_009_0002F08A_header_phase1_115x183.bmp`
- `known_010_00031A09_header_phase1_119x182.bmp`
- `known_011_000344BB_header_135x187.bmp`
- `known_012_0003766F_header_phase1_113x178.bmp`
- `known_013_00039E1A_header_phase1_121x161.bmp`
- `known_014_0003C480_header_phase1_125x181.bmp`

The mapper loads images from the corrected-palette folder first:

```text
Sorted Original Files/BIN/out/MW_PICS/mw_pics_images_corrected/
```

If a corrected image is missing, it falls back to:

```text
Sorted Original Files/BIN/out/MW_PICS/mw_pics_images/
```

## How To Use

Open this file in a browser:

```text
tools/armor_region_mapper.html
```

Workflow:

1. Select a mech image.
2. Select an armor section, such as `RA`.
3. Drag a rectangle on the image.
4. Press `Add to section`.
5. Repeat until all purple armor pixels for the section are covered.
6. Export either the current image JSON or all images JSON.
7. Optionally export a purple PNG preview or transparent purple PNG mask.

The zoom controls are visual only. They do not change exported coordinates.

## Exported Files

The JSON buttons produce files named like:

```text
mw_pics_armor_regions_known_008_0002C7E6_header_phase1_125x165.json
mw_pics_armor_regions_all.json
```

The PNG buttons produce files named like:

```text
mw_pics_purple_preview_known_008_0002C7E6_header_phase1_125x165.png
mw_pics_purple_mask_known_008_0002C7E6_header_phase1_125x165.png
```

`PNG preview` keeps the mech visible but darkens non-target pixels.
`PNG mask` is transparent except for target pixels drawn as bright purple.

## Coordinate Contract

Coordinates are always in source image pixels, not screen pixels.

The origin is the top-left corner:

```text
x grows to the right
y grows downward
```

Each rectangle has:

```text
x, y, w, h, x2, y2
```

`x2` and `y2` are exclusive bounds:

```text
inside = x >= rect.x && x < rect.x2 && y >= rect.y && y < rect.y2
```

This is important. The visible selection border is only UI decoration and must
not be counted as part of the exported area.

Neighboring rectangles may share a border. With exclusive `x2/y2`, this does not
double-count pixels when rectangles are placed edge-to-edge:

```text
rect A: x=0,  w=10, x2=10
rect B: x=10, w=8,  x2=18
```

For multiple rectangles in the same section, consumers should union the pixels.
Do not add duplicated pixels twice if two rectangles in the same section overlap.

If rectangles from different sections overlap in their interiors, prefer fixing
the mapping when the overlap is obvious. Small overlaps are currently acceptable
for the Mech Status implementation: assign the disputed pixels to one armor
section and verify visually during gameplay testing.

## Target Pixel Rule

The rectangles are not meant to recolor every pixel inside them. They define
where to look for armor pixels. The intended target pixels are source pixels
inside those rectangles that are purple/magenta in the corrected image.

The JSON includes this filter:

```json
{
  "name": "purple_source_pixels",
  "rule": "Inside each exported rectangle, armor pixels are source pixels that look purple/magenta.",
  "rgba_heuristic": {
    "r_min": 110,
    "g_max": 85,
    "b_min": 110,
    "max_abs_r_minus_b": 90,
    "alpha_nonzero": true
  }
}
```

Equivalent predicate:

```cpp
bool isPurpleArmorPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return a != 0 && r >= 110 && b >= 110 && g <= 85 && abs(int(r) - int(b)) <= 90;
}
```

If a later implementation works from indexed source pixels instead of RGBA, map
through the same corrected palette first, then apply the same predicate or an
equivalent palette-index test.

## JSON Shape

Top-level structure:

```json
{
  "schema": "mw_pics_armor_regions.v1",
  "coordinate_system": {
    "origin": "top-left",
    "units": "source pixels",
    "rectangles": "x,y,w,h with x2/y2 exclusive",
    "export_area": "only pixels inside each rectangle are counted; visual selection borders are not exported"
  },
  "target_pixel_filter": {
    "name": "purple_source_pixels"
  },
  "sections": [
    { "id": "RA", "name": "Right arm" }
  ],
  "images": {
    "known_008_0002C7E6_header_phase1_125x165.bmp": {
      "source": {
        "bin": "MW_PICS.BIN",
        "known_index": 8,
        "offset_hex": "0x0002C7E6",
        "filename": "known_008_0002C7E6_header_phase1_125x165.bmp",
        "width": 125,
        "height": 165,
        "image_dir": "../Sorted Original Files/BIN/out/MW_PICS/mw_pics_images_corrected/",
        "palette": "corrected palette"
      },
      "sections": {
        "RA": [
          { "id": "RA_001", "x": 0, "y": 0, "w": 10, "h": 20, "x2": 10, "y2": 20 }
        ],
        "RL": [],
        "RT": [],
        "CT": [],
        "LT": [],
        "LL": [],
        "LA": []
      }
    }
  }
}
```

Do not assume the sample rectangle above is a real mapping. It only documents the
shape.

## Consumer Algorithm

Recommended algorithm for using exported JSON:

1. Load the corrected-palette source BMP named by `source.filename`.
2. Decode it to RGBA in source pixel coordinates.
3. For each image and armor section, iterate all rectangles.
4. For each pixel inside each rectangle using exclusive `x2/y2`, test the purple
   armor predicate.
5. Add matching pixels to that section's mask.
6. Union duplicate pixels within the same section.
7. Detect interior overlaps between different sections and report them.
8. Use each final section mask to recolor or otherwise display armor integrity.

Pseudo-code:

```cpp
for (const ImageMapping& image : mappings) {
    RgbaImage source = loadCorrectedImage(image.source.filename);

    for (const Section& section : sections) {
        Mask& mask = masks[image.source.filename][section.id];

        for (const Rect& rect : image.sections[section.id]) {
            for (int y = rect.y; y < rect.y2; ++y) {
                for (int x = rect.x; x < rect.x2; ++x) {
                    Rgba px = source.pixel(x, y);
                    if (isPurpleArmorPixel(px.r, px.g, px.b, px.a)) {
                        mask.set(x, y);
                    }
                }
            }
        }
    }
}
```

## Validation Notes

Use the purple PNG mask or preview as a visual QA artifact. It should show only
the armor pixels that will be redistributed across armor sections.

When validating a mapping:

- Check that the image metadata says `palette: corrected palette`.
- Check that all seven sections have the expected marked areas.
- Check that white highlights, black outlines, gray internals, cockpit pixels,
  and background grid pixels are not included in the purple mask.
- Check that edge-to-edge rectangles do not leave a visible one-pixel gap unless
  that gap is intentional.

Current implementation note:

- `mechs_armor_parts/mw_pics_armor_regions_all.json` is the active handoff file
  for all eight playable Mech Status art records.
- The Warhammer mapping includes an added `LT` rectangle:
  `x=91, y=3, w=22, h=20`.

## Current Storage

The browser stores in-progress annotations in `localStorage` under:

```text
mw_pics_armor_region_mapper_v1
```

The durable handoff artifact is the exported JSON, not browser storage.
