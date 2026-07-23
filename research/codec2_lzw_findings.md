# BIN codec 2 findings

`BIN` codec 2 is now implemented in `tools/mw_extract.py`.

## Confirmed compression

Codec 2 is an LZW-style bitstream:

- codes are packed least-significant-bit first;
- initial code width is 9 bits;
- byte literals are codes `0..255`;
- the first dynamic dictionary code is `257`;
- code `256` is not treated as a clear or end marker in the confirmed assets;
- code width grows up to 12 bits;
- decoding stops at bitstream EOF;
- decoded size must match the `BIN:` local decoded-size dword.

The implementation is `decompress_codec2()` in `tools/mw_extract.py`.

## Confirmed decoded layout

For tagged `BMP:` and `SCR:` graphics, the decoded payload is packed 4bpp chunky image data:

- one byte stores two pixels;
- high nibble is the left/even pixel;
- low nibble is the right/odd pixel;
- row stride is `ceil(width / 2)`.

This replaces the earlier EGA-planar assumption. The old assumption produced recognizable images with vertical stripe artifacts because the decoded size can coincidentally equal both:

- `width * height / 2` packed 4bpp, and
- `ceil(width / 8) * height * 4` planar EGA,

when widths are multiples of 8.

## Extractor support

Implemented commands:

- `decode-bin`: now supports codec 1 and codec 2.
- `render-bmp`: decodes `BIN:` and renders `INF:` records as packed 4bpp.
- `render-scr`: decodes full-screen `.SCR` resources and renders packed 4bpp by default when the decoded size matches.

Generated preview folders:

- `research/extract/rendered_btech_bmp_png_display/`
- `research/extract/rendered_btech_scr_png_display/`

Generated review artifacts:

- `research/analysis/rendered_btech_preview_inventory.csv`
- `research/analysis/rendered_btech_bmp_contact_sheet.png`
- `research/analysis/rendered_btech_scr_contact_sheet.png`

## Remaining palette work

The BTECH combat graphics currently render with the default EGA palette unless another palette is explicitly applied in future work.

The terrain palettes in `ARCTIC.PAL`, `DESERT.PAL`, `DMGPAL.PAL`, and `TROPIC.PAL` are tagged `PAL:` containers with `EGA:` and `CGA:` payloads. Their exact palette-register semantics still need a separate confirmation pass.
