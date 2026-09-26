# File inventory notes

Source directory: Original
Generated UTC: 2026-06-24T12:06:27Z
File count: 141
Total bytes: 1344193

This catalog is intentionally conservative. Role guesses are candidates for triage, not confirmed file formats.

Generated artifacts:
- file_inventory.csv: main forensic inventory
- file_inventory.json: same records in JSON form
- file_hashes.md5 / file_hashes.sha1 / file_hashes.sha256: checksum lists
- file_tree.txt: size-sorted source file list in path order
- extension_summary.csv: grouped extension totals
- role_summary.csv: grouped role-candidate totals
- strings/: printable ASCII strings per file, minimum length 4
- hex_samples/: first and last byte samples per file

Recommended follow-up triage:
- Inspect MZ executables and their embedded filenames/strings.
- Compare all .GAM files byte-by-byte to identify save/game-state fields.
- Inspect .BIN/.TBL/.GRD/.WLD files for record sizes, offset tables, and embedded palettes.
- Treat .BMP/.PAL/.FNT as graphics-related candidates until the byte layout is confirmed.
