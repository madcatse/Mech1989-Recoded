# MZ executable triage

All DOS executable candidates below have an `MZ` signature. Header values are decoded from the DOS EXE header only; no compiler/runtime identification is assumed.

| filename | size_bytes | header_size_bytes | relocation_count | entry_cs_ip | initial_ss_sp | extra_data_size | file_reference_count |
| --- | --- | --- | --- | --- | --- | --- | --- |
| BTECH.EXE | 174267 | 512 | 0 | 29D8:0012 | 2DC0:0080 | 0 | 28 |
| MW.EXE | 4096 | 512 | 4 | 0006:09EE | 0000:0064 | 189 | 2 |
| MW_CPICS.EXE | 3566 | 512 | 0 | 0000:02C6 | 0000:0000 | 0 | 1 |
| MW_EGA.EXE | 5599 | 512 | 0 | 0000:0000 | 0000:0000 | 0 | 5 |
| MW_MAIN.EXE | 137871 | 1024 | 125 | 0741:0FBC | 2169:03E8 | 0 | 17 |
| MW_TANDY.EXE | 5009 | 512 | 0 | 0000:0000 | 0000:0000 | 0 | 5 |

Embedded file-name-like strings should be treated as references to verify, not proof of load behavior.

| filename | file_references |
| --- | --- |
| BTECH.EXE | 6X6B.FNT; ARCTIC.PAL; BAT.BMP; BTECHSND.SND; COCKPIT.BMP; DESERT.PAL; DMGPAL.PAL; HEAVY.SCR; HUD_CYAN.BMP; HUD_NUMS.BMP; JEN.BMP; LIGHT.SCR; LOC.BMP; MAP.SCR; MAR.BMP; MEDIUM.SCR; MW.EXE; PHO.BMP; POINT.BMP; PROMPT.BMP; RIF.BMP; SHA.BMP; SM_MECHS.BMP; SNARIO.DAT; STATUS.SCR; STRUTS.BMP; TROPIC.PAL; WAR.BMP |
| MW.EXE | BTECH.EXE; MW_MAIN.EXE |
| MW_CPICS.EXE | MW_CPICS.BIN |
| MW_EGA.EXE | 6X6.FNT; 88C64.FNT; 8X8.FNT; FOX88.FNT; PROM88.FNT |
| MW_MAIN.EXE | BTECH.EXE; DEATH.MUS; LAUNCH.MUS; MECH.MUS; MECHBAR.MUS; MW_1PICS.BIN; MW_2PICS.BIN; MW_3PICS.BIN; MW_APICS.BIN; MW_CPICS.BIN; MW_EGA.EXE; MW_FPICS.BIN; MW_GPICS.BIN; MW_PICS.BIN; MW_TANDY.EXE; MW_TPICS.BIN; VICTORY.MUS |
| MW_TANDY.EXE | 6X6.FNT; 88C64.FNT; 8X8.FNT; FOX88.FNT; PROM88.FNT |

Referenced file-name-like strings not present as files in `Original`: 7
| filename | referenced_name | normalized_name |
| --- | --- | --- |
| MW_EGA.EXE | 88C64.FNT | 88C64.FNT |
| MW_EGA.EXE | 8X8.FNT | 8X8.FNT |
| MW_EGA.EXE | PROM88.FNT | PROM88.FNT |
| MW_MAIN.EXE | MW_3PICS.BIN | MW_3PICS.BIN |
| MW_TANDY.EXE | 88C64.FNT | 88C64.FNT |
| MW_TANDY.EXE | 8X8.FNT | 8X8.FNT |
| MW_TANDY.EXE | PROM88.FNT | PROM88.FNT |
