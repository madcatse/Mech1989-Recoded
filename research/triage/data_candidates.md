# Data/container candidate triage

Extensions inspected: `.BIN`, `.TBL`, `.GRD`, `.WLD`, `.DAT`.
Counts: .BIN: 8, .DAT: 1, .GRD: 32, .TBL: 10, .WLD: 32

The offset-table and palette reports are heuristic scans. They identify byte patterns worth checking manually, not confirmed structures.

Largest files:
| filename | extension | size_bytes | entropy | printable_ascii_ratio |
| --- | --- | --- | --- | --- |
| MW_PICS.BIN | .BIN | 328171 | 6.2767 | 0.3302 |
| SHAPCK.TBL | .TBL | 40544 | 7.6528 | 0.3486 |
| MARPCK.TBL | .TBL | 39022 | 7.5905 | 0.3345 |
| BMAPCK.TBL | .TBL | 34973 | 7.5540 | 0.3353 |
| RIFPCK.TBL | .TBL | 32775 | 7.5473 | 0.3310 |
| MW_TPICS.BIN | .BIN | 32593 | 5.9485 | 0.3670 |
| MW_CPICS.BIN | .BIN | 30097 | 6.4182 | 0.3128 |
| HAMPCK.TBL | .TBL | 29960 | 7.5676 | 0.3398 |
| MW_1PICS.BIN | .BIN | 26662 | 5.3785 | 0.4161 |
| LOCPCK.TBL | .TBL | 26627 | 7.6515 | 0.3557 |
| PHAPCK.TBL | .TBL | 24673 | 7.7282 | 0.3570 |
| JENPCK.TBL | .TBL | 22866 | 7.7749 | 0.3724 |
| MW_2PICS.BIN | .BIN | 21348 | 5.0344 | 0.4720 |
| MW_GPICS.BIN | .BIN | 18846 | 5.7880 | 0.4969 |
| MW_FPICS.BIN | .BIN | 13644 | 5.6867 | 0.2258 |

Possible offset-table starts found: 0
Possible embedded palette windows found: 32
Decoded 14-byte `.WLD` candidate records: 86
