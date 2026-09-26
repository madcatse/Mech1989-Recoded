# BTECH BIN samples

Extracted BIN payload samples for resources whose filenames are embedded in `BTECH.EXE`.

- BIN samples: 19
- Codec counts: 1: 1, 2: 18

Each `*.payload.bin` starts with the local BIN header: one codec byte and a little-endian decoded-size dword.
Each `*.body.bin` removes that five-byte header and contains only the compressed byte stream.

## Samples

| resource | codec | decoded | body | records | dimensions | target |
|---|---:|---:|---:|---:|---|---|
| `loc.bmp` | 2 | 14668 | 2998 | 1 | 152x193 | inf_packed_4bpp |
| `jen.bmp` | 2 | 14668 | 2752 | 1 | 152x193 | inf_packed_4bpp |
| `pho.bmp` | 2 | 14668 | 3542 | 1 | 152x193 | inf_packed_4bpp |
| `sha.bmp` | 2 | 14668 | 3829 | 1 | 152x193 | inf_packed_4bpp |
| `rif.bmp` | 2 | 14668 | 4258 | 1 | 152x193 | inf_packed_4bpp |
| `war.bmp` | 2 | 14668 | 3649 | 1 | 152x193 | inf_packed_4bpp |
| `mar.bmp` | 2 | 14668 | 3289 | 1 | 152x193 | inf_packed_4bpp |
| `bat.bmp` | 2 | 14668 | 4117 | 1 | 152x193 | inf_packed_4bpp |
| `prompt.bmp` | 2 | 1512 | 259 | 1 | 112x27 | inf_packed_4bpp |
| `point.bmp` | 1 | 376 | 235 | 3 | 16x10; 24x16; 16x13 | inf_packed_4bpp |
| `sm_mechs.bmp` | 2 | 12348 | 3005 | 9 | 56x49; 56x49; 56x49; 56x49; 56x49; 56x49; 56x49; 56x49; 56x49 | inf_packed_4bpp |
| `struts.bmp` | 2 | 3404 | 1056 | 12 | 24x15; 24x15; 40x30; 40x30; 80x9; 16x14; 16x14; 16x14; 16x14; 32x27; 32x28; 104x3 | inf_packed_4bpp |
| `cockpit.bmp` | 2 | 2232 | 688 | 12 | 8x7; 8x7; 8x3; 8x3; 16x9; 16x9; 48x11; 48x15; 40x19; 48x11; 48x15; 40x19 | inf_packed_4bpp |
| `hud_cyan.bmp` | 2 | 480 | 173 | 12 | 16x5; 16x5; 16x5; 16x5; 16x5; 16x5; 16x5; 16x5; 16x5; 16x5; 16x5; 16x5 | inf_packed_4bpp |
| `hud_nums.bmp` | 2 | 480 | 173 | 12 | 16x5; 16x5; 16x5; 16x5; 16x5; 16x5; 16x5; 16x5; 16x5; 16x5; 16x5; 16x5 | inf_packed_4bpp |
| `light.scr` | 2 | 32000 | 4682 |  |  | packed_4bpp_320x200 |
| `medium.scr` | 2 | 32000 | 4138 |  |  | packed_4bpp_320x200 |
| `heavy.scr` | 2 | 32000 | 3454 |  |  | packed_4bpp_320x200 |
| `status.scr` | 2 | 32000 | 1572 |  |  | packed_4bpp_320x200 |
