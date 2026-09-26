# Follow-up triage index

Generated reports:

- `mz_executables.md` / `mz_executables.csv`: DOS MZ header fields and embedded file-name-like strings.
- `mz_embedded_strings.csv`: extracted printable strings from MZ executables with offsets.
- `mz_file_references.csv`: normalized executable file-name-like references and whether they exist in `Original`.
- `gam_comparison.md`: `.GAM` comparison summary.
- `gam_pairwise_diff.csv`: pairwise byte differences between `.GAM` files.
- `gam_offset_variability.csv`: offsets that vary across `.GAM` files.
- `gam_variable_runs.csv`: contiguous variable-byte ranges across `.GAM` files.
- `gam_duplicate_hashes.csv`: identical `.GAM` payload groups, if any.
- `data_candidates.md` / `data_candidates.csv`: `.BIN/.TBL/.GRD/.WLD/.DAT` byte-level triage.
- `possible_offset_tables.csv`: heuristic little-endian offset table candidates.
- `possible_embedded_palettes.csv`: heuristic VGA-style palette-window candidates.
- `wld_14byte_records.csv`: `.WLD` files decoded as 14-byte record candidates.
- `graphics_candidates.md` / `graphics_candidates.csv`: `.BMP/.PAL/.FNT` triage without assuming confirmed formats.
- `resource_chunks.csv`: parsed ASCII tagged resource chunk candidates from graphics/font/palette files.

All findings are candidates for manual reverse engineering, not final format definitions.
