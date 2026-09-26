# Codec 2 stream profile

Profiles compressed `BIN` codec 2 bodies from `research/extract/btech_bin_samples`.

- Samples: 18
- CSV: `research/analysis/codec2_stream_profile.csv`
- Byte frequency CSV: `research/analysis/codec2_byte_frequency.csv`

## Samples

| resource | target | decoded | body | ratio | entropy | high-bit % | zero % | first bytes |
|---|---|---:|---:|---:|---:|---:|---:|---|
| `loc.bmp` | inf_packed_4bpp | 14668 | 2998 | 4.8926 | 7.9105 | 50.47 | 0.73 | `99 02 46 0A 98 69 A0 40 82 06 0B 22 5C 78 B0 A1` |
| `jen.bmp` | inf_packed_4bpp | 14668 | 2752 | 5.3299 | 7.9123 | 49.78 | 0.69 | `99 22 65 1A 28 90 E0 C0 80 07 0B 22 34 C8 70 A1` |
| `pho.bmp` | inf_packed_4bpp | 14668 | 3542 | 4.1412 | 7.9184 | 48.64 | 0.73 | `99 02 66 08 98 69 A0 40 82 06 0B 22 5C 78 B0 A1` |
| `sha.bmp` | inf_packed_4bpp | 14668 | 3829 | 3.8308 | 7.9219 | 47.74 | 0.55 | `99 32 64 1A 28 90 E0 C0 80 07 0B 22 34 C8 70 A1` |
| `rif.bmp` | inf_packed_4bpp | 14668 | 4258 | 3.4448 | 7.9315 | 48.17 | 0.80 | `99 02 46 0A 98 69 A0 40 82 06 0B 22 5C 78 B0 A1` |
| `war.bmp` | inf_packed_4bpp | 14668 | 3649 | 4.0197 | 7.9214 | 48.01 | 0.66 | `99 22 65 1A 28 90 E0 C0 80 07 0B 22 34 C8 70 A1` |
| `mar.bmp` | inf_packed_4bpp | 14668 | 3289 | 4.4597 | 7.9250 | 49.32 | 0.70 | `99 02 46 0A 98 69 A0 40 82 06 0B 22 5C 78 B0 A1` |
| `bat.bmp` | inf_packed_4bpp | 14668 | 4117 | 3.5628 | 7.9389 | 48.00 | 0.87 | `99 32 64 1A 18 69 60 A6 82 04 0D 22 3C A8 B0 61` |
| `prompt.bmp` | inf_packed_4bpp | 1512 | 259 | 5.8378 | 7.1844 | 58.30 | 1.16 | `77 02 DE F9 F3 A3 E0 BF 83 08 13 2A 5C C8 B0 E1` |
| `sm_mechs.bmp` | inf_packed_4bpp | 12348 | 3005 | 4.1092 | 7.9025 | 48.22 | 0.67 | `AA 02 0A 1C 48 B0 20 41 50 00 0C CE D0 31 70 61` |
| `struts.bmp` | inf_packed_4bpp | 3404 | 1056 | 3.2235 | 7.7569 | 48.11 | 0.85 | `91 02 D6 A8 42 B0 CA 17 00 08 33 64 98 50 90 20` |
| `cockpit.bmp` | inf_packed_4bpp | 2232 | 688 | 3.2442 | 7.6287 | 48.26 | 1.31 | `88 02 22 52 A4 4A 15 2A 82 06 11 1E 2C B8 D0 A0` |
| `hud_cyan.bmp` | inf_packed_4bpp | 480 | 173 | 2.7746 | 6.7682 | 39.31 | 1.73 | `BB 76 2D D8 05 2B 20 00 58 0B 16 1C 44 78 30 E1` |
| `hud_nums.bmp` | inf_packed_4bpp | 480 | 173 | 2.7746 | 6.7480 | 41.04 | 1.73 | `FF FE 3D F8 07 2F 20 00 78 0F 1E 1C 44 78 30 E1` |
| `light.scr` | packed_4bpp_320x200 | 32000 | 4682 | 6.8347 | 7.9183 | 53.35 | 0.47 | `88 02 0A 1C 48 B0 A0 C1 83 08 13 2A 5C C8 B0 A1` |
| `medium.scr` | packed_4bpp_320x200 | 32000 | 4138 | 7.7332 | 7.9302 | 50.68 | 0.48 | `88 02 E2 08 48 10 D1 23 40 00 12 2A 5C C8 B0 A1` |
| `heavy.scr` | packed_4bpp_320x200 | 32000 | 3454 | 9.2646 | 7.9112 | 51.36 | 0.35 | `00 02 0A 1C 48 B0 A0 C1 83 08 13 2A 5C C8 B0 A1` |
| `status.scr` | packed_4bpp_320x200 | 32000 | 1572 | 20.3562 | 7.8245 | 49.36 | 1.65 | `77 02 0A 1C 48 B0 A0 C1 83 08 13 2A 5C C8 B0 A1` |

## Reading notes

- High-bit and PackBits-like profiles are heuristic counters, not decoded output validation.
- The very low entropy and repeated command-looking bytes suggest a custom RLE/delta-style codec remains plausible.
- The target decoded sizes are already known; the remaining work is to identify command semantics.
