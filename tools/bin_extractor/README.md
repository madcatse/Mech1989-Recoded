# BIN Extractor

`bin_extract.py` exports MechWarrior (1989) `MW_*PICS.BIN` resources.
The tool is intended for the original MS-DOS/EGA data files and writes decoded
streams, raw entry payloads, rendered images, manifests, and coverage/audit CSVs.

These files are not the same as nested `BIN:` chunks inside `.BMP` or `.SCR`
resources. The observed `MW_*PICS.BIN` files use a nibble-RLE post-compressor:

```text
literal nibble: any value except F
run token:      F, count_minus_one, value
```

The decompressor reads source nibbles low-then-high from each byte and repacks
decoded nibbles high-then-low into the output byte stream.

## Two decoded layouts

### Table-based `MW_*PICS.BIN` files

Most of the smaller files decode to an offset table followed by a `u16le` size
table. Active entries are either 48-byte DAC palettes or packed 4bpp images:

```text
u16le width
u16le height
packed 4bpp pixels, high nibble first
optional trailing bytes
```

For these files, the extractor exports every active table entry and renders all
recognized image entries.

### Special case: `MW_PICS.BIN`

`MW_PICS.BIN` uses the same nibble-RLE compression, but it does **not** have the
normal offset/size table. It is handled by an explicit forensic/manual map of
verified image records. The map combines the earlier confirmed fullscreen
records with the manual marks from `mw_pics_manual_marks_saved.json`.

The current default export for the analyzed file contains **67 images**:

| Category | Count | Notes |
|---|---:|---|
| scene | 7 | fullscreen and large scenic panels |
| image | 10 | large mech/vehicle illustrations from manual marks |
| mech | 8 | 68x68 mech portraits/sprites |
| banner | 5 | 129/130x36 nameplate/banner records |
| emblem | 10 | house/company emblems and smaller badges |
| portrait | 24 | 90/91x104 character portraits |
| panel | 3 | 148/149x127 panels |

The full per-record list is written to `out/MW_PICS/known_records.csv`.

Important details:

- Strict runtime-style records store `u16le width`, `u16le height`, then packed
  4bpp pixels.
- Some records are raw/unheaded or manual-viewer records. They are described by
  `offset`, `width`, `height`, `header_size`, and `nibble_phase` instead of by a
  validated runtime header.
- `nibble_phase=1` means rendering starts from the low nibble of the first
  pixel byte. This was added because several manually identified images line up
  only at half-byte phase.
- `header_size=4` on a manual record means "skip four prefix bytes before pixel
  data"; it does **not** necessarily mean those four bytes are a valid
  `u16 width/u16 height` header.
- `validate_header=False` records are exported exactly as the manual viewer
  displayed them. This is intentional: the file is not a clean self-describing
  container.
- `0x0001799B` remains a full raw 320x200 frame. Treating its first four bytes
  as a header misreads it as a small false-positive candidate.

The previous `320x73` diagnostic strip at `0x000273A9` has been superseded by
the more precise manual marks beginning at `0x000273AE` and `0x000289DE`.

## Quick Start

From the project root:

```powershell
python "Sorted Original Files\BIN\bin_extract.py" --force
```

This reads all `.BIN` files in:

```text
Sorted Original Files/BIN
```

and writes decoded streams, raw entries, rendered images, manifests, and summary
files to:

```text
Sorted Original Files/BIN/out
```

Export one file:

```powershell
python "Sorted Original Files\BIN\bin_extract.py" "Sorted Original Files\BIN\MW_PICS.BIN" --image-format png --force
```

## Output Layout

For a table-based file such as `MW_CPICS.BIN`, the output is:

```text
out/MW_CPICS/
  MW_CPICS.decoded.bin
  entries.csv
  manifest.json
  entries_raw/*.bin
  images/*.bmp|png|ppm
  images_corrected/*.bmp|png|ppm
```

For `MW_PICS.BIN`, the output additionally includes:

```text
out/MW_PICS/
  MW_PICS.decoded.bin
  known_records.csv
  coverage.csv
  uncovered_raw_candidates.csv
  manifest.json
  mw_pics_images/*.bmp|png|ppm
  mw_pics_images_corrected/*.bmp|png|ppm
```

Top-level files:

```text
out/summary.json
out/summary.csv
```

summarize decoded sizes, image counts, raw candidate counts, and byte coverage.

## Palette Modes

- `--video-mode auto` - default; uses BIN palette unless `--palette` is set, in
  which case it uses PAL:EGA. If `--raw-palette` is set, it uses raw mode.
- `--video-mode bin` - uses the first active 48-byte BIN DAC palette, falling
  back to standard EGA colors.
- `--video-mode raw` - uses a user-supplied 48-byte RGB DAC palette from
  `--raw-palette`.
- `--video-mode ega` - maps 4bpp indexes through a selected `PAL:EGA` bank.
- `--video-mode cga` - maps 4bpp indexes through a selected `PAL:CGA` table and
  renders the resulting 2bpp CGA pattern.

`--game-palette-remap` applies the project-observed source-index to runtime-index
EGA remap. Corrected output is enabled by default.

For `MW_PICS.BIN`, if no embedded palette and no neighboring `MW_GPICS.BIN`
palette are available, `mw_pics_images_corrected/` falls back to the standard
EGA palette plus the verified game remap. This makes the special `MW_PICS.BIN`
records usable even when only `MW_PICS.BIN` is available.

Available CGA monitor palettes are:

```text
palette0-low, palette0-high, palette1-low, palette1-high, index-preview
```

## Useful Commands

Render larger images for inspection:

```powershell
python "Sorted Original Files\BIN\bin_extract.py" --scale 2 --force
```

Write PNG files instead of dependency-free BMP files:

```powershell
python "Sorted Original Files\BIN\bin_extract.py" --image-format png --force
```

PNG output requires Pillow. BMP and PPM output are written without external
packages.

Scan for raw image-like records as a forensic aid:

```powershell
python "Sorted Original Files\BIN\bin_extract.py" "Sorted Original Files\BIN\MW_PICS.BIN" --scan-raw --force
```

Scan only ranges that are not covered by verified entries / known records:

```powershell
python "Sorted Original Files\BIN\bin_extract.py" "Sorted Original Files\BIN\MW_PICS.BIN" --scan-coverage-gaps --force
```

Add or test a manual-viewer JSON mark file without editing the embedded map:

```powershell
python "Sorted Original Files\BIN\bin_extract.py" "Sorted Original Files\BIN\MW_PICS.BIN" --manual-marks "mw_pics_manual_marks_saved.json" --image-format png --force
```

Duplicate manual records are de-duplicated by offset, dimensions, header size,
and nibble phase.

Render an ad-hoc raw record by decoded-stream offset:

```powershell
python "Sorted Original Files\BIN\bin_extract.py" "Sorted Original Files\BIN\MW_PICS.BIN" --raw-offset 0x280 --force
```

This ad-hoc path still interprets the offset as a normal headered image record.
For confirmed `MW_PICS.BIN` raw/unheaded records, use the default `MW_PICS`
forensic export instead.

Render through a PAL file's EGA bank:

```powershell
python "Sorted Original Files\BIN\bin_extract.py" "Sorted Original Files\BIN\MW_GPICS.BIN" --palette "Sorted Original Files\PAL\ARCTIC.PAL" --video-mode ega --palette-bank 0 --palette-interpretation low --force
```

Render through a PAL file's CGA lookup table:

```powershell
python "Sorted Original Files\BIN\bin_extract.py" "Sorted Original Files\BIN\MW_GPICS.BIN" --palette "Sorted Original Files\PAL\ARCTIC.PAL" --video-mode cga --cga-table 0 --cga-palette palette1-high --force
```

In CGA mode, each source 4bpp pixel is expanded through the selected `PAL:CGA`
table into an 8-pixel 2bpp CGA pattern. This intentionally makes the output
image eight times wider before any `--scale` factor is applied.

## Coverage and completeness

`coverage.csv` and `manifest.json` report byte coverage in the **decoded**
stream, not in the compressed `.BIN` file.

For the analyzed `MW_PICS.BIN` copy after integrating the manual marks:

```text
compressed size: 328,171 bytes
decoded size:    498,619 bytes
known/manual images exported: 67
image-covered decoded bytes: 497,612 bytes (99.798042%)
unknown / gap decoded bytes: 1,007 bytes (0.201958%)
unknown decoded ranges: 67
manual marks integrated: 62
nibble_phase=1 records: 30
```

Most remaining gaps are tiny separators/prefix/padding between records plus the
initial pre-image area before `0x00000280`. They are still listed in
`coverage.csv` for audit. The extractor also supports `--scan-coverage-gaps`,
but after this manual integration those gap candidates should be treated mainly
as false-positive checks, not as confirmed graphics.

Because manual/half-nibble records are not fully self-describing, the extractor
still cannot mathematically prove that no arbitrary hidden image exists at an
unmarked byte offset. The practical claim is now stronger: all known early
records plus all visually meaningful manual marks supplied in
`mw_pics_manual_marks_saved.json` are exported by default.

## Odd-width row alignment fix

`MW_EGA` image data is byte-aligned per scanline. For odd-width images, the
unused low nibble at the end of each row is padding and must be skipped before
rendering the next row. Earlier manual-integration builds used a continuous
pixel counter for `nibble_phase=0`, which sheared odd-width images such as
`161x70`, `135x187`, `129x36`, `55x49`, `91x104`, and `149x127` records.

The current extractor renders all normal and manual records row-by-row using
`row_stride = (width + 1) // 2`. Manual `nibble_phase=1` records still begin on
the low nibble, but they also keep row alignment consistent with the manual
viewer output.
