#!/usr/bin/env python3
"""Build a reusable text catalog from MechWarrior 1989 MW_MAIN.EXE.

The catalog is intentionally external to Original/MW_MAIN.EXE.  It preserves
file offsets and suggested stable ids so engine code can later reference text
without modifying the original executable.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
from dataclasses import asdict, dataclass
from pathlib import Path


SCHEMA_VERSION = 1
DEFAULT_EXE = Path("Original/MW_MAIN.EXE")
DEFAULT_JSON = Path("research/analysis/mw_main_text_catalog.json")
DEFAULT_CSV = Path("research/analysis/mw_main_text_catalog.csv")

PRINTABLE_CONTROLS = {0x09, 0x0A, 0x0D}
MIN_STRING_LEN = 4


@dataclass(frozen=True)
class CategoryRange:
    category: str
    start: int
    end: int
    description: str


CATEGORY_RANGES = [
    CategoryRange("loader_prompt", 0x008D10, 0x009C9F, "loader, disk, password, and resource prompts"),
    CategoryRange("combat_warning", 0x009CC0, 0x009D30, "pre-combat warning text"),
    CategoryRange("intro_text", 0x009F55, 0x00A0FF, "opening setting and betrayal text"),
    CategoryRange("mission_result", 0x00A10D, 0x00A8FF, "post-mission outcomes and Dark Wing combat result text"),
    CategoryRange("credits_auth", 0x00A937, 0x00AEBF, "credits, copyright, and authorization text"),
    CategoryRange("company_status", 0x00AEEB, 0x00B2DF, "company, message, and roster status text"),
    CategoryRange("mechlab_ui", 0x00B2E4, 0x00D4FF, "mech complex, repair, ammo, and mech status text"),
    CategoryRange("contract_ui", 0x00D54F, 0x00D9FF, "contract request and acceptance UI text"),
    CategoryRange("starmap_planet", 0x00E6C4, 0x011756, "house, starmap, planet names, and planet descriptions"),
    CategoryRange("mission_name", 0x01176B, 0x011B28, "mission type names"),
    CategoryRange("crew_recruitment", 0x011D31, 0x011FFF, "crew recruitment UI and short crew lines"),
    CategoryRange("pilot_bio", 0x012001, 0x012938, "hireable pilot names and biography blurbs"),
    CategoryRange("ui_save_hire", 0x012939, 0x012E3F, "save, restore, hiring limit, and disk messages"),
    CategoryRange("rumor", 0x012F40, 0x013841, "bar rumors and early story leads"),
    CategoryRange("main_story", 0x013842, 0x0183C9, "main story scenes and branches"),
    CategoryRange("personal_message", 0x0183CA, 0x018C4D, "personal messages and Matabushi memo chain"),
    CategoryRange("newsnet", 0x018C4E, 0x01C9C6, "NewsNet and story-adjacent articles"),
    CategoryRange("main_story", 0x01C9C7, 0x01DBD7, "late Grig and landing-site story branches"),
    CategoryRange("reputation_flavor", 0x01DC5F, 0x01E040, "faction reputation/location flavor"),
    CategoryRange("newsnet", 0x01E041, 0x01F82D, "Ander's Moon, Matabushi, and Inner Sphere NewsNet articles"),
    CategoryRange("brief_headline", 0x01F82E, 0x020514, "brief dated headlines"),
    CategoryRange("newsnet_endgame", 0x020515, 0x020AAA, "endgame Ander's Moon NewsNet articles"),
    CategoryRange("endgame", 0x020AAB, 0x020E9F, "Dark Wing base and ending prompts"),
]

KNOWN_TITLES = {
    "PERSONAL MESSAGE FROM:": "personal message",
    "BRIEF HEADLINES": "brief headlines",
    "SUDDEN DEATH IS A GRIM REALITY": "sudden death prompt",
    "VALENSIA, ANDER'S MOON": "valensia news",
    "ANDER'S MOON": "ander's moon news",
}


@dataclass
class TextBlock:
    key: str
    suggested_id: str
    assigned_id: str
    source_file: str
    file_offset: int
    file_offset_hex: str
    loaded_offset: int
    loaded_offset_hex: str
    length: int
    category: str
    title: str
    first_line: str
    line_count: int
    has_cr: bool
    has_lf: bool
    terminator: str
    text: str
    notes: str = ""
    tags: list[str] | None = None


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def mz_header_size(data: bytes) -> int:
    if len(data) < 0x20 or data[:2] != b"MZ":
        return 0
    paragraphs = int.from_bytes(data[0x08:0x0A], "little")
    return paragraphs * 16


def is_text_byte(value: int) -> bool:
    return value in PRINTABLE_CONTROLS or 32 <= value <= 126


def looks_like_text(text: str) -> bool:
    stripped = text.strip()
    if len(stripped) < MIN_STRING_LEN:
        return False
    if not any(ch.isalpha() for ch in stripped):
        return False
    alpha = sum(ch.isalpha() for ch in stripped)
    return alpha / max(1, len(stripped)) >= 0.25


def in_known_text_range(offset: int) -> bool:
    return any(item.start <= offset <= item.end for item in CATEGORY_RANGES)


def should_keep_block(offset: int, text: str, category: str) -> bool:
    stripped = text.strip()
    letters = sum(ch.isalpha() for ch in stripped)
    digits = sum(ch.isdigit() for ch in stripped)
    safe_punct = sum(ch in " .,;:'\"!?()/-:%$" for ch in stripped)
    meaningful = letters + digits + safe_punct

    if in_known_text_range(offset):
        if len(stripped) <= 6:
            return bool(re.fullmatch(r"[A-Za-z0-9 .:'\"!?()/%$-]+", stripped)) and letters > 0
        return letters > 0 and meaningful / max(1, len(stripped)) >= 0.65

    if category in {
        "mission_contract",
        "ui",
        "world_label",
        "resource_reference",
    }:
        return True

    if len(stripped) < 12:
        return False
    if not any(ch.isspace() for ch in stripped):
        return False

    return letters >= 6 and meaningful / max(1, len(stripped)) >= 0.80


def extract_ascii_runs(data: bytes) -> list[tuple[int, str, str]]:
    rows: list[tuple[int, str, str]] = []
    start: int | None = None
    buf = bytearray()
    for index, value in enumerate(data):
        if is_text_byte(value):
            if start is None:
                start = index
            buf.append(value)
        else:
            if start is not None and len(buf) >= MIN_STRING_LEN:
                text = bytes(buf).decode("ascii", errors="replace")
                if looks_like_text(text):
                    rows.append((start, text, f"0x{value:02X}"))
            start = None
            buf.clear()
    if start is not None and len(buf) >= MIN_STRING_LEN:
        text = bytes(buf).decode("ascii", errors="replace")
        if looks_like_text(text):
            rows.append((start, text, "EOF"))
    return rows


def classify(offset: int, text: str) -> tuple[str, str]:
    for item in CATEGORY_RANGES:
        if item.start <= offset <= item.end:
            return item.category, item.description

    upper = text.upper()
    if "CONTRACT" in upper or "MISSION" in upper:
        return "mission_contract", "mission and contract text outside the main story range"
    if any(word in upper for word in ("REPAIR", "SAVE GAME", "RESTORE", "SOUND", "PILOT")):
        return "ui", "menu, economy, or pilot UI text outside the main story range"
    if any(word in upper for word in ("HOUSE ", "PLANET", "ANDER", "VEGA", "DONEGAL")):
        return "world_label", "world, house, or location label"
    if re.search(r"\.(EXE|BIN|BMP|PAL|FNT|MUS|SND|DAT|SCR|TBL|GAM)\b", upper):
        return "resource_reference", "resource filename or loader reference"
    return "other_ascii", "other printable ASCII text"


def normalize_lines(text: str) -> list[str]:
    return [line.rstrip() for line in text.replace("\r", "\n").replace("\t", " ").split("\n")]


def title_for(text: str, category: str) -> str:
    lines = [line.strip() for line in normalize_lines(text) if line.strip()]
    if not lines:
        return category
    upper_joined = "\n".join(lines[:3]).upper()
    for marker, title in KNOWN_TITLES.items():
        if marker in upper_joined:
            return title
    if category in {"newsnet", "newsnet_endgame"} and len(lines) >= 3:
        return lines[2][:72]
    if category == "brief_headline" and len(lines) >= 2:
        return lines[1][:72]
    if category == "personal_message":
        return lines[0][:72]
    return lines[0][:72]


def prefix_for(category: str) -> str:
    return {
        "pilot_bio": "pilot",
        "ui_save_hire": "ui",
        "rumor": "rumor",
        "main_story": "story",
        "personal_message": "pm",
        "newsnet": "news",
        "newsnet_endgame": "news",
        "brief_headline": "headline",
        "reputation_flavor": "reputation",
        "endgame": "endgame",
        "mission_contract": "mission",
        "world_label": "world",
        "resource_reference": "resource",
        "ui": "ui",
        "loader_prompt": "loader",
        "combat_warning": "combat",
        "intro_text": "intro",
        "mission_result": "result",
        "credits_auth": "credits",
        "company_status": "company",
        "mechlab_ui": "mechlab",
        "contract_ui": "contract",
        "starmap_planet": "planet",
        "mission_name": "mission",
        "crew_recruitment": "crew",
    }.get(category, "text")


def build_blocks(data: bytes, source_file: str, header_size: int) -> list[TextBlock]:
    blocks: list[TextBlock] = []
    for offset, text, terminator in extract_ascii_runs(data):
        category, _description = classify(offset, text)
        if not should_keep_block(offset, text, category):
            continue
        lines = normalize_lines(text)
        non_empty = [line.strip() for line in lines if line.strip()]
        first_line = non_empty[0] if non_empty else text.strip()[:80]
        loaded_offset = offset - header_size if header_size and offset >= header_size else offset
        prefix = prefix_for(category)
        key = f"mw_main:{offset:06X}:{len(text):04X}"
        block = TextBlock(
            key=key,
            suggested_id=f"mw_main.{prefix}.{offset:06x}",
            assigned_id="",
            source_file=source_file,
            file_offset=offset,
            file_offset_hex=f"0x{offset:06X}",
            loaded_offset=loaded_offset,
            loaded_offset_hex=f"0x{loaded_offset:06X}",
            length=len(text),
            category=category,
            title=title_for(text, category),
            first_line=first_line[:120],
            line_count=max(1, len(lines)),
            has_cr="\r" in text,
            has_lf="\n" in text,
            terminator=terminator,
            text=text,
            tags=[],
        )
        blocks.append(block)
    return blocks


def apply_annotations(blocks: list[TextBlock], annotations_path: Path | None) -> None:
    if not annotations_path:
        return
    payload = json.loads(annotations_path.read_text(encoding="utf-8"))
    rows = payload.get("blocks", payload if isinstance(payload, list) else [])
    by_key = {row.get("key"): row for row in rows if isinstance(row, dict)}
    by_offset = {row.get("file_offset_hex"): row for row in rows if isinstance(row, dict)}
    for block in blocks:
        row = by_key.get(block.key) or by_offset.get(block.file_offset_hex)
        if not row:
            continue
        block.assigned_id = str(row.get("assigned_id") or row.get("id") or "")
        block.category = str(row.get("category") or block.category)
        block.title = str(row.get("title") or block.title)
        block.notes = str(row.get("notes") or "")
        tags = row.get("tags")
        if isinstance(tags, list):
            block.tags = [str(tag) for tag in tags]


def write_json(path: Path, exe_path: Path, data: bytes, blocks: list[TextBlock], header_size: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema": "mw1989.text_catalog",
        "schema_version": SCHEMA_VERSION,
        "source": {
            "path": str(exe_path).replace("\\", "/"),
            "name": exe_path.name,
            "size": len(data),
            "sha256": sha256_bytes(data),
            "mz_header_size": header_size,
        },
        "category_ranges": [asdict(item) for item in CATEGORY_RANGES],
        "blocks": [asdict(block) for block in blocks],
    }
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def write_csv(path: Path, blocks: list[TextBlock]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "key",
        "suggested_id",
        "assigned_id",
        "file_offset_hex",
        "loaded_offset_hex",
        "length",
        "category",
        "title",
        "first_line",
        "line_count",
        "terminator",
        "tags",
        "notes",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for block in blocks:
            row = asdict(block)
            row["tags"] = ",".join(block.tags or [])
            writer.writerow({field: row.get(field, "") for field in fields})


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE, help="Path to MW_MAIN.EXE")
    parser.add_argument("--out-json", type=Path, default=DEFAULT_JSON, help="Catalog JSON output")
    parser.add_argument("--out-csv", type=Path, default=DEFAULT_CSV, help="Catalog CSV output")
    parser.add_argument("--annotations", type=Path, help="Optional existing annotation JSON to merge")
    parser.add_argument("--min-length", type=int, default=MIN_STRING_LEN, help="Reserved for compatibility")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    data = args.exe.read_bytes()
    header_size = mz_header_size(data)
    blocks = build_blocks(data, args.exe.name, header_size)
    apply_annotations(blocks, args.annotations)
    write_json(args.out_json, args.exe, data, blocks, header_size)
    write_csv(args.out_csv, blocks)
    print(f"Wrote {len(blocks)} text blocks to {args.out_json}")
    print(f"Wrote CSV index to {args.out_csv}")


if __name__ == "__main__":
    main()
