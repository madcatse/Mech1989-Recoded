# Codec 2 probe notes

Scope:

- Samples: `DOTS.BMP`, `DIGITS.BMP`, `HUD_CYAN.BMP`, `BAT.BMP`, `PROMPT.BMP`.
- All samples are tagged `BMP:` resources with `INF:` dimensions and a `BIN:` chunk.
- `BIN:` header layout is still `u8 codec, u32le decoded_size, encoded_body`.

Generated artifacts:

- `codec2_code_preview.csv`: first 12 fixed-width codes for MSB/LSB bit order and 9/10/11/12-bit widths.
- `codec2_probe_results.csv`: model probes that matched at least one sample.

Current negative results:

- Simple LZSS-style probes found no parameter set that works across the sample group.
- Basic LZW-style length probes found no parameter set.
- Simple bit-RLE probes found no parameter set that works across the sample group.

False-positive warning:

- `DOTS.BMP` is only 24 encoded bytes and 72 decoded bytes.
- Two simplistic models can match its declared length exactly, but both fail on larger samples.
- Do not treat `DOTS.BMP` alone as proof of a codec layout.

Interesting leads:

- LSB 9-bit previews often show values around `257..267`, which looks dictionary-like.
- The LZW-like simulation does not validate that interpretation yet.
- Several large `152x193` mech BMP files share identical or near-identical early encoded bytes.
- The decoder is likely a better next target than further blind parameter search.

Recommended next steps:

1. Locate the codec 2 decoder in `MW_MAIN.EXE`, `BTECH.EXE`, or the picture helper executable.
2. Search for code that reads the `BIN:` header byte and the following 32-bit decoded size.
3. Compare call sites near strings such as `Decompression buffer allocated.` and resource-name tables.
4. Once a candidate routine is found, mirror it in a separate experimental decoder before promoting it into `mw_extract`.

Executable triage update:

- `MW_CPICS.EXE` has explicit strings for picture/file/decompression buffers and source `.LBM` paths.
- Its entry code sets `DS=CS`, resizes its DOS memory block, allocates three 64K blocks, and creates/writes `MW_CPICS.BIN`.
- A routine around `0x00066C` scans the loaded file buffer for IFF/Deluxe Paint chunks:
  - `BMHD`
  - `CMAP`
  - `BODY`
- This strongly suggests `MW_CPICS.EXE` is a picture filing/conversion utility for source art, not necessarily the runtime decoder for shipped `BMP:/BIN:` resources.
- `FUN_1000_069f` contains a confirmed nibble-RLE post-compressor for `MW_*PICS.BIN`.
- The nibble-RLE was tested against `BMP:/BIN:` codec 2 payloads and does not match their declared decoded sizes.
- `MW_MAIN.EXE` simple xrefs to `$Decompression buffer allocated.` land inside pointer/text-table-looking regions, not executable code. These xrefs are low confidence until a disassembly confirms a code reference.

Related artifacts:

- `mw_cpics_strings.csv`
- `mw_cpics_string_contexts.md`
- `mw_cpics_control_offsets.csv`
- `mw_cpics_control_contexts.md`
- `mw_cpics_entry_context_long.md`
- `mw_main_buffer_xrefs.csv`
- `mw_main_buffer_contexts.md`
- `mw_pics_bin_format.md`
