# MW_PICS odd-width rendering fix

## Problem

The manual-viewer export and the integrated extractor export disagreed for records such as:

- `known_006_000289DE_header_161x70`
- `known_011_000344BB_header_135x187`
- `known_023_000439A0_header_129x36`
- `known_025_00044BFB_header_129x36`
- `known_027_00045E56_header_129x36`
- `known_035_000492B6_header_55x49`
- `known_036_0004981B_header_55x49`
- `known_037_00049D80_header_55x49`
- `known_058_00061148_header_91x104`
- `known_064_0006FDF4_header_149x127`

All of these have odd widths and `nibble_phase=0`.  The extractor was treating
4bpp pixels as one continuous nibble stream across the whole image.  That works
for even widths, but for odd widths it causes the final padding nibble of a row
to become the first pixel of the next row.  The visible symptom is a horizontal
one-nibble shear/shift accumulating down the image.

## Correct behavior

For MW_EGA packed 4bpp records, scanlines are byte-aligned:

```text
row_stride = (width + 1) // 2
row y starts at pixel_data + y * row_stride
```

For an odd width, the low nibble of the last byte in each row is padding.

## Fix

- `pics_4bpp_to_rgb()` now renders by row using `row_stride`.
- `pics_4bpp_to_cga_rgb()` uses the same row alignment.
- `render_4bpp_with_profile_phased()` no longer falls back to the old continuous
  renderer for `nibble_phase=0`; phase-0 manual records still need row alignment.

## Verification

After the fix:

```text
Exported 1 BIN file(s), rendered 67 image(s) and 67 corrected image(s)
covered decoded bytes: 99.798042%
unknown decoded bytes: 0.201958%
```

The corrected `known_006_000289DE_header_161x70` matches the provided manual
export spatially.
