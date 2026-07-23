# NEWS NET publication table draft

This table is generated from the original `MW_MAIN.EXE` publication records at file offset `0x00F0F7`. Dates use the original condition format `message_id 03 01 month day year_minus_3000 FF`; the condition passes when the campaign date is at or after the listed date.

`confidence` values: `confirmed_observed` was checked against supplied original-game observations, `order_inferred` is inferred from original record order plus observed NEWS5 order, `title_date_inferred` matches article title dates but still needs a save/screenshot cut, and `unmapped` still needs work.

| order | id | record offset | after date | text id | text offset | title | confidence | notes |
|---:|---:|---:|---|---|---:|---|---|---|
| 0 | 0x01 | 0x00F0F7 | 3024-04-01 | mw_main.news.01c0b4 | 0x01C0B4 | ander's moon news | confirmed_observed | April 3024 start message |
| 1 | 0x02 | 0x00F0FE | 3024-04-08 | mw_main.news.01c418 | 0x01C418 | BANDITS ATTACK PERIPHERY | confirmed_observed | June 3024 snapshot |
| 2 | 0x05 | 0x00F105 | 3024-04-15 | mw_main.news.01c60a | 0x01C60A | WANTED - 200,000 C-BILL REWARD | confirmed_observed | June 3024 snapshot |
| 3 | 0x04 | 0x00F10C | 3024-05-01 |  |  |  | unmapped |  |
| 4 | 0x2C | 0x00F113 | 3024-06-30 | mw_main.news.019f24 | 0x019F24 | 23 JUNE 3024 | confirmed_observed | September 3024 snapshot |
| 5 | 0x1D | 0x00F11A | 3024-07-01 | mw_main.news.01ad7d | 0x01AD7D | JUNE 3024 | confirmed_observed | September 3024 snapshot |
| 6 | 0x2A | 0x00F121 | 3024-08-25 | mw_main.news.01a192 | 0x01A192 | 13 AUGUST 3024 | confirmed_observed | September 3024 snapshot |
| 7 | 0x10 | 0x00F128 | 3024-11-15 | mw_main.news.01beee | 0x01BEEE | 1 NOVEMBER 3024 | confirmed_observed | January/April 3025 snapshots |
| 8 | 0x03 | 0x00F12F | 3025-04-08 | mw_main.news.01c906 | 0x01C906 | personal message | confirmed_observed | annual Jordan Rowe message, visible in April 3025 |
| 9 | 0x03 | 0x00F136 | 3026-04-08 | mw_main.news.01c906 | 0x01C906 | personal message | confirmed_observed | annual Jordan Rowe message, visible in April 3025 |
| 10 | 0x16 | 0x00F13D | 3026-06-01 |  |  |  | unmapped |  |
| 11 | 0x1E | 0x00F144 | 3026-08-15 | mw_main.news.01ac1a | 0x01AC1A | 9 AUGUST 3026 | title_date_inferred | 9 August 3026 title near 15 August publication |
| 12 | 0x1F | 0x00F14B | 3026-09-05 | mw_main.news.01a9f0 | 0x01A9F0 | 1 SEPTEMBER 3026 | title_date_inferred | 1 September 3026 title near 5 September publication |
| 13 | 0x20 | 0x00F152 | 3026-11-30 |  |  |  | unmapped |  |
| 14 | 0x14 | 0x00F159 | 3027-03-15 | mw_main.news.01b3c6 | 0x01B3C6 | 10 MARCH 3027 | title_date_inferred | 10 March 3027 title near 15 March publication |
| 15 | 0x21 | 0x00F160 | 3027-07-15 | mw_main.news.01a6a6 | 0x01A6A6 | 7 JULY 3027 | title_date_inferred | 7 July 3027 title near 15 July publication |
| 16 | 0x22 | 0x00F167 | 3027-08-17 | mw_main.news.01a4fb | 0x01A4FB | 10 AUGUST 3027 | title_date_inferred | 10 August 3027 title near 17 August publication |
| 17 | 0x23 | 0x00F16E | 3027-11-15 | mw_main.news.01a30c | 0x01A30C | 6 NOVEMBER 3027 | title_date_inferred | 6 November 3027 title near 15 November publication |
| 18 | 0x03 | 0x00F175 | 3027-04-08 | mw_main.news.01c906 | 0x01C906 | personal message | confirmed_observed | annual Jordan Rowe message, visible in April 3025 |
| 19 | 0x15 | 0x00F17C | 3027-05-15 | mw_main.news.01b214 | 0x01B214 | 21 MAY 3027 | title_date_inferred | 21 May 3027 title near 15 May publication |
| 20 | 0x11 | 0x00F183 | 3028-01-02 | mw_main.news.01b971 | 0x01B971 | 2 JANUARY 3028 | title_date_inferred | 2 January 3028 title |
| 21 | 0x12 | 0x00F18A | 3028-01-03 | mw_main.news.01bc2f | 0x01BC2F | 3 JANUARY 3028 | title_date_inferred | 3 January 3028 title |
| 22 | 0x13 | 0x00F191 | 3028-01-14 | mw_main.news.01b667 | 0x01B667 | JANUARY 14, 3028 | title_date_inferred | 14 January 3028 title |
| 23 | 0x03 | 0x00F198 | 3028-04-08 | mw_main.news.01c906 | 0x01C906 | personal message | confirmed_observed | annual Jordan Rowe message, visible in April 3025 |
| 24 | 0x03 | 0x00F19F | 3029-04-08 | mw_main.news.01c906 | 0x01C906 | personal message | confirmed_observed | annual Jordan Rowe message, visible in April 3025 |
| 25 | 0x55 | 0x00F1A6 | 3025-01-22 | mw_main.news.01e383 | 0x01E383 | MATABUSHI'S NEW IMAGE | order_inferred | NEWS5 order suggests this is the first 3025 Valensia/Anders item |
| 26 | 0x56 | 0x00F1AD | 3024-11-05 | mw_main.news.01e668 | 0x01E668 | valensia news | order_inferred | NEWS4/NEWS5 order suggests this Nov 3024 item |
| 27 | 0x57 | 0x00F1B4 | 3025-03-05 | mw_main.news.01e8f3 | 0x01E8F3 | valensia news | order_inferred | NEWS5 order |
| 28 | 0x58 | 0x00F1BB | 3025-04-10 | mw_main.news.01ea9c | 0x01EA9C | valensia news | order_inferred | NEWS5 order |
| 29 | 0x59 | 0x00F1C2 | 3025-09-15 |  |  |  | unmapped |  |
| 30 | 0x5A | 0x00F1C9 | 3025-02-14 | mw_main.news.01f01f | 0x01F01F | valensia news | order_inferred | NEWS5 order; date is earlier than displayed position |
| 31 | 0x5C | 0x00F1D0 | 3026-08-28 |  |  |  | unmapped |  |
| 32 | 0x5D | 0x00F1D7 | 3028-01-12 |  |  |  | unmapped |  |
| 33 | 0x5E | 0x00F1DE | raw 1C/01/1B |  |  |  | unmapped |  |
| 34 | 0x5F | 0x00F1E5 | 3027-10-02 |  |  |  | unmapped |  |
| 35 | 0x60 | 0x00F1EC | raw 1F/03/1B |  |  |  | unmapped |  |
| 36 | 0x61 | 0x00F1F3 | raw 1E/04/1B |  |  |  | unmapped |  |
| 37 | 0x62 | 0x00F1FA | 3027-06-08 |  |  |  | unmapped |  |
| 38 | 0x63 | 0x00F201 | 3027-10-31 |  |  |  | unmapped |  |
| 39 | 0x70 | 0x00F208 | 3027-11-28 |  |  |  | unmapped |  |
| 40 | 0x65 | 0x00F20F | 3028-06-30 |  |  |  | unmapped |  |
| 41 | 0x66 | 0x00F216 | 3028-08-13 |  |  |  | unmapped |  |
| 42 | 0x67 | 0x00F21D | 3028-08-22 |  |  |  | unmapped |  |
| 43 | 0x68 | 0x00F224 | 3028-08-30 |  |  |  | unmapped |  |
| 44 | 0x69 | 0x00F22B | 3028-10-11 |  |  |  | unmapped |  |
| 45 | 0x6A | 0x00F232 | 3028-10-28 |  |  |  | unmapped |  |
| 46 | 0x6B | 0x00F239 | 3028-11-23 |  |  |  | unmapped |  |
| 47 | 0x6C | 0x00F240 | 3029-01-15 |  |  |  | unmapped |  |
| 48 | 0x6D | 0x00F247 | 3029-04-15 |  |  |  | unmapped |  |
| 49 | 0x6E | 0x00F24E | 3029-04-30 |  |  |  | unmapped |  |
| 50 | 0x6F | 0x00F255 | 3029-05-15 |  |  |  | unmapped |  |
