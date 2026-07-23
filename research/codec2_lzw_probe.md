# Codec 2 LZW probe

Rows tested: 3360
Exact size+consumed matches: 61

## Exact matches

| sample | params | reason | decoded hex preview |
|---|---|---|---|
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=10; grow_when=after_insert_ge; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=10; grow_when=after_insert_gt; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=11; grow_when=after_insert_ge; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=11; grow_when=after_insert_gt; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=12; grow_when=after_insert_ge; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=12; grow_when=after_insert_gt; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=13; grow_when=after_insert_ge; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=13; grow_when=after_insert_gt; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=14; grow_when=after_insert_ge; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=14; grow_when=after_insert_gt; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=15; grow_when=after_insert_ge; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=15; grow_when=after_insert_gt; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=16; grow_when=after_insert_ge; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DOTS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=16; grow_when=after_insert_gt; reserved=none | code_eof | `F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00 F0 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=10; grow_when=after_insert_ge; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=10; grow_when=after_insert_gt; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=11; grow_when=after_insert_ge; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=11; grow_when=after_insert_gt; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=12; grow_when=after_insert_ge; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=12; grow_when=after_insert_gt; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=13; grow_when=after_insert_ge; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=13; grow_when=after_insert_gt; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=14; grow_when=after_insert_ge; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=14; grow_when=after_insert_gt; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=15; grow_when=after_insert_ge; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=15; grow_when=after_insert_gt; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=16; grow_when=after_insert_ge; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `DIGITS.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=16; grow_when=after_insert_gt; reserved=none | code_eof | `FF F0 00 00 F0 F0 00 00 F0 F0 00 00 F0 F0 00 00 FF F0 00 00 0F 00 00 00 0F 00 00 00 0F 00 00 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=10; grow_when=after_insert_ge; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=10; grow_when=after_insert_gt; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=11; grow_when=after_insert_ge; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=11; grow_when=after_insert_gt; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=12; grow_when=after_insert_ge; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=12; grow_when=after_insert_gt; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=13; grow_when=after_insert_ge; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=13; grow_when=after_insert_gt; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=14; grow_when=after_insert_ge; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=14; grow_when=after_insert_gt; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=15; grow_when=after_insert_ge; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=15; grow_when=after_insert_gt; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=16; grow_when=after_insert_ge; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `HUD_CYAN.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=16; grow_when=after_insert_gt; reserved=none | code_eof | `BB BB 0B BB B0 BB BB 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00 B0 0B 0B 00 B0 B0 0B 00` |
| `BAT.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=12; grow_when=after_insert_ge; reserved=none | code_eof | `99 19 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99` |
| `BAT.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=13; grow_when=after_insert_ge; reserved=none | code_eof | `99 19 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99` |
| `BAT.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=14; grow_when=after_insert_ge; reserved=none | code_eof | `99 19 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99` |
| `BAT.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=15; grow_when=after_insert_ge; reserved=none | code_eof | `99 19 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99` |
| `BAT.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=16; grow_when=after_insert_ge; reserved=none | code_eof | `99 19 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99 99 91 99 99` |
| `PROMPT.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=10; grow_when=after_insert_ge; reserved=none | code_eof | `77 77 77 77 7F 3F 3F 3F FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF` |
| `PROMPT.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=10; grow_when=after_insert_gt; reserved=none | code_eof | `77 77 77 77 7F 3F 3F 3F FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF` |
| `PROMPT.BMP` | bit_order=lsb; initial_width=9; initial_next_code=257; max_width=11; grow_when=after_insert_ge; reserved=none | code_eof | `77 77 77 77 7F 3F 3F 3F FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF FF` |

## Best rows

See `codec2_lzw_probe_best.csv` for the closest rows per sample.
