# BTECH resource table candidates

Extracted fixed byte ranges from `BTECH.EXE` that contain resource filenames and chunk tag layout strings.

Rows: 72

## mech_bmp_names
- `0x026DAC` `filename` `loc.bmp`
- `0x026DB4` `filename` `jen.bmp`
- `0x026DBC` `filename` `pho.bmp`
- `0x026DC4` `filename` `sha.bmp`
- `0x026DCC` `filename` `rif.bmp`
- `0x026DD4` `filename` `war.bmp`
- `0x026DDC` `filename` `mar.bmp`
- `0x026DE4` `filename` `bat.bmp`

## combat_asset_names
- `0x027071` `filename` `desert.pal`
- `0x02707C` `filename` `arctic.pal`
- `0x027087` `filename` `tropic.pal`
- `0x027092` `filename` `dmgpal.pal`
- `0x02709D` `filename` `6x6b.fnt`
- `0x0270A6` `filename` `prompt.bmp`
- `0x0270B1` `filename` `btechsnd.snd`
- `0x0270BE` `label_or_mode` `Sound file not found...`
- `0x0270D6` `label_or_mode` `locpck`
- `0x0270DD` `label_or_mode` `jenpck`
- `0x0270E4` `label_or_mode` `phapck`
- `0x0270EB` `label_or_mode` `shapck`
- `0x0270F2` `label_or_mode` `rifpck`
- `0x0270F9` `label_or_mode` `hampck`
- `0x027100` `label_or_mode` `marpck`
- `0x027107` `label_or_mode` `bmapck`
- `0x02710E` `label_or_mode` `tbl`
- `0x027112` `label_or_mode` `terpck`
- `0x027119` `label_or_mode` `gi`
- `0x02711C` `label_or_mode` `terpck`
- `0x027123` `label_or_mode` `tbl`
- `0x027127` `label_or_mode` `othpck`
- `0x02712E` `label_or_mode` `tbl`
- `0x02714A` `label_or_mode` `card not found`
- `0x027159` `filename` `point.bmp`
- `0x027163` `filename` `sm_mechs.bmp`
- `0x027170` `filename` `struts.bmp`
- `0x02717B` `filename` `cockpit.bmp`
- `0x027187` `filename` `hud_cyan.bmp`
- `0x027194` `filename` `hud_nums.bmp`

### PCK filename pointer candidate

`btech_pck_filename_pointer_candidates.csv` records a nearby pointer-like table at DS `0x06DF` / file `0x027133`. The first eight little-endian words become the eight BattleMech PCK base names when adjusted by `+3`:

| Index | Raw word | Adjusted target | Name |
| ---: | --- | --- | --- |
| 0 | `0x067F` | `0x0682` | `locpck` |
| 1 | `0x0686` | `0x0689` | `jenpck` |
| 2 | `0x068D` | `0x0690` | `phapck` |
| 3 | `0x0694` | `0x0697` | `shapck` |
| 4 | `0x069B` | `0x069E` | `rifpck` |
| 5 | `0x06A2` | `0x06A5` | `hampck` |
| 6 | `0x06A9` | `0x06AC` | `marpck` |
| 7 | `0x06B0` | `0x06B3` | `bmapck` |

Treat the `+3` adjustment as unresolved until the filename builder is confirmed in code. The match is strong enough to use as a candidate map for mech-type to combat PCK base names.

## scr_names
- `0x027248` `filename` `light.scr`
- `0x027252` `filename` `medium.scr`
- `0x02725D` `filename` `heavy.scr`

## status_scr_name
- `0x0277D0` `label_or_mode` `TAGE RAID`
- `0x0277DA` `filename` `status.scr`

## sound_music_tag_layouts
- `0x0291E9` `label_or_mode` `SND:INF:`
- `0x0291F2` `label_or_mode` `SND:IBM:`
- `0x0291FB` `label_or_mode` `SND:TAN:`
- `0x029204` `label_or_mode` `SND:8SV:`
- `0x02920D` `label_or_mode` `SND:ROL:`
- `0x029216` `label_or_mode` `SND:ITM:`
- `0x029221` `label_or_mode` `MUS:INF:`
- `0x02922A` `label_or_mode` `MUS:ROL:`
- `0x029233` `label_or_mode` `MUS:ROL:`
- `0x02923C` `label_or_mode` `MUS:TAN:`
- `0x029245` `chunk_tag` `MUS:`

## graphics_tag_layouts
- `0x0295E3` `label_or_mode` `BMP:INF:`
- `0x0295EC` `label_or_mode` `BMP:BIN:`
- `0x0295F7` `label_or_mode` `BMP:VGA:`
- `0x029603` `label_or_mode` `SCR:BIN:`
- `0x02960E` `label_or_mode` `SCR:VGA:`
- `0x029617` `label_or_mode` `r5`
- `0x02961F` `chunk_tag` `FNT:`
- `0x029624` `filename` `.fnt`
- `0x029629` `label_or_mode` `TA`
- `0x029632` `label_or_mode` `PAL:CGA:`
- `0x02963B` `label_or_mode` `PAL:EGA:`
- `0x029644` `label_or_mode` `PAL:EGA:`
- `0x02964D` `label_or_mode` `PAL:CGA:`
- `0x029656` `label_or_mode` `PAL:VGA:`
- `0x02965F` `label_or_mode` `PAL:AMG:`
- `0x029668` `label_or_mode` `PAL:AST:`
- `0x029671` `label_or_mode` `PAL:VGA:`
- `0x02967A` `filename` `.pal`
