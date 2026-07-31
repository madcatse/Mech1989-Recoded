# NEWS NET publication table

This table is generated from the original `MW_MAIN.EXE` publication records at file offset `0x00F0F7`. Dates use the original condition format `message_id 03 01 month day year_minus_3000 FF`; the condition passes when the campaign date is at or after the listed date.

All `51` records currently listed here have confirmed save-side read-flag observations and are mapped to `MW_MAIN.EXE` text blocks. A few brief-headline records preserve unusual raw date bytes from the executable; the article text dates are noted separately where they differ.

| order | id | record offset | after date | text id | text offset | title | confidence | notes |
|---:|---:|---:|---|---|---:|---|---|---|
| 0 | 0x01 | 0x00F0F7 | 3024-04-01 | mw_main.news.01c0b4 | 0x01C0B4 | ander's moon news | confirmed_observed | April 3024 start message |
| 1 | 0x02 | 0x00F0FE | 3024-04-08 | mw_main.news.01c418 | 0x01C418 | BANDITS ATTACK PERIPHERY | confirmed_observed | June 3024 snapshot |
| 2 | 0x05 | 0x00F105 | 3024-04-15 | mw_main.news.01c303 | 0x01C303 | PERSONAL MESSAGE FROM: JORDAN ROWE | confirmed_observed | `SAV2 -> SAV3` birthday screenshot plus read flag. |
| 3 | 0x04 | 0x00F10C | 3024-05-01 | mw_main.news.01c60a | 0x01C60A | WANTED - 200,000 C-BILL REWARD | confirmed_observed | `SAV3 -> SAV4` screenshot plus read flag. |
| 4 | 0x2C | 0x00F113 | 3024-06-30 | mw_main.news.019f24 | 0x019F24 | 23 JUNE 3024 | confirmed_observed | `SAV3 -> SAV4` Susquehanna screenshot plus read flag. |
| 5 | 0x1D | 0x00F11A | 3024-07-01 | mw_main.news.01ad7d | 0x01AD7D | JUNE 3024 | confirmed_observed | `SAV4 -> SAV5` New Earth / Alliance Games screenshot plus read flag. |
| 6 | 0x2A | 0x00F121 | 3024-08-25 | mw_main.news.01a192 | 0x01A192 | 13 AUGUST 3024 | confirmed_observed | `SAV4 -> SAV5` Thestria / ISP border security screenshot plus read flag. |
| 7 | 0x10 | 0x00F128 | 3024-11-15 | mw_main.news.01beee | 0x01BEEE | 1 NOVEMBER 3024 | confirmed_observed | `SAV5 -> SAV6` Regis / Verthandi screenshot plus read flag. |
| 8 | 0x03 | 0x00F12F | 3025-04-08 | mw_main.news.01c906 | 0x01C906 | PERSONAL MESSAGE FROM: JORDAN ROWE | confirmed_observed | `SAV6 -> SAV7` birthday screenshot plus read flag. |
| 9 | 0x03 | 0x00F136 | 3026-04-08 | mw_main.news.01c906 | 0x01C906 | personal message | confirmed_observed | annual Jordan Rowe message, visible in April 3025 |
| 10 | 0x16 | 0x00F13D | 3026-06-01 | mw_main.news.01afd5 | 0x01AFD5 | WANTED - 50,000 C-BILL REWARD | confirmed_observed | `SAV10 -> SAV11` Luthien reward screenshot plus read flag. |
| 11 | 0x1E | 0x00F144 | 3026-08-15 | mw_main.news.01ac1a | 0x01AC1A | AFFS ANNOUNCES MASSIVE WARGAMES | confirmed_observed | `SAV11 -> SAV12` New Avalon screenshot plus read flag. |
| 12 | 0x1F | 0x00F14B | 3026-09-05 | mw_main.news.01a9f0 | 0x01A9F0 | OPERATION GALAHAD OFF AND RUNNING | confirmed_observed | `SAV12 -> SAV13` New Avalon screenshot plus read flag. |
| 13 | 0x20 | 0x00F152 | 3026-11-30 | mw_main.news.01a84d | 0x01A84D | OPERATION GALAHAD ENDS | confirmed_observed | `SAV13 -> SAV14` New Avalon screenshot plus read flag. |
| 14 | 0x14 | 0x00F159 | 3027-03-15 | mw_main.news.01b3c6 | 0x01B3C6 | OUTLAW MERCENARIES MASSACRE MILLIONS | confirmed_observed | `SAV14 -> SAV15` Tiantan screenshot plus read flag. |
| 15 | 0x21 | 0x00F160 | 3027-07-15 | mw_main.news.01a6a6 | 0x01A6A6 | OPERATION GALAHAD TO BE REPEATED | confirmed_observed | `SAV16 -> SAV17` New Avalon screenshot plus read flag. |
| 16 | 0x22 | 0x00F167 | 3027-08-17 | mw_main.news.01a4fb | 0x01A4FB | GALAHAD 3027 IS LAUNCHED | confirmed_observed | `SAV16 -> SAV17` New Avalon screenshot plus read flag. |
| 17 | 0x23 | 0x00F16E | 3027-11-15 | mw_main.news.01a30c | 0x01A30C | GALAHAD 3027 AND OPERATION THOR END | confirmed_observed | `SAV18 -> SAV19` New Avalon screenshot plus read flag; text title date says 6 November 3027. |
| 18 | 0x03 | 0x00F175 | 3027-04-08 | mw_main.news.01c906 | 0x01C906 | personal message | confirmed_observed | annual Jordan Rowe message, visible in April 3025 |
| 19 | 0x15 | 0x00F17C | 3027-05-15 | mw_main.news.01b214 | 0x01B214 | GRAY DEATH CLEARED OF SIRIUS V MASSACRE | confirmed_observed | `SAV15 -> SAV16` Helmdown screenshot plus read flag. |
| 20 | 0x11 | 0x00F183 | 3028-01-02 | mw_main.news.01b971 | 0x01B971 | RIOTING SHAKES CAPITAL OF AN TING | confirmed_observed | `SAV18 -> SAV19` Cerant screenshot plus read flag. |
| 21 | 0x12 | 0x00F18A | 3028-01-03 | mw_main.news.01bc2f | 0x01BC2F | COMSTAR FACILITY ATTACKED! HEPHAESTUS DESTROYED! | confirmed_observed | `SAV18 -> SAV19` Cerant screenshot plus read flag. |
| 22 | 0x13 | 0x00F191 | 3028-01-14 | mw_main.news.01b667 | 0x01B667 | WOLF'S DRAGOONS DEFEAT KURITA RYUKEN | confirmed_observed | `SAV18 -> SAV19` Cerant screenshot plus read flag. |
| 23 | 0x03 | 0x00F198 | 3028-04-08 | mw_main.news.01c906 | 0x01C906 | personal message | confirmed_observed | annual Jordan Rowe message, visible in April 3025 |
| 24 | 0x03 | 0x00F19F | 3029-04-08 | mw_main.news.01c906 | 0x01C906 | personal message | confirmed_observed | annual Jordan Rowe message, visible in April 3025 |
| 25 | 0x55 | 0x00F1A6 | 3025-01-22 | mw_main.news.01e383 | 0x01E383 | MATABUSHI'S NEW IMAGE | confirmed_observed | `SAV5 -> SAV6` Matabushi screenshot plus read flag. |
| 26 | 0x56 | 0x00F1AD | 3024-11-05 | mw_main.news.01e668 | 0x01E668 | VALENSIA / SENIOR COUNCIL PREPARES FOR FISCAL BATTLE | confirmed_observed | `SAV5 -> SAV6` Valensia fiscal battle screenshot plus read flag. |
| 27 | 0x57 | 0x00F1B4 | 3025-03-05 | mw_main.news.01e8f3 | 0x01E8F3 | FINANCIAL SERVICES COMPANY OPENING | confirmed_observed | `SAV6 -> SAV7` Valensia financial services screenshot plus read flag. |
| 28 | 0x58 | 0x00F1BB | 3025-04-10 | mw_main.news.01ea9c | 0x01EA9C | ANDER'S MOON ECONOMY REELING | confirmed_observed | `SAV6 -> SAV7` Ander's Moon economy screenshot plus read flag. |
| 29 | 0x59 | 0x00F1C2 | 3025-09-15 | mw_main.news.01ed2c | 0x01ED2C | SACRIFICIAL FORESTS AND THE MONEY GOD | confirmed_observed | `SAV8 -> SAV9` Alicia Rowe editorial screenshot plus read flag. |
| 30 | 0x5A | 0x00F1C9 | 3025-02-14 | mw_main.news.01f01f | 0x01F01F | NEW ALLIANCES ARE FORMING IN VALENSIA | confirmed_observed | `SAV6 -> SAV7` Valensia alliances screenshot plus read flag; date is earlier than displayed position. |
| 31 | 0x5C | 0x00F1D0 | 3026-08-28 | mw_main.news.01f292 | 0x01F292 | WINEMAKERS MAKE DRAMATIC COMEBACK | confirmed_observed | `SAV11 -> SAV12` Valensia winemakers screenshot plus read flag. |
| 32 | 0x5D | 0x00F1D7 | 3028-01-12 | mw_main.news.01f545 | 0x01F545 | ALLEGED MCBRIN CRIMES FALL ON DEAF EARS | confirmed_observed | `SAV18 -> SAV19` Valensia screenshot plus read flag. |
| 33 | 0x5E | 0x00F1DE | raw 1C/01/1B | mw_main.news.01f82e | 0x01F82E | BRIEF HEADLINES / JUSTIN ALLARD GOES ON TRIAL | confirmed_observed | `SAV18 -> SAV19` brief-headline screenshot plus read flag; record bytes use invalid raw month `0x1C`, text title date says 20 Jan 3027. |
| 34 | 0x5F | 0x00F1E5 | 3027-10-02 | mw_main.news.01f8dc | 0x01F8DC | BRIEF HEADLINES / ALLARD TRIAL ENDS | confirmed_observed | `SAV17 -> SAV18` brief headlines screenshot plus read flag; text title date says 30 Jan 3027. |
| 35 | 0x60 | 0x00F1EC | raw 1F/03/1B | mw_main.news.01f96d | 0x01F96D | BRIEF HEADLINES / JUSTIN XIANG SEEKS VENGENCE | confirmed_observed | `SAV18 -> SAV19` brief-headline screenshot plus read flag; record bytes use invalid raw month `0x1F`, text title date says 20 Mar 3027. |
| 36 | 0x61 | 0x00F1F3 | raw 1E/04/1B | mw_main.news.01f9fe | 0x01F9FE | BRIEF HEADLINES / XIANG CONTINUES METEROIC RISE | confirmed_observed | `SAV18 -> SAV19` brief-headline screenshot plus read flag; record bytes use invalid raw month `0x1E`, text title date says 20 Apr 3027. |
| 37 | 0x62 | 0x00F1FA | 3027-06-08 | mw_main.news.01fa8d | 0x01FA8D | BRIEF HEADLINES / XIANGS REVENGE CONTINUES | confirmed_observed | `SAV15 -> SAV16` brief headlines screenshot plus read flag. |
| 38 | 0x63 | 0x00F201 | 3027-10-31 | mw_main.news.01fb1e | 0x01FB1E | BRIEF HEADLINES / HANSE DAVION AND MELISSA STEINER TO WED | confirmed_observed | `SAV17 -> SAV18` brief headlines screenshot plus read flag. |
| 39 | 0x70 | 0x00F208 | 3027-11-28 | mw_main.news.01fc47 | 0x01FC47 | BRIEF HEADLINES / CAPTAIN A. REDBURN ATTACKED | confirmed_observed | `SAV18 -> SAV19` brief-headline screenshot plus read flag. |
| 40 | 0x65 | 0x00F20F | 3028-06-30 | mw_main.headline.01fcfa | 0x01FCFA | BRIEF HEADLINES / OPERATIONS GALLAHAD AND THOR 3028 TO PROCEED | confirmed_observed | `SAV21 -> SAV22` brief-headline screenshot plus read flag. |
| 41 | 0x66 | 0x00F216 | 3028-08-13 | mw_main.headline.01fd83 | 0x01FD83 | BRIEF HEADLINES / WOLFS DRAGOONS OFFICIALY STATIONED IN DAVION SPACE | confirmed_observed | `SAV21 -> SAV22` brief-headline screenshot plus read flag. |
| 42 | 0x67 | 0x00F21D | 3028-08-22 | mw_main.headline.01fea8 | 0x01FEA8 | BRIEF HEADLINES / HANSE DAVION AND MELISSA STEINER WED | confirmed_observed | `SAV21 -> SAV22` brief-headline screenshot plus read flag. |
| 43 | 0x68 | 0x00F224 | 3028-08-30 | mw_main.headline.01ffb8 | 0x01FFB8 | BRIEF HEADLINES / FED SUNS AND COMMONWEALTH ATTACKS | confirmed_observed | `SAV21 -> SAV22` brief-headline screenshot plus read flag. |
| 44 | 0x69 | 0x00F22B | 3028-10-11 | mw_main.headline.020152 | 0x020152 | BRIEF HEADLINES / THE FOX STILL ON THE HUNT | confirmed_observed | `SAV22 -> SAV23` brief-headline screenshot plus read flag; text title date says 25 Sep 3028. |
| 45 | 0x6A | 0x00F232 | 3028-10-28 | mw_main.headline.0201d2 | 0x0201D2 | BRIEF HEADLINES / TIKONOV THROWS IN THE TOWEL | confirmed_observed | `SAV22 -> SAV23` brief-headline screenshot plus read flag; text title date says 18 Oct 3028. |
| 46 | 0x6B | 0x00F239 | 3028-11-23 | mw_main.headline.020328 | 0x020328 | BRIEF HEADLINES / FOX GOES FOR THE THROAT | confirmed_observed | `SAV22 -> SAV23` brief-headline screenshot plus read flag; text title date says 13 Nov 3028. |
| 47 | 0x6C | 0x00F240 | 3029-01-15 | mw_main.headline.0203a4 | 0x0203A4 | BRIEF HEADLINES / AFTER YEARS OF EXILE - HOME AT LAST | confirmed_observed | `SAV24 -> SAV25` brief-headline screenshot plus read flag; text title dates are 15/20/31 Dec 3028. |
| 48 | 0x6D | 0x00F247 | 3029-04-15 | mw_main.news.020515 | 0x020515 | SENIOR COUNCIL MOVES TO CHOOSE NEW DUKE | confirmed_observed | `SAV25 -> SAV26` Valensia / Ander's Moon screenshot plus read flag. |
| 49 | 0x6E | 0x00F24E | 3029-04-30 | mw_main.news.0206af | 0x0206AF | MCBRIN SUCCESSFUL! CHALICE RETURNED! | confirmed_observed | `SAV26 -> SAV27` Valensia / Ander's Moon screenshot plus read flag. |
| 50 | 0x6F | 0x00F255 | 3029-05-15 | mw_main.news.020912 | 0x020912 | JARRIS THEODORE MCBRIN IS NEW DUKE | confirmed_observed | `SAV26 -> SAV27` Valensia / Ander's Moon screenshot plus read flag. |
