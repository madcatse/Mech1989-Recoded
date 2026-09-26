# Graphics/font/palette candidate triage

Files with `.BMP`, `.PAL`, and `.FNT` extensions are treated as graphics-related candidates only. The byte layout is not considered confirmed here.

Several files use ASCII resource tags such as `BMP:`, `PAL:`, `FNT:`, `INF:`, `BIN:`, and `EGA:`. These are chunk-structure candidates, not standard Windows bitmap confirmation.

Summary:
| filename | extension | size_bytes | triage_note |
| --- | --- | --- | --- |
| 6X6.FNT | .FNT | 580 | raw font candidate; first two bytes are 6 and 6 |
| 6X6B.FNT | .FNT | 588 | starts with custom FNT: resource tag; inspect nested font chunks |
| 8X8B.FNT | .FNT | 780 | starts with custom FNT: resource tag; inspect nested font chunks |
| ARCTIC.PAL | .PAL | 314 | starts with custom PAL: resource tag; inspect nested palette chunks |
| BAT.BMP | .BMP | 4152 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| COCKPIT.BMP | .BMP | 767 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| DESERT.PAL | .PAL | 314 | starts with custom PAL: resource tag; inspect nested palette chunks |
| DIGITS.BMP | .BMP | 456 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| DMGPAL.PAL | .PAL | 314 | starts with custom PAL: resource tag; inspect nested palette chunks |
| DOTS.BMP | .BMP | 127 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| FOX88.FNT | .FNT | 772 | raw font candidate; first two bytes are 8 and 8 |
| HUD_CYAN.BMP | .BMP | 252 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| HUD_NUMS.BMP | .BMP | 252 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| JEN.BMP | .BMP | 2787 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| LOC.BMP | .BMP | 3033 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| MAR.BMP | .BMP | 3324 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| PHO.BMP | .BMP | 3577 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| POINT.BMP | .BMP | 278 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| PROMPT.BMP | .BMP | 294 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| RIF.BMP | .BMP | 4293 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| SHA.BMP | .BMP | 3864 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| SM_MECHS.BMP | .BMP | 3072 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| STRUTS.BMP | .BMP | 1135 | starts with custom BMP: resource tag; not a standard Windows BMP header |
| TROPIC.PAL | .PAL | 314 | starts with custom PAL: resource tag; inspect nested palette chunks |
| WAR.BMP | .BMP | 3684 | starts with custom BMP: resource tag; not a standard Windows BMP header |

Tagged resource chunks parsed: 65
