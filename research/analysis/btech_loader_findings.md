# BTECH.EXE loader findings

This note ties current extractor findings back to the Ghidra decompiler output for `BTECH.EXE`.

## Embedded resource tables

- `research/analysis/btech_resource_tables.csv` records fixed byte ranges in `BTECH.EXE` that contain resource filenames and tag-layout strings.
- `research/analysis/btech_resource_usage.csv` cross-references those embedded filenames against `Original/` and the current `mw_extract map` output.
- All 25 concrete resource filenames recovered from these ranges exist in `Original/` and have extractor maps.

## Resource manager candidates

The strongest resource-loader cluster in `research/disassembly/BTECH.EXE/BTECH.EXE.c` is around:

- `FUN_2000_2fdf`
- `FUN_2000_31b9`
- `FUN_2000_3233`
- `FUN_2000_3445`
- `FUN_2000_3971`

Observed behavior:

- `FUN_2000_2fdf` opens a file handle or accepts an existing one, then calls `FUN_2000_31b9` / `FUN_2000_3233` with tag-layout string addresses.
- Calls with tag layout addresses near `0x286b`, `0x2874`, `0x287d`, `0x2886`, `0x288f` line up with embedded sound/music tag layouts such as `SND:IBM:`, `SND:TAN:`, `SND:8SV:`, `SND:ROL:`, and `SND:ITM:`.
- `FUN_2000_3233` seeks to a tag path, reads a 32-bit size field, allocates memory, copies the payload in chunks, and records the allocation in a linked resource table.
- `FUN_2000_31b9` calls `FUN_2000_3233`, then converts child offset tables into absolute in-memory pointers by adding the loaded base address.
- `FUN_2000_3971` walks those linked allocations and frees them.

This supports the current extractor model:

- resource chunks are addressed by tag paths rather than by fixed file extension alone;
- chunk length is stored immediately after the 4-byte ASCII tag;
- nested/child entries are loaded as payload blocks and then patched into absolute pointers by the original loader.

## BTECH BIN samples

`research/extract/btech_bin_samples/summary.csv` contains 19 `BIN:` samples from resources named inside `BTECH.EXE`.

- codec 1: 1 sample, `point.bmp`
- codec 2: 18 samples
- BMP codec 2 targets are confirmed by `INF:` dimensions and packed 4bpp byte size.
- SCR codec 2 targets are confirmed as 32000-byte 320x200 packed 4bpp buffers.

The compression transform for `BIN` codec 2 is now identified as an LZW-style LSB-first stream. See `research/analysis/codec2_lzw_findings.md`.

## Next reverse target

Continue from the code paths that consume the memory returned by `FUN_2000_3233`, especially calls that branch on the first byte of a `BIN:` payload:

- byte `0x01`: known RLE-like codec implemented by `mw_extract decode-bin`
- byte `0x02`: unresolved codec used by combat BMP/SCR resources

The original decoder likely sits outside the tag loader itself: `FUN_2000_3233` appears to load raw payloads, while later graphics routines interpret the local `BIN` header and expand the compressed body.
