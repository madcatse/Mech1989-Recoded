# MW_*PICS.BIN format notes

Status: partially confirmed from `MW_CPICS.EXE` disassembly and decoded files.

Related disassembly:

- `research/disassembly/MW_CPICS.EXE/MW_CPICS.EXE.c`
- `FUN_1000_046e`: reads IFF/ILBM chunks `BMHD`, `CMAP`, `BODY` and converts image data into EGA planar records.
- `FUN_1000_05d8`: decodes ILBM BODY PackBits-like rows when BMHD compression is non-zero.
- `FUN_1000_069f`: post-compresses generated `MW_CPICS.BIN`.

`MW_CPICS.EXE` workflow:

1. Allocates three 64K DOS blocks:
   - file buffer
   - picture buffer
   - decompression buffer
2. Creates `MW_CPICS.BIN`.
3. Writes a provisional 0x12C-byte header.
4. Reads source `.LBM` files listed in the embedded path table.
5. Converts IFF/ILBM image data to EGA planar image blocks.
6. Writes palette/image blocks to `MW_CPICS.BIN`.
7. Seeks back to the start and rewrites the populated header.
8. Reopens and post-compresses `MW_CPICS.BIN` with nibble-RLE.

Post-compression: nibble-RLE

- Input is treated as nibbles in high-nibble, then low-nibble order per byte.
- Output bytes store the first compressed nibble in the low half and the second compressed nibble in the high half.
- Token format:
  - literal: any nibble except `0xF`
  - run/escape: `0xF`, `count_minus_one`, `value`
- Runs of 4..16 equal nibbles are encoded as run tokens.
- A literal `0xF` is encoded as `0xF, 0x0, 0xF`.

Extractor support:

- `mw_extract decode-pics-bin INPUT --out OUTPUT`
- `mw_extract map-pics-bin INPUT --json MAP.json --csv MAP.csv`
- `mw_extract extract-pics-bin INPUT --out OUTDIR`
- `mw_extract render-pics-bin INPUT --out OUTDIR`
- `mw_extract scan-pics-raw INPUT --csv CANDIDATES.csv`
- `mw_extract render-pics-raw INPUT --offset 0x... --out OUTDIR`
- Decoder name in metadata: `mw_pics_nibble_rle`
- Generated summary:
  - `research/extract/decoded_pics_bin/summary.csv`
  - `research/extract/decoded_pics_bin/entries.csv`
  - `research/extract/decoded_pics_bin/*.map.json`
  - `research/extract/decoded_pics_bin/*.entries.csv`
  - `research/extract/rendered_pics_bin/summary.csv`

Decoded header candidate:

- Most smaller `MW_*PICS.BIN` files decode to a header containing an offset table
  followed by a size table.
- The first offset is also the header size.
- Header slot count is:

```text
entry_count = header_size / 6
```

- Offset table entries are 4 bytes each, but not plain `u32le`; they are stored as:

```text
u16le high_word
u16le low_word
offset = (high_word << 16) | low_word
```

- The size table follows immediately after the offset table:

```text
u16le size[entry_count]
```

- Examples after decoding:
  - `MW_CPICS.BIN`: header `0x12C` bytes, 50 slots, 27 active entries.
  - `MW_2PICS.BIN`: header `0x78` bytes, 20 slots, 7 active entries.
- Current decoded entry table:
  - `research/extract/decoded_pics_bin/entries.csv`
- Current aggregate totals for recognized smaller `MW_*PICS.BIN` files:
  - 320 header slots.
  - 99 active entries.
  - 221 empty slots.
  - 0 invalid entries.

Entry payload candidates:

- A 48-byte active entry is a 16-color palette:

```text
16 * RGB
component range appears to be 0..63 DAC-style values
```

- Image entries currently match:

```text
u16le width
u16le height
u4 packed_pixels[row_stride_bytes * height * 2]
u8[usually 5] trailing_unknown

row_stride_bytes = ceil(width / 2)
```

- Pixel bytes are interpreted as high nibble first, low nibble second.
- The converted pixel data starts immediately after `width` and `height`.
- Earlier notes treated the surplus bytes as a leading 5-byte header, but
  `MW_CPICS.EXE` writes pixels from offset `+4`; the surplus belongs after the
  row-padded pixel area.
- Rendered examples:
  - `MW_1PICS.BIN`: two 320x200 images.
  - `MW_CPICS.BIN`: 26 images, mostly 68x68 crew portraits.
  - `MW_GPICS.BIN`: 46 small UI/gameplay image entries.

Open questions:

- `MW_PICS.BIN` decodes with the same nibble-RLE but does not match the smaller-file header heuristic.
  It appears to contain raw image records in-stream rather than the smaller-file
  offset/size header.
- Several decoded offset tables contain zero holes; these appear to be unused slots.
- This nibble-RLE is not the same as `BMP:/BIN:` codec 2. Direct tests against codec 2 BMP payloads produce output much shorter than the declared decoded size.
- The meaning of the trailing bytes is not confirmed yet.

`MW_PICS.BIN` partial mapping:

- `mw_extract render-pics-raw` can render raw image records by decoded offset.
- `mw_extract scan-pics-raw` can produce a forensic candidate table, but it is intentionally
  heuristic and can include false positives when image-like byte patterns resemble dimensions.
- Confirmed rendered offsets:
  - `0x00000280`: 320x200
  - `0x00007F89`: 320x200
  - `0x0000FC92`: 320x200
  - `0x0001F6A5`: 320x200
  - `0x000680D5`: 320x200
- Additional rendered candidates:
  - `0x0001799B`: 196x28 with height high-bit flag set.
  - `0x0002A320`: 273x16.
  - `0x0002A470`: 273x16.
  - `0x0002A5C0`: 273x16.
- Generated files:
  - `research/extract/rendered_pics_raw/MW_PICS/*.ppm`
  - `research/extract/rendered_pics_raw/MW_PICS_selected/*.ppm`
  - `research/extract/rendered_pics_raw/summary.csv`
  - `research/extract/rendered_pics_raw/MW_PICS_candidates.csv`
  - `research/extract/rendered_pics_raw/MW_PICS_candidates_interesting.csv`
- The first three records are sequential when accounting for a 5-byte post-pixel gap.
  The next area begins with `C4 00 1C 80`, which may represent a flagged or compressed
  variant record rather than the simple raw 4bpp layout.
