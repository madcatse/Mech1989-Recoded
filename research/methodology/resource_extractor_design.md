# Resource extractor design

This document describes a staged design for `mw_extract`, a resource inspection and extraction tool for the MechWarrior 1989 reconstruction project.

The extractor must not contain original game data. It only reads local files supplied by the user and writes derived analysis outputs or extracted byte ranges.

## Current evidence

Known from inventory and triage:

- `*.BMP`, `*.PAL`, `*.FNT`, `*.SCR`, and `BTECHSND.SND` use ASCII chunk tags such as `BMP:`, `PAL:`, `FNT:`, `INF:`, `BIN:`, `EGA:`, and `CGA:`.
- Tagged chunk layout appears to be:
  - 4-byte ASCII tag ending with `:`
  - 4-byte little-endian length
  - payload bytes
  - high bit of length may mark a parent/nested container
- Example: `BAT.BMP`:
  - `BMP:` parent chunk
  - nested `INF:` metadata chunk
  - nested `BIN:` payload chunk
- Example: `ARCTIC.PAL`:
  - `PAL:` parent chunk
  - nested `EGA:` chunk
  - nested `CGA:` chunk
- `MW_MAIN.EXE`, `BTECH.EXE`, `MW_EGA.EXE`, `MW_TANDY.EXE`, and `MW_CPICS.EXE` contain resource filename strings.
- `MW_CPICS.EXE` contains source-art path strings such as `C:\B_TECH\ART\CREW01.LBM`.
- Large `.BIN` and `.TBL` files are still unknown resource/container candidates.
- `.WLD` files divide cleanly into 14-byte record candidates.

## Staged approach

### Stage 1: exact chunk extractor

Implement a deterministic parser for tagged files where the structure is already strongly indicated.

Target files:

- `*.BMP`
- `*.PAL`
- `*.FNT`
- `*.SCR`
- `*.SND`

Tasks:

- Read tag and length fields.
- Decode length as little-endian `u32`.
- Interpret `length & 0x7fffffff` as payload size.
- Interpret `length & 0x80000000` as "has nested chunks" candidate.
- Validate that payload stays inside file bounds.
- Recursively parse nested chunks when the payload begins with a known tag.
- Save a map as CSV/JSON.
- Optionally extract each chunk payload as raw bytes.

Do not convert graphics yet. Extraction comes before interpretation.

### Stage 2: executable reference map

Use strings from executables as a cross-reference layer.

Tasks:

- Scan `*.EXE` for ASCII file-name-like strings.
- Normalize names case-insensitively.
- Check whether referenced files exist in the user-supplied game directory.
- Build `resource_refs.csv`:
  - executable
  - offset
  - referenced name
  - exists yes/no
  - note
- Use this map to prioritize extraction and unknown format analysis.

### Stage 3: heuristic container scanner

For unknown `.BIN`, `.TBL`, `.DAT`, `.GI`, and possibly `.GRD` files, scan for candidate structures without assuming the format.

Look for:

- Magic/tag strings near the start or at aligned offsets.
- Little-endian offset tables.
- Little-endian size tables.
- Repeated fixed-size records.
- Monotonic increasing offsets.
- Offset/length pairs where every range stays inside the file.
- Tables where offsets point to areas with distinct entropy.
- Embedded tagged chunks.
- Embedded palettes or small metadata headers.
- Repeated compressed-block signatures.

The scanner should emit candidates with confidence and evidence, not final names.

### Stage 4: extraction by manifest

Once a candidate map is reviewed, extraction should be manifest-driven.

The extractor should support:

- Automatically generated candidate maps.
- Manually edited maps.
- Extraction from approved offset/size ranges.
- Stable output names based on source file, offset, tag, and candidate id.

This keeps reverse-engineering hypotheses separate from the raw parser.

## Determining resource structure

### Header

Check:

- First 4 bytes as ASCII tag.
- First 2/4 bytes as possible count.
- First 2/4 bytes as possible offset to first payload.
- Whether a claimed count produces a plausible table.
- Whether the first bytes recur in many files of the same extension.
- Whether executable strings point to the file as a whole or to logical resources inside it.

For tagged resources, a header candidate is strong when:

- bytes 0..3 are an ASCII tag ending with `:`
- bytes 4..7 are a little-endian length
- length fits the file
- nested payload begins with another tag or valid-looking data

### Count

Count may be stored as:

- `u8`
- `u16le`
- `u32le`
- implicit by table length
- implicit by consuming records until EOF
- implicit by executable code or external filename table

Heuristic test:

```text
for candidate_count in possible header fields:
  for entry_size in [2,4,6,8,10,12,16]:
    table_end = header_size + candidate_count * entry_size
    if table_end <= file_size:
      validate entries as offsets/sizes/ids
```

### Offsets and sizes

Strong offset-table signs:

- Values are little-endian.
- Values are non-negative and inside file.
- Values are monotonic increasing.
- First offset is near the end of the table.
- Last offset is before EOF.
- Differences between adjacent offsets look like plausible block sizes.
- Blocks pointed to by offsets have sensible entropy or recognizable tags.

Offset/size pair signs:

- `offset + size <= file_size`.
- Offsets do not overlap unless expected.
- Sizes are not all zero.
- Entry count matches a nearby string/resource count.

### Compression

Possible compression signs:

- High entropy in payload compared with nearby metadata.
- Payload size is not a clean raw image size.
- Repeated command-like bytes or back-reference-looking patterns.
- Runtime error strings mention decompression or packed files.
- Loader strings mention buffers for decompression.
- Multiple resources share similar small headers then high-entropy payloads.

Current string evidence includes:

- `$Decompression buffer allocated.`
- `$COULD NOT ALLOCATE 64K FOR DECOMPRESSION BUFFER.`
- `!Packed file is corrupt6`

Treat these as hints, not proof for a specific algorithm.

### Resource names

Possible name sources:

- Embedded executable strings.
- Installer script copy commands.
- Source art paths in helper executables.
- Adjacent null-terminated string tables.
- Fixed-width name records.
- File order in containers.
- External table files.

Never invent names in extracted output. If no name is known, use a stable synthetic name:

```text
<source_stem>__chunk_<index>__off_<hex>__len_<size>.bin
```

## CLI architecture

Proposed CLI name: `mw_extract`.

Commands:

```text
mw_extract scan <input-file> [--json out.json] [--csv out.csv]
mw_extract map <game-dir> [--out research/extract/maps]
mw_extract extract <input-file> --map map.json --out extracted/
mw_extract extract-tagged <input-file> --out extracted/
mw_extract batch <game-dir> --out extracted/ --maps research/extract/maps
mw_extract strings <input-file> --min-len 4 --csv out.csv
mw_extract verify <game-dir> --inventory research/inventory/file_inventory.csv
```

### `scan`

Reads one file and emits a candidate map.

Output fields:

- source file
- candidate id
- parser type
- offset
- header size
- payload offset
- payload size
- end offset
- tag/name if known
- confidence
- evidence
- warnings

### `map`

Scans a directory and produces maps for every supported/candidate file:

- exact tagged chunk maps
- executable string reference maps
- heuristic candidate maps for unknown containers

### `extract`

Reads a reviewed map and writes extracted payloads.

Rules:

- Never write outside the output directory.
- Preserve original bytes exactly.
- Write metadata sidecar files.
- Include source hash and byte ranges.
- Do not overwrite unless `--force` is provided.

### `extract-tagged`

Convenience command for files with confirmed chunk tags.

Example output:

```text
extracted/BAT.BMP/
  manifest.json
  chunk_0000_BMP_off_000000_len_4144.bin
  chunk_0001_INF_off_000008_len_6.bin
  chunk_0002_BIN_off_000016_len_4122.bin
```

### `verify`

Checks that user-provided files match known inventory records, when available. This is optional; the tool must still work with different legitimate versions, but should report mismatches.

## Suggested project layout

```text
src/
  mw_extract/
    __init__.py
    cli.py
    io.py
    hashes.py
    strings.py
    chunks.py
    scanners.py
    maps.py
    extract.py
    formats/
      tagged.py
      mz_refs.py
      unknown_container.py
tests/
  test_tagged_chunks.py
  test_offset_scanner.py
  fixtures/
    synthetic_tagged_resource.bin
```

Use synthetic test fixtures only. Do not commit original game data as tests.

## Map schema

JSON map example:

```json
{
  "tool": "mw_extract",
  "schema_version": 1,
  "source": {
    "path": "Original/BAT.BMP",
    "size": 4152,
    "sha256": "..."
  },
  "entries": [
    {
      "id": "BAT_BMP_0000",
      "kind": "tagged_chunk",
      "tag": "BMP:",
      "offset": 0,
      "header_size": 8,
      "payload_offset": 8,
      "payload_size": 4144,
      "end_offset": 4152,
      "depth": 0,
      "confidence": "high",
      "evidence": ["ASCII tag", "length fits file", "nested flag set"]
    }
  ]
}
```

CSV map fields:

```csv
id,source_file,kind,tag,name,offset_hex,header_size,payload_offset_hex,payload_size,end_offset_hex,depth,confidence,evidence,warnings
```

## Pseudocode

### Binary reader

```pseudo
class Reader:
    data: bytes

    size():
        return len(data)

    u8(offset):
        require offset + 1 <= size
        return data[offset]

    u16le(offset):
        require offset + 2 <= size
        return data[offset] | data[offset + 1] << 8

    u32le(offset):
        require offset + 4 <= size
        return data[offset]
             | data[offset + 1] << 8
             | data[offset + 2] << 16
             | data[offset + 3] << 24

    slice(offset, length):
        require offset + length <= size
        return data[offset : offset + length]
```

### Tagged chunk parser

```pseudo
KNOWN_TAGS = ["BMP:", "PAL:", "FNT:", "INF:", "BIN:", "EGA:", "CGA:"]

function is_tag(bytes4):
    return bytes4 in KNOWN_TAGS

function parse_tagged(reader, start, limit, depth):
    entries = []
    offset = start

    while offset + 8 <= limit:
        tag = reader.slice(offset, 4).ascii()
        if not is_tag(tag):
            break

        raw_length = reader.u32le(offset + 4)
        nested = (raw_length & 0x80000000) != 0
        payload_size = raw_length & 0x7fffffff
        payload_offset = offset + 8
        end = payload_offset + payload_size

        if end > limit:
            entries.add(error_entry(tag, offset, "length exceeds parent/file"))
            break

        entry = {
            kind: "tagged_chunk",
            tag: tag,
            offset: offset,
            header_size: 8,
            payload_offset: payload_offset,
            payload_size: payload_size,
            end_offset: end,
            depth: depth,
            confidence: "high"
        }
        entries.add(entry)

        if nested and payload_offset + 8 <= end:
            child_tag = reader.slice(payload_offset, 4).ascii()
            if is_tag(child_tag):
                entries.extend(parse_tagged(reader, payload_offset, end, depth + 1))

        offset = end

    return entries
```

### Possible offset table scanner

```pseudo
function scan_offset_tables(reader):
    candidates = []

    for table_start in aligned_offsets(0, min(reader.size, 4096), align=[1,2,4]):
        for width in [2, 4]:
            for count in range(4, 512):
                table_size = width * count
                if table_start + table_size > reader.size:
                    break

                values = read_unsigned_le_values(reader, table_start, width, count)

                if not all(0 <= v < reader.size for v in values):
                    continue
                if not is_monotonic_non_decreasing(values):
                    continue
                if unique_count(values) < max(3, count / 2):
                    continue

                first_payload = min(values)
                if first_payload < table_start + table_size:
                    continue

                block_sizes = differences(values + [reader.size])
                if too_many_zero_or_tiny_blocks(block_sizes):
                    continue

                candidates.add({
                    kind: "offset_table",
                    offset: table_start,
                    entry_width: width,
                    entry_count: count,
                    payload_start: first_payload,
                    confidence: score_offset_table(values, block_sizes),
                    evidence: [
                        "monotonic offsets",
                        "offsets inside file",
                        "payload starts after table"
                    ]
                })

    return candidates
```

### Offset/size pair scanner

```pseudo
function scan_offset_size_pairs(reader):
    candidates = []

    for table_start in aligned_offsets(0, min(reader.size, 4096), align=[2,4]):
        for offset_width, size_width in [(2,2), (4,2), (4,4)]:
            entry_size = offset_width + size_width

            for count in range(4, 256):
                ranges = []
                valid = true

                for i in range(count):
                    entry = table_start + i * entry_size
                    off = read_unsigned(reader, entry, offset_width)
                    size = read_unsigned(reader, entry + offset_width, size_width)

                    if size == 0 or off + size > reader.size:
                        valid = false
                        break

                    ranges.add((off, size))

                if not valid:
                    continue
                if ranges_overlap_too_much(ranges):
                    continue

                candidates.add({
                    kind: "offset_size_table",
                    offset: table_start,
                    entry_size: entry_size,
                    entry_count: count,
                    confidence: score_ranges(ranges),
                    evidence: ["offset+size ranges fit file"]
                })

    return candidates
```

### Fixed-record scanner

```pseudo
function scan_fixed_records(reader):
    candidates = []

    for record_size in [3, 4, 6, 7, 8, 10, 12, 14, 16, 20, 24, 32, 48, 64]:
        if reader.size % record_size != 0:
            continue

        record_count = reader.size / record_size
        if record_count < 2:
            continue

        records = split(reader.data, record_size)
        score = score_repeated_structure(records)

        if score >= threshold:
            candidates.add({
                kind: "fixed_records",
                record_size: record_size,
                record_count: record_count,
                confidence: score,
                evidence: ["file size divisible by record size", "repeated byte/word layout"]
            })

    return candidates
```

### Extraction

```pseudo
function extract_entries(reader, map, out_dir):
    ensure out_dir exists
    ensure out_dir is not source directory unless explicitly allowed

    for entry in map.entries:
        if entry.payload_size <= 0:
            continue
        if entry.payload_offset + entry.payload_size > reader.size:
            warn and skip

        bytes = reader.slice(entry.payload_offset, entry.payload_size)
        filename = safe_name(entry)
        write out_dir / filename with bytes
        write sidecar metadata:
            source file
            source sha256
            offset
            size
            tag/name
            extractor version
```

## Safety and legal hygiene

- Do not commit extracted resources.
- Do not embed original hashes as a DRM gate; use hashes only for version identification.
- Do not ship original resource names as required data except names discovered from the user's own files at runtime or documented as parser labels.
- Do not overwrite user files.
- Output should go to a separate analysis/extracted directory.
- Every extracted file should include metadata proving it came from the user's local input file.

## Immediate implementation order

1. Implement `mw_extract scan` for tagged chunks.
2. Implement `mw_extract extract-tagged`.
3. Add synthetic tests for nested chunks.
4. Add `mw_extract strings` using the same string logic as `build_string_candidates.py`.
5. Add `mw_extract map <game-dir>` to combine chunk maps and executable references.
6. Add heuristic scanners for unknown `.BIN/.TBL/.DAT/.GI`.
7. Add manifest-driven extraction after candidate maps are reviewable.

This gives us useful extraction quickly without pretending unknown container formats are solved.
