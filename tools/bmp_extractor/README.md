# BMP Extractor

`bmp_extract.py` exports MechWarrior `.BMP` resources from `Sorted Original Files/BMP`.
These files are not Windows BMP images. They are tagged containers:

```text
BMP:
  INF:  u16le count, then width[count], then height[count]
  BIN:  u8 codec, u32le decoded_size, encoded bytes
```

The tool decodes the observed `BIN` codecs:

- codec `1`: simple literal/run RLE;
- codec `2`: LZW-like stream, LSB-first, 9..12 bit codes.

Decoded image data is exported as packed 4bpp pixels: the high nibble is the first pixel, the low nibble is the second pixel.

## Quick Start

From the project root:

```powershell
python BMP\bmp_extract\bmp_extract.py --force
```

This reads all files in:

```text
Sorted Original Files/BMP
```

and writes PNG files plus manifests to:

```text
BMP/bmp_extract/out
```

## Useful Commands

Export every BMP, scaled for easier viewing:

```powershell
python BMP\bmp_extract\bmp_extract.py --display-scale --xscale 4 --yscale 4 --force
```

Export PNG, PPM, and standard Windows BMP images:

```powershell
python BMP\bmp_extract\bmp_extract.py --image-format all --force
```

Export one source file:

```powershell
python BMP\bmp_extract\bmp_extract.py "Sorted Original Files\BMP\POINT.BMP" --out BMP\bmp_extract\out_point --force
```

Also write raw chunks and decoded 4bpp records:

```powershell
python BMP\bmp_extract\bmp_extract.py --extract-raw --force
```

Render with one of the tagged terrain palette banks:

```powershell
python BMP\bmp_extract\bmp_extract.py --palette "Sorted Original Files\PAL\ARCTIC.PAL" --palette-bank 0 --force
```

## Output Layout

For each source file the tool creates a folder:

```text
BMP/bmp_extract/out/JEN/
  chunks.csv
  manifest.json
  JEN_000_152x193.png
```

When `--extract-raw` is used, the folder also contains:

```text
JEN.INF.bin
JEN.BIN.payload.bin
JEN.BIN.decoded.bin
records_raw/*.4bpp
```

The top-level `summary.json` lists all processed files, codecs, decoded sizes, and rendered image counts.

## Notes

The default palette is the standard 16-color EGA palette. Palette decoding for `.PAL` files follows the current project finding named `ega_pair_banks_tentative`; it is useful for comparison, but exact runtime palette semantics still need final confirmation.
