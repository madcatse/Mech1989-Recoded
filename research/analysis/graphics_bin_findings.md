# Graphics BIN findings

Current evidence from `mw_extract` maps and summaries:

- Tagged graphic files use parent `BMP:` chunks with nested `INF:` and `BIN:` chunks.
- `INF:` payload decodes as:

```text
u16le count
u16le widths[count]
u16le heights[count]
```

- `BIN:` payload begins with:

```text
u8 codec_candidate
u32le decoded_size
encoded_bytes...
```

- For every analyzed `*.BMP` file with `INF:` + `BIN:`, `decoded_size` exactly equals:

```text
sum(ceil(width / 8) * height) * 4
```

This is consistent with an EGA-style 4-plane bitmap buffer: one packed 1bpp plane per EGA bitplane.

Observed codecs:

- `codec_candidate = 2` for most graphics and all `*.SCR` screen chunks.
- `codec_candidate = 1` for `POINT.BMP`.

Codec 1 status:

- Implemented in `mw_extract decode-bin`.
- Command byte format:
  - `command & 0x80 != 0`: repeat the next byte `command & 0x7f` times.
  - `command & 0x80 == 0`: copy the next `command` literal bytes.
- Verified on `POINT.BMP`: encoded body `235` bytes decodes to `376` bytes.
- Output artifact:
  - `research/extract/decoded/POINT.BMP.BIN.codec1.decoded.bin`
- `POINT.BMP` decoded buffer has been rendered as EGA planar graphics:
  - `research/extract/rendered/POINT/POINT_000_16x10.ppm`
  - `research/extract/rendered/POINT/POINT_001_24x16.ppm`
  - `research/extract/rendered/POINT/POINT_002_16x13.ppm`

EGA rendering assumption:

- Record order follows the `INF:` dimensions.
- Each record is four sequential 1bpp planes.
- Per-plane row stride is `ceil(width / 8)` bytes.
- Color index is assembled from plane bits as bit 0..3.

Important examples:

- `BAT.BMP`: `152x193`, declared decoded size `14668`.
- `DIGITS.BMP`: 44 records, declared decoded size `1024`.
- `PROMPT.BMP`: `112x27`, declared decoded size `1512`.
- `SM_MECHS.BMP`: 9 records of `56x49`, declared decoded size `12348`.
- `SCR:` files have nested `BIN:` chunks declaring decoded size `32000`, matching `320x200` EGA 4-plane screen data.

Next reverse-engineering target:

Identify codec `2` decompression. Start with small payloads:

- `DOTS.BMP`: codec 2, encoded body 24 bytes, decoded size 72 bytes.
- `DIGITS.BMP`: codec 2, encoded body 249 bytes, decoded size 1024 bytes.

These are small enough to inspect manually and compare against expected EGA plane sizes.

Codec 2 status:

- Codec 2 is now identified as an LZW-style LSB-first stream.
- The decoded graphics payload is packed 4bpp, not EGA planar.
- Probe artifacts:
  - `research/analysis/codec2_code_preview.csv`
  - `research/analysis/codec2_probe_results.csv`
  - `research/analysis/codec2_lzw_probe_results.csv`
  - `research/analysis/codec2_lzw_probe_best.csv`
  - `research/analysis/codec2_lzw_findings.md`
- `mw_extract decode-bin`, `render-bmp`, and `render-scr` now support codec 2.

The next graphics task is confirming the exact semantics of `PAL:EGA:` payloads for terrain/combat palettes.
