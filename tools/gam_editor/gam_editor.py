#!/usr/bin/env python3
"""Editor for known MechWarrior 1989 .GAM save fields.

This intentionally edits only low-risk fields confirmed by save/screenshot and
MW_MAIN.EXE routine analysis. Mech labels are shown but not edited because
changing them can break the external combat module handoff.

The original game does not store a normal calendar day. It stores a day counter
within the displayed month at save offset 0x0031. NEWS NET date records compare
against `(published_day - 1) * 2`, so this editor exposes a friendly "news day"
for testing publication thresholds. GUI saves preserve the raw date counters
unless the friendly date fields were actually changed.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path


GAM_SIZE = 0x718

OFF_MONTH_DAY_COUNTER = 0x0031
OFF_YEAR = 0x0035
OFF_REPUTATION = 0x001D
OFF_PLANET_INDEX = 0x0021
OFF_MONTH_ZERO_BASED = 0x0033
OFF_PERIODIC_14_DAY_COUNTER = 0x0037
OFF_MECH_LABEL = 0x003B
LEN_MECH_LABEL = 11
OFF_RAW_AGE_OR_START_AGE = 0x0047
OFF_MONEY = 0x0049
OFF_FAMILY_ATTITUDES = 0x004D
FAMILY_ATTITUDE_NAMES = ("Kurita", "Steiner", "Marik", "Liao", "Davion")
OFF_MECH_COUNT = 0x00E8
OFF_MECH_CHASSIS_LIST = 0x00EA
MECH_SLOT_COUNT = 12
MECH_RECORD_BASE = 0x0102
MECH_RECORD_STRIDE = 0x1D
MECH_AMMO_BASE = 0x025E
EMPTY_MECH_CHASSIS_ID = 0xFFFF
MAX_WEAPON_SLOTS = 10
OFF_ACTIVE_MECH_CHASSIS = OFF_MECH_CHASSIS_LIST
OFF_PLAYER_JENNER_ENGINE = 0x0102
OFF_PLAYER_JENNER_GYROS = 0x0103
OFF_PLAYER_JENNER_SENSORS = 0x0104
OFF_PLAYER_JENNER_LIFE_SUPPORT = 0x0105
OFF_PLAYER_JENNER_HEAT_SINKS_MISSING = 0x0106
OFF_PLAYER_JENNER_LA_ACTUATOR = 0x0107
OFF_PLAYER_JENNER_RA_ACTUATOR = 0x0108
OFF_PLAYER_JENNER_LL_ACTUATOR = 0x0109
OFF_PLAYER_JENNER_RL_ACTUATOR = 0x010A
OFF_PLAYER_JENNER_JUMP_JETS_MISSING = 0x010B
OFF_PLAYER_JENNER_WEAPON_SRM4_CT_CONDITION = 0x010C
OFF_PLAYER_JENNER_WEAPON_MLAS_RA_A_CONDITION = 0x010D
OFF_PLAYER_JENNER_MLAS_RA_CONDITION = 0x010E
OFF_PLAYER_JENNER_WEAPON_MLAS_LA_A_CONDITION = 0x010F
OFF_PLAYER_JENNER_WEAPON_MLAS_LA_B_CONDITION = 0x0110
OFF_PLAYER_JENNER_ARMOR_DAMAGE_A = 0x0116
OFF_PLAYER_JENNER_ARMOR_DAMAGE_LA = 0x0117
OFF_PLAYER_JENNER_ARMOR_DAMAGE_RL = 0x0118
OFF_PLAYER_JENNER_ARMOR_DAMAGE_LL = 0x0119
OFF_PLAYER_JENNER_ARMOR_DAMAGE_HEAD = 0x011A
OFF_PLAYER_JENNER_ARMOR_DAMAGE_CT = 0x011B
OFF_PLAYER_JENNER_ARMOR_DAMAGE_BACK = 0x011C
OFF_PLAYER_JENNER_ARMOR_DAMAGE_C = 0x011D
OFF_PLAYER_JENNER_ARMOR_DAMAGE_D = 0x011E
OFF_PLAYER_JENNER_SRM4_AMMO = 0x025E
OFF_EXTRA_AMMO_AC5 = 0x02EE
OFF_EXTRA_AMMO_LRM5 = 0x02F0
OFF_EXTRA_AMMO_SRM2 = 0x02F2
OFF_EXTRA_AMMO_SRM4 = 0x02F4
OFF_EXTRA_AMMO_SRM6 = 0x02F6
OFF_EXTRA_AMMO_MG = 0x02F8
OFF_SOUND_DISABLED = 0x05E2
OFF_DETAIL_LEVEL = 0x070D

START_YEAR = 3024
START_MONTH_ONE_BASED = 4
START_MONTH_DAY_COUNTER = 1

DEFAULT_GAM_DIR = Path(__file__).resolve().parents[1] / "Sorted Original Files" / "GAM"
KNOWN_PLANETS = {
    0x00: "LUTHIEN",
    0x01: "PESHT",
    0x02: "QANDAHAR",
    0x03: "NEW SAMARKAND",
    0x04: "TABAYAMA",
    0x05: "GALEDON V",
    0x06: "KAZNEJOV",
    0x07: "THESTRIA",
    0x08: "MISERY",
    0x09: "MATSUIDA",
    0x0A: "OSHIKA",
    0x0B: "IRURZUN",
    0x0C: "PROSERPINA",
    0x0D: "BENJAMIN",
    0x0E: "DIERON",
    0x0F: "KESSEL",
    0x10: "BUCKMINSTER",
    0x11: "KARBALA",
    0x12: "RUBIGEN",
    0x13: "GALUZZO",
    0x14: "ALSHAIN",
    0x15: "RADSTADT",
    0x16: "KIRCHBACH",
    0x17: "RASALHAGUE",
    0x18: "ALBIERO",
    0x19: "VEGA",
    0x1A: "SKOKIE",
    0x1B: "XINYANG",
    0x1C: "DELACRUZ",
    0x1D: "LAND'S END",
    0x1E: "THARKAD",
    0x1F: "DONEGAL",
    0x20: "ALARION",
    0x21: "COVENTRY",
    0x22: "POULSBO",
    0x23: "TIMBUKTU",
    0x24: "BOUNTIFUL HARVEST",
    0x25: "CHUCKCHI III",
    0x26: "SKYE",
    0x27: "ALEXANDRIA",
    0x28: "RAHNE",
    0x29: "HESPERUS II",
    0x2A: "PORT MOSEBY",
    0x2B: "MIZAR",
    0x2C: "SEVREN",
    0x2D: "CARSE",
    0x2E: "KOBE",
    0x2F: "DUSTBALL",
    0x30: "SUK II",
    0x31: "WINFIELD",
    0x32: "ANYWHERE",
    0x33: "TAMAR",
    0x34: "ANEMBO",
    0x35: "TIMBIQUI",
    0x36: "DIXIE",
    0x37: "ARCADIA",
    0x38: "VALLOIRE",
    0x39: "THORIN",
    0x3A: "GARRISON",
    0x3B: "NIANGOL",
    0x3C: "ATREUS",
    0x3D: "ANGELL II",
    0x3E: "MARIK",
    0x3F: "SILVER",
    0x40: "ANDURIEN",
    0x41: "ALULA AUSTRALIS",
    0x42: "GIBSON",
    0x43: "MOSIRO",
    0x44: "CALLOWAY VI",
    0x45: "ORIENTE",
    0x46: "NEW DELOS",
    0x47: "REGULUS",
    0x48: "LESNOVO",
    0x49: "AMITY",
    0x4A: "SHILOH",
    0x4B: "PROCYON",
    0x4C: "TAMARIND",
    0x4D: "CLAYBROOKE",
    0x4E: "IRIAN",
    0x4F: "OLIVER",
    0x50: "SUZANO",
    0x51: "GOODNA",
    0x52: "SADURNI",
    0x53: "CIREBON",
    0x54: "NESTOR",
    0x55: "GRIFFITH",
    0x56: "AUTUMN WIND",
    0x57: "CHALOUBA",
    0x58: "SOPHIE'S WORLD",
    0x59: "ZORTMAN",
    0x5A: "SIAN",
    0x5B: "BETELGEUSE",
    0x5C: "BUENOS AIRES",
    0x5D: "GRAND BASE",
    0x5E: "MENKE",
    0x5F: "TURIN",
    0x60: "TIKONOV",
    0x61: "ALDEBARAN",
    0x62: "BHARAT",
    0x63: "KEID",
    0x64: "NANKING",
    0x65: "NEW HESSEN",
    0x66: "TALL TREES",
    0x67: "CAPELLA",
    0x68: "ARES",
    0x69: "BITHINIA",
    0x6A: "EXEDOR",
    0x6B: "NECROMO",
    0x6C: "RABALLA",
    0x6D: "STYK",
    0x6E: "TSINGHAI",
    0x6F: "WARLOCK",
    0x70: "MATSU",
    0x71: "ZANZIBAR",
    0x72: "MILOS",
    0x73: "NEW AVALON",
    0x74: "NEW SYRTIS",
    0x75: "ROBINSON",
    0x76: "KESAI IV",
    0x77: "GALAX",
    0x78: "MALLORY'S WORLD",
    0x79: "KATHIL",
    0x7A: "REDFIELD",
    0x7B: "OKEFENOKEE",
    0x7C: "GAMBLER",
    0x7D: "MARDUK",
    0x7E: "ANDER'S MOON",
    0x7F: "HYALITE",
    0x80: "TANCREDI IV",
    0x81: "HOBBS",
    0x82: "KITTERY",
    0x83: "GREAT GORGE",
    0x84: "CAPH",
    0x85: "HOFF",
    0x86: "BAXLEY",
    0x87: "GREELY",
    0x88: "COGDELL",
    0x89: "FRAZER",
    0x8A: "MORAVIAN",
    0x8B: "NEW ARAGON",
    0x8C: "NOATAK",
    0x8D: "BEECHER",
    0x8E: "XHOSA VII",
    0x8F: "IMMENSTADT",
    0x90: "DELACAMBRE",
}
DETAIL_LEVELS = {
    0: "low",
    1: "medium",
    2: "high",
}
DETAIL_NAMES = {name: value for value, name in DETAIL_LEVELS.items()}
DETAIL_NAMES["med"] = 1
ORIGINAL_CHASSIS_NAMES = {
    0: "LOCUST",
    1: "WASP",
    2: "JENNER",
    3: "PHOENIX HAWK",
    4: "SHADOW HAWK",
    5: "WOLVERINE",
    6: "RIFLEMAN",
    7: "WARHAMMER",
    8: "MARAUDER",
    9: "BATTLEMASTER",
}
PLAYABLE_CHASSIS_SAVE_IDS = tuple(ORIGINAL_CHASSIS_NAMES)
CHASSIS_NAMES = tuple(ORIGINAL_CHASSIS_NAMES[index] for index in PLAYABLE_CHASSIS_SAVE_IDS)
GLITCH_CHASSIS_NAMES = {"WASP", "WOLVERINE"}


def _chassis_display_name(chassis_name: str) -> str:
    normalized = chassis_name.strip().upper()
    if normalized in GLITCH_CHASSIS_NAMES:
        return f"{normalized} (WILL GLITCH!)"
    return normalized


def _normalize_chassis_name(chassis_name: str) -> str:
    normalized = chassis_name.strip().upper()
    suffix = " (WILL GLITCH!)"
    if normalized.endswith(suffix):
        normalized = normalized[: -len(suffix)]
    return normalized


CHASSIS_CHOICES = ("EMPTY",) + tuple(_chassis_display_name(name) for name in CHASSIS_NAMES)
CHASSIS_SAVE_IDS_BY_NAME = {name: index for index, name in ORIGINAL_CHASSIS_NAMES.items()}
CHASSIS_HEAT_SINK_TOTALS = {
    0: 10,
    1: 10,
    2: 10,
    3: 10,
    4: 12,
    5: 12,
    6: 10,
    7: 18,
    8: 16,
    9: 18,
}
CHASSIS_JUMP_JET_TOTALS = {
    0: 0,
    1: 6,
    2: 3,
    3: 6,
    4: 3,
    5: 5,
    6: 0,
    7: 0,
    8: 0,
    9: 0,
}
MECH_WEAPON_FIELDS_BY_CHASSIS = {
    "LOCUST": (
        ("weapon_0", "M LAS CT", 0, True),
        ("weapon_1", "MG RA", 1, True),
        ("weapon_2", "MG LA", 2, True),
    ),
    "WASP": (
        ("weapon_0", "M LAS RA", 0, False),
        ("weapon_1", "SRM2 LT", 1, False),
    ),
    "JENNER": (
        ("weapon_0", "SRM4 CT", 0, True),
        ("weapon_1", "M LAS RA A", 1, True),
        ("weapon_2", "M LAS RA B", 2, True),
        ("weapon_3", "M LAS LA A", 3, True),
        ("weapon_4", "M LAS LA B", 4, True),
    ),
    "PHOENIX HAWK": (
        ("weapon_0", "L LAS RA", 0, True),
        ("weapon_1", "M LAS RA", 1, True),
        ("weapon_2", "M LAS LA", 2, True),
        ("weapon_3", "MG LA", 3, True),
        ("weapon_4", "MG RA", 4, True),
    ),
    "SHADOW HAWK": (
        ("weapon_0", "AC/5 LT", 0, True),
        ("weapon_1", "LRM5 RT", 1, True),
        ("weapon_2", "SRM2 HD", 2, True),
        ("weapon_3", "M LAS RA", 3, True),
    ),
    "WOLVERINE": (
        ("weapon_0", "AC/5 RA", 0, False),
        ("weapon_1", "SRM6 LT", 1, False),
        ("weapon_2", "M LAS HD", 2, False),
    ),
    "RIFLEMAN": (
        ("weapon_0", "L LAS RA", 0, True),
        ("weapon_1", "L LAS LA", 1, True),
        ("weapon_2", "AC/5 RA", 2, True),
        ("weapon_3", "AC/5 LA", 3, True),
        ("weapon_4", "M LAS RT", 4, True),
        ("weapon_5", "M LAS LT", 5, True),
    ),
    "WARHAMMER": (
        ("weapon_0", "PPC RA", 0, True),
        ("weapon_1", "PPC LA", 1, True),
        ("weapon_2", "SRM6 RT", 2, True),
        ("weapon_3", "M LAS RT", 3, True),
        ("weapon_4", "M LAS LT", 4, True),
        ("weapon_5", "S LAS RT", 5, True),
        ("weapon_6", "S LAS LT", 6, True),
        ("weapon_7", "MG RT", 7, True),
        ("weapon_8", "MG LT", 8, True),
    ),
    "MARAUDER": (
        ("weapon_0", "PPC RA", 0, True),
        ("weapon_1", "PPC LA", 1, True),
        ("weapon_2", "M LAS RA", 2, True),
        ("weapon_3", "M LAS LA", 3, True),
        ("weapon_4", "AC/5 RT", 4, True),
    ),
    "BATTLEMASTER": (
        ("weapon_0", "PPC RA", 0, True),
        ("weapon_1", "M LAS RT A", 1, True),
        ("weapon_2", "M LAS RT B", 2, True),
        ("weapon_3", "M LAS RT C", 3, True),
        ("weapon_4", "MG LA A", 4, True),
        ("weapon_5", "MG LA B", 5, True),
        ("weapon_6", "SRM6 LT", 6, True),
        ("weapon_7", "M LAS LT A", 7, True),
        ("weapon_8", "M LAS LT B", 8, True),
        ("weapon_9", "M LAS LT C", 9, True),
    ),
}
MECH_AMMO_LABEL_BY_CHASSIS = {
    "LOCUST": "MG ammo",
    "WASP": "SRM2 ammo",
    "JENNER": "SRM4 ammo",
    "PHOENIX HAWK": "MG ammo",
    "WOLVERINE": "AC/SRM6 ammo",
    "WARHAMMER": "MG/SRM6 ammo",
    "BATTLEMASTER": "MG/SRM6 ammo",
    "SHADOW HAWK": "AC/LRM/SRM ammo",
    "RIFLEMAN": "AC/5 ammo",
    "MARAUDER": "AC/5 ammo",
}
MECH_AMMO_MAX_BY_CHASSIS = {
    "LOCUST": 200,
    "JENNER": 25,
    "PHOENIX HAWK": 200,
}
ARMOR_QUALITY_LABELS_BY_DAMAGE = {
    0: "100%",
    1: "66%",
    2: "33%",
    3: "0%",
}
ARMOR_DAMAGE_BY_QUALITY_LABEL = {
    label: damage for damage, label in ARMOR_QUALITY_LABELS_BY_DAMAGE.items()
}
ARMOR_QUALITY_VALUES = tuple(ARMOR_QUALITY_LABELS_BY_DAMAGE[value] for value in range(4))
DAMAGE_STATES = {
    0: "functional",
    1: "light damage",
    2: "heavy damage",
    3: "junk",
}
DAMAGE_NAMES = {
    "functional": 0,
    "ok": 0,
    "0": 0,
    "light": 1,
    "light damage": 1,
    "1": 1,
    "heavy": 2,
    "heavy damage": 2,
    "2": 2,
    "junk": 3,
    "3": 3,
}
PLAYER_JENNER_HEAT_SINKS_TOTAL = 10
PLAYER_JENNER_JUMP_JETS_TOTAL = 3
PLAYER_JENNER_ARMOR_DAMAGE_DENOMINATOR = 27
PLAYER_JENNER_ARMOR_DAMAGE_OFFSETS = (
    OFF_PLAYER_JENNER_ARMOR_DAMAGE_A,
    OFF_PLAYER_JENNER_ARMOR_DAMAGE_LA,
    OFF_PLAYER_JENNER_ARMOR_DAMAGE_RL,
    OFF_PLAYER_JENNER_ARMOR_DAMAGE_LL,
    OFF_PLAYER_JENNER_ARMOR_DAMAGE_HEAD,
    OFF_PLAYER_JENNER_ARMOR_DAMAGE_CT,
    OFF_PLAYER_JENNER_ARMOR_DAMAGE_BACK,
    OFF_PLAYER_JENNER_ARMOR_DAMAGE_C,
    OFF_PLAYER_JENNER_ARMOR_DAMAGE_D,
)
PLAYER_JENNER_ARMOR_FIELDS = (
    ("ra", "RA", OFF_PLAYER_JENNER_ARMOR_DAMAGE_A),
    ("la", "LA", OFF_PLAYER_JENNER_ARMOR_DAMAGE_LA),
    ("rl", "RL", OFF_PLAYER_JENNER_ARMOR_DAMAGE_RL),
    ("ll", "LL", OFF_PLAYER_JENNER_ARMOR_DAMAGE_LL),
    ("head", "HEAD", OFF_PLAYER_JENNER_ARMOR_DAMAGE_HEAD),
    ("ct", "CT", OFF_PLAYER_JENNER_ARMOR_DAMAGE_CT),
    ("back", "BACK", OFF_PLAYER_JENNER_ARMOR_DAMAGE_BACK),
    ("tr", "TR", OFF_PLAYER_JENNER_ARMOR_DAMAGE_C),
    ("tl", "TL", OFF_PLAYER_JENNER_ARMOR_DAMAGE_D),
)
PLAYER_JENNER_ARMOR_PRESETS = {
    "data2-77": (1, 0, 0, 0, 0, 1, 0, 3, 1),
    "data3-85": (0, 0, 0, 0, 0, 0, 0, 3, 1),
    "data6-66": (0, 1, 1, 0, 3, 0, 3, 1, 0),
    "dat0-74": (3, 3, 0, 0, 0, 0, 0, 1, 0),
    "dat2-85": (0, 3, 0, 0, 0, 0, 0, 1, 0),
    "data10-70": (0, 0, 1, 0, 3, 0, 3, 1, 0),
    "data11-74": (0, 0, 0, 0, 3, 0, 3, 1, 0),
    "data12-85": (0, 0, 0, 0, 0, 0, 3, 1, 0),
    "data13-96": (0, 0, 0, 0, 0, 0, 0, 1, 0),
    "100": (0, 0, 0, 0, 0, 0, 0, 0, 0),
}
PLAYER_JENNER_ARMOR_ALIASES = {
    "77": "data2-77",
    "77%": "data2-77",
    "data2-77": "data2-77",
    "damaged": "data2-77",
    "85": "data3-85",
    "85%": "data3-85",
    "data3-85": "data3-85",
    "partial": "data3-85",
    "66": "data6-66",
    "66%": "data6-66",
    "data6-66": "data6-66",
    "dat0-74": "dat0-74",
    "dat2-85": "dat2-85",
    "70": "data10-70",
    "70%": "data10-70",
    "data10-70": "data10-70",
    "74": "data11-74",
    "74%": "data11-74",
    "data11-74": "data11-74",
    "data12-85": "data12-85",
    "96": "data13-96",
    "96%": "data13-96",
    "data13-96": "data13-96",
    "100": "100",
    "100%": "100",
    "data14-100": "100",
    "full": "100",
    "repaired": "100",
}
EXTRA_AMMO_MAX_IN_HOLD = 9999
EXTRA_AMMO_FIELDS = (
    ("ac5", "AC 5-PKS", OFF_EXTRA_AMMO_AC5),
    ("lrm5", "LRM 5-PKS", OFF_EXTRA_AMMO_LRM5),
    ("srm2", "SRM 2-PKS", OFF_EXTRA_AMMO_SRM2),
    ("srm4", "SRM 4-PKS", OFF_EXTRA_AMMO_SRM4),
    ("srm6", "SRM 6-PKS", OFF_EXTRA_AMMO_SRM6),
    ("mg", "MACH GUN", OFF_EXTRA_AMMO_MG),
)
EXTRA_AMMO_OFFSETS_BY_KEY = {
    key: offset for key, _label, offset in EXTRA_AMMO_FIELDS
}
JENNER_COMPONENT_FIELDS = (
    ("engine", "ENGINE", 0, True),
    ("gyros", "GYROS", 1, True),
    ("sensors", "SENSORS", 2, True),
    ("life_support", "LIFE SUPPORT", 3, True),
    ("la_actuator", "LA ACTUATOR", 5, True),
    ("ra_actuator", "RA ACTUATOR", 6, True),
    ("ll_actuator", "LL ACTUATOR", 7, True),
    ("rl_actuator", "RL ACTUATOR", 8, True),
)
JENNER_COMPONENT_OFFSETS_BY_KEY = {
    key: OFF_PLAYER_JENNER_ENGINE + rel for key, _label, rel, _confirmed in JENNER_COMPONENT_FIELDS
}
JENNER_WEAPON_FIELDS = MECH_WEAPON_FIELDS_BY_CHASSIS["JENNER"]
JENNER_WEAPON_ALIASES_BY_KEY = {
    "srm4_ct": 0,
    "mlas_ra_a": 1,
    "mlas_ra_b": 2,
    "mlas_la_a": 3,
    "mlas_la_b": 4,
}
JENNER_WEAPON_OFFSETS_BY_KEY = {
    key: OFF_PLAYER_JENNER_WEAPON_SRM4_CT_CONDITION + index
    for key, index in JENNER_WEAPON_ALIASES_BY_KEY.items()
}
JENNER_ARMOR_OFFSETS_BY_KEY = {
    key: offset for key, _label, offset in PLAYER_JENNER_ARMOR_FIELDS
}
JENNER_ARMOR_OFFSETS_BY_KEY.update({
    "unk1": OFF_PLAYER_JENNER_ARMOR_DAMAGE_CT,
    "unk2": OFF_PLAYER_JENNER_ARMOR_DAMAGE_BACK,
    "old_ct": OFF_PLAYER_JENNER_ARMOR_DAMAGE_HEAD,
})


class GamError(ValueError):
    pass


def _read_u16le(data: bytearray, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little", signed=False)


def _write_u16le(data: bytearray, offset: int, value: int) -> None:
    data[offset : offset + 2] = value.to_bytes(2, "little", signed=False)


def _read_i16le(data: bytearray, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little", signed=True)


def _write_i16le(data: bytearray, offset: int, value: int) -> None:
    data[offset : offset + 2] = value.to_bytes(2, "little", signed=True)


def _read_u32le(data: bytearray, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little", signed=False)


def _write_u32le(data: bytearray, offset: int, value: int) -> None:
    data[offset : offset + 4] = value.to_bytes(4, "little", signed=False)


def _validate_int(label: str, value: int, min_value: int, max_value: int) -> int:
    if not min_value <= value <= max_value:
        raise GamError(f"{label} must be in range {min_value}..{max_value}")
    return value


def _parse_date(value: str) -> tuple[int, int, int]:
    parts = value.split("-")
    if len(parts) != 3:
        raise GamError("Date must use YYYY-MM-DD")
    try:
        year, month, day = (int(part, 10) for part in parts)
    except ValueError as exc:
        raise GamError("Date must use decimal numbers in YYYY-MM-DD") from exc
    _validate_int("Year", year, 0, 0xFFFF)
    _validate_int("Month", month, 1, 12)
    _validate_int("News day", day, 1, 30)
    return year, month, day


@dataclass
class GamSave:
    path: Path
    data: bytearray

    @classmethod
    def load(cls, path: Path | str) -> "GamSave":
        path = Path(path)
        data = bytearray(path.read_bytes())
        if len(data) != GAM_SIZE:
            raise GamError(f"{path} is {len(data)} bytes; expected {GAM_SIZE} bytes")
        return cls(path=path, data=data)

    @property
    def mech_label(self) -> str:
        raw = bytes(self.data[OFF_MECH_LABEL : OFF_MECH_LABEL + LEN_MECH_LABEL])
        raw = raw.split(b"\0", 1)[0]
        return raw.decode("ascii", errors="replace").rstrip()

    @mech_label.setter
    def mech_label(self, value: str) -> None:
        encoded = value.encode("ascii", errors="strict")
        if len(encoded) > LEN_MECH_LABEL - 1:
            raise GamError(f"Mech label must be ASCII and at most {LEN_MECH_LABEL - 1} chars")
        self.data[OFF_MECH_LABEL : OFF_MECH_LABEL + LEN_MECH_LABEL] = encoded.ljust(
            LEN_MECH_LABEL, b"\0"
        )

    @property
    def commander(self) -> str:
        """Backward-compatible alias for older CLI/API use."""
        return self.mech_label

    @commander.setter
    def commander(self, value: str) -> None:
        self.mech_label = value

    @property
    def age(self) -> int:
        return self.year - 3006

    @property
    def raw_age_or_start_age(self) -> int:
        return self.data[OFF_RAW_AGE_OR_START_AGE]

    @property
    def planet_index(self) -> int:
        return self.data[OFF_PLANET_INDEX]

    @property
    def planet_name(self) -> str:
        return KNOWN_PLANETS.get(self.planet_index, "Unknown")

    @property
    def money(self) -> int:
        return _read_u32le(self.data, OFF_MONEY)

    @money.setter
    def money(self, value: int) -> None:
        _write_u32le(self.data, OFF_MONEY, _validate_int("Money", value, 0, 0xFFFFFFFF))

    def family_attitude(self, index: int) -> int:
        _validate_int("Family attitude index", index, 0, len(FAMILY_ATTITUDE_NAMES) - 1)
        return _read_i16le(self.data, OFF_FAMILY_ATTITUDES + index * 2)

    def set_family_attitude(self, index: int, value: int) -> None:
        _validate_int("Family attitude index", index, 0, len(FAMILY_ATTITUDE_NAMES) - 1)
        _write_i16le(
            self.data,
            OFF_FAMILY_ATTITUDES + index * 2,
            _validate_int(FAMILY_ATTITUDE_NAMES[index], value, -0x8000, 0x7FFF),
        )

    @property
    def family_attitudes(self) -> dict[str, int]:
        return {
            name: self.family_attitude(index)
            for index, name in enumerate(FAMILY_ATTITUDE_NAMES)
        }

    @property
    def year(self) -> int:
        return _read_u16le(self.data, OFF_YEAR)

    @year.setter
    def year(self, value: int) -> None:
        _write_u16le(self.data, OFF_YEAR, _validate_int("Year", value, 0, 0xFFFF))

    @property
    def month(self) -> int:
        return self.data[OFF_MONTH_ZERO_BASED] + 1

    @month.setter
    def month(self, value: int) -> None:
        self.data[OFF_MONTH_ZERO_BASED] = _validate_int("Month", value, 1, 12) - 1

    @property
    def month_day_counter(self) -> int:
        return self.data[OFF_MONTH_DAY_COUNTER]

    @month_day_counter.setter
    def month_day_counter(self, value: int) -> None:
        self.data[OFF_MONTH_DAY_COUNTER] = _validate_int("Month day counter", value, 0, 59)

    @property
    def news_day(self) -> int:
        return self.month_day_counter // 2 + 1

    @news_day.setter
    def news_day(self, value: int) -> None:
        self.month_day_counter = (_validate_int("News day", value, 1, 30) - 1) * 2

    @property
    def periodic_14_day_counter(self) -> int:
        return self.data[OFF_PERIODIC_14_DAY_COUNTER]

    @periodic_14_day_counter.setter
    def periodic_14_day_counter(self, value: int) -> None:
        self.data[OFF_PERIODIC_14_DAY_COUNTER] = _validate_int(
            "14-day periodic counter", value, 0, 13
        )

    @property
    def news_date(self) -> str:
        return f"{self.year:04d}-{self.month:02d}-{self.news_day:02d}"

    def set_news_date(self, year: int, month: int, day: int) -> None:
        self.year = year
        self.month = month
        self.news_day = day

    def internal_days_since_start(self) -> int:
        return (
            (self.year - START_YEAR) * 12 * 60
            + (self.month - START_MONTH_ONE_BASED) * 60
            + (self.month_day_counter - START_MONTH_DAY_COUNTER)
        )

    def sync_periodic_counter_from_date(self) -> None:
        self.periodic_14_day_counter = self.internal_days_since_start() % 14

    @property
    def reputation_index(self) -> int:
        return self.data[OFF_REPUTATION]

    @property
    def reputation_text(self) -> str:
        if self.reputation_index in (1, 2):
            return "Worth Watching"
        return "Unknown"

    @staticmethod
    def _validate_slot_index(slot_index: int) -> int:
        return _validate_int("Mech slot", slot_index, 0, MECH_SLOT_COUNT - 1)

    @staticmethod
    def slot_record_base(slot_index: int) -> int:
        return MECH_RECORD_BASE + GamSave._validate_slot_index(slot_index) * MECH_RECORD_STRIDE

    @staticmethod
    def slot_record_offset(slot_index: int, relative_offset: int) -> int:
        return GamSave.slot_record_base(slot_index) + relative_offset

    @staticmethod
    def slot_chassis_offset(slot_index: int) -> int:
        return OFF_MECH_CHASSIS_LIST + GamSave._validate_slot_index(slot_index) * 2

    @staticmethod
    def slot_ammo_offset(slot_index: int) -> int:
        return MECH_AMMO_BASE + GamSave._validate_slot_index(slot_index) * 2

    @property
    def mech_count(self) -> int:
        return _read_u16le(self.data, OFF_MECH_COUNT)

    @mech_count.setter
    def mech_count(self, value: int) -> None:
        _write_u16le(self.data, OFF_MECH_COUNT, _validate_int("Mech count", value, 0, MECH_SLOT_COUNT))

    def recompute_mech_count(self) -> None:
        self.mech_count = sum(
            1 for slot_index in range(MECH_SLOT_COUNT) if self.slot_chassis_save_id(slot_index) != EMPTY_MECH_CHASSIS_ID
        )

    def slot_chassis_save_id(self, slot_index: int) -> int:
        return _read_u16le(self.data, self.slot_chassis_offset(slot_index))

    def set_slot_chassis_save_id(self, slot_index: int, value: int) -> None:
        if value != EMPTY_MECH_CHASSIS_ID:
            _validate_int("Chassis id", value, 0, 9)
        _write_u16le(self.data, self.slot_chassis_offset(slot_index), value)
        self.recompute_mech_count()

    def slot_chassis_name(self, slot_index: int) -> str:
        chassis_id = self.slot_chassis_save_id(slot_index)
        if chassis_id == EMPTY_MECH_CHASSIS_ID:
            return "EMPTY"
        return ORIGINAL_CHASSIS_NAMES.get(chassis_id, f"UNKNOWN ({chassis_id})")

    def set_slot_chassis_name(self, slot_index: int, chassis_name: str) -> None:
        normalized = _normalize_chassis_name(chassis_name)
        previous_chassis_id = self.slot_chassis_save_id(slot_index)
        if normalized == "EMPTY":
            self.set_slot_chassis_save_id(slot_index, EMPTY_MECH_CHASSIS_ID)
            self.clear_mech_slot_payload(slot_index)
            return
        try:
            chassis_id = CHASSIS_SAVE_IDS_BY_NAME[normalized]
        except KeyError as exc:
            raise GamError(f"Unknown chassis: {chassis_name}") from exc
        if chassis_id not in PLAYABLE_CHASSIS_SAVE_IDS:
            raise GamError(f"Chassis is not exposed for normal editing: {chassis_name}")
        self.set_slot_chassis_save_id(slot_index, chassis_id)
        if previous_chassis_id != chassis_id:
            self.reset_slot_chassis_dependent_counts(slot_index)

    def clear_mech_slot_payload(self, slot_index: int) -> None:
        start = self.slot_record_base(slot_index)
        self.data[start : start + MECH_RECORD_STRIDE] = b"\0" * MECH_RECORD_STRIDE
        _write_u16le(self.data, self.slot_ammo_offset(slot_index), 0)

    def reset_slot_chassis_dependent_counts(self, slot_index: int) -> None:
        self.data[self.slot_record_offset(slot_index, 4)] = 0
        self.data[self.slot_record_offset(slot_index, 9)] = 0

    @property
    def active_chassis_save_id(self) -> int:
        return self.slot_chassis_save_id(0)

    @property
    def active_chassis_name(self) -> str:
        return self.slot_chassis_name(0)

    @staticmethod
    def heat_sinks_total_for_chassis(chassis_name: str) -> int:
        normalized = _normalize_chassis_name(chassis_name)
        if normalized == "EMPTY":
            return 0
        chassis_id = CHASSIS_SAVE_IDS_BY_NAME.get(normalized)
        if chassis_id is None:
            return PLAYER_JENNER_HEAT_SINKS_TOTAL
        return CHASSIS_HEAT_SINK_TOTALS.get(chassis_id, PLAYER_JENNER_HEAT_SINKS_TOTAL)

    @staticmethod
    def jump_jets_total_for_chassis(chassis_name: str) -> int:
        normalized = _normalize_chassis_name(chassis_name)
        if normalized == "EMPTY":
            return 0
        chassis_id = CHASSIS_SAVE_IDS_BY_NAME.get(normalized)
        if chassis_id is None:
            return PLAYER_JENNER_JUMP_JETS_TOTAL
        return CHASSIS_JUMP_JET_TOTALS.get(chassis_id, PLAYER_JENNER_JUMP_JETS_TOTAL)

    @staticmethod
    def weapon_fields_for_chassis(chassis_name: str) -> tuple[tuple[str, str, int, bool], ...]:
        return MECH_WEAPON_FIELDS_BY_CHASSIS.get(_normalize_chassis_name(chassis_name), ())

    @staticmethod
    def ammo_label_for_chassis(chassis_name: str) -> str:
        return MECH_AMMO_LABEL_BY_CHASSIS.get(_normalize_chassis_name(chassis_name), "Ammo")

    @staticmethod
    def ammo_max_for_chassis(chassis_name: str) -> int:
        normalized = _normalize_chassis_name(chassis_name)
        if normalized == "EMPTY":
            return 0
        return MECH_AMMO_MAX_BY_CHASSIS.get(normalized, 255)

    def active_heat_sinks_total(self) -> int:
        return self.heat_sinks_total_for_chassis(self.active_chassis_name)

    def active_jump_jets_total(self) -> int:
        return self.jump_jets_total_for_chassis(self.active_chassis_name)

    def active_weapon_fields(self) -> tuple[tuple[str, str, int, bool], ...]:
        return self.weapon_fields_for_chassis(self.active_chassis_name)

    @property
    def active_ammo_label(self) -> str:
        return self.ammo_label_for_chassis(self.active_chassis_name)

    @property
    def active_ammo_max(self) -> int:
        return self.ammo_max_for_chassis(self.active_chassis_name)

    def slot_heat_sinks_total(self, slot_index: int) -> int:
        return self.heat_sinks_total_for_chassis(self.slot_chassis_name(slot_index))

    def slot_jump_jets_total(self, slot_index: int) -> int:
        return self.jump_jets_total_for_chassis(self.slot_chassis_name(slot_index))

    def slot_ammo_label(self, slot_index: int) -> str:
        return self.ammo_label_for_chassis(self.slot_chassis_name(slot_index))

    def slot_ammo_max(self, slot_index: int) -> int:
        return self.ammo_max_for_chassis(self.slot_chassis_name(slot_index))

    def slot_weapon_fields(self, slot_index: int) -> tuple[tuple[str, str, int, bool], ...]:
        return self.weapon_fields_for_chassis(self.slot_chassis_name(slot_index))

    def slot_component_condition(self, slot_index: int, key: str) -> int:
        try:
            relative_offset = next(rel for item_key, _label, rel, _confirmed in JENNER_COMPONENT_FIELDS if item_key == key)
        except StopIteration as exc:
            raise GamError(f"Unknown mech component key: {key}") from exc
        return self.data[self.slot_record_offset(slot_index, relative_offset)]

    def set_slot_component_condition(self, slot_index: int, key: str, value: int) -> None:
        try:
            relative_offset = next(rel for item_key, _label, rel, _confirmed in JENNER_COMPONENT_FIELDS if item_key == key)
        except StopIteration as exc:
            raise GamError(f"Unknown mech component key: {key}") from exc
        self.data[self.slot_record_offset(slot_index, relative_offset)] = _validate_int(
            f"Mech slot {slot_index + 1} {key} condition", value, 0, 3
        )

    def slot_heat_sinks_working(self, slot_index: int) -> int:
        missing = self.data[self.slot_record_offset(slot_index, 4)]
        return self.slot_heat_sinks_total(slot_index) - missing

    def set_slot_heat_sinks_working(self, slot_index: int, value: int) -> None:
        total = self.slot_heat_sinks_total(slot_index)
        working = _validate_int(f"Mech slot {slot_index + 1} heat sinks working", value, 0, total)
        self.data[self.slot_record_offset(slot_index, 4)] = total - working

    def slot_jump_jets_working(self, slot_index: int) -> int:
        missing = self.data[self.slot_record_offset(slot_index, 9)]
        return self.slot_jump_jets_total(slot_index) - missing

    def set_slot_jump_jets_working(self, slot_index: int, value: int) -> None:
        total = self.slot_jump_jets_total(slot_index)
        working = _validate_int(f"Mech slot {slot_index + 1} jump jets working", value, 0, total)
        self.data[self.slot_record_offset(slot_index, 9)] = total - working

    def slot_weapon_condition(self, slot_index: int, weapon_index: int) -> int:
        _validate_int("Weapon slot", weapon_index, 0, MAX_WEAPON_SLOTS - 1)
        return self.data[self.slot_record_offset(slot_index, 10 + weapon_index)]

    def set_slot_weapon_condition(self, slot_index: int, weapon_index: int, value: int) -> None:
        _validate_int("Weapon slot", weapon_index, 0, MAX_WEAPON_SLOTS - 1)
        self.data[self.slot_record_offset(slot_index, 10 + weapon_index)] = _validate_int(
            f"Mech slot {slot_index + 1} weapon condition", value, 0, 3
        )

    def clear_unused_slot_weapon_conditions(self, slot_index: int) -> None:
        active_count = len(self.slot_weapon_fields(slot_index))
        for weapon_index in range(active_count, MAX_WEAPON_SLOTS):
            self.set_slot_weapon_condition(slot_index, weapon_index, 0)

    def slot_armor_damage(self, slot_index: int, key: str) -> int:
        try:
            offset = JENNER_ARMOR_OFFSETS_BY_KEY[key]
        except KeyError as exc:
            raise GamError(f"Unknown mech armor key: {key}") from exc
        return self.data[self.slot_record_offset(slot_index, offset - MECH_RECORD_BASE)]

    def set_slot_armor_damage(self, slot_index: int, key: str, value: int) -> None:
        try:
            offset = JENNER_ARMOR_OFFSETS_BY_KEY[key]
        except KeyError as exc:
            raise GamError(f"Unknown mech armor key: {key}") from exc
        self.data[self.slot_record_offset(slot_index, offset - MECH_RECORD_BASE)] = _validate_int(
            f"Mech slot {slot_index + 1} {key} armor damage", value, 0, 3
        )

    def slot_armor_damage_raw(self, slot_index: int) -> tuple[int, ...]:
        return tuple(
            self.data[self.slot_record_offset(slot_index, offset - MECH_RECORD_BASE)]
            for offset in PLAYER_JENNER_ARMOR_DAMAGE_OFFSETS
        )

    def set_slot_armor_damage_raw(self, slot_index: int, values: tuple[int, ...]) -> None:
        if len(values) != len(PLAYER_JENNER_ARMOR_DAMAGE_OFFSETS):
            raise GamError("Mech armor damage needs the observed raw byte tuple")
        for offset, value in zip(PLAYER_JENNER_ARMOR_DAMAGE_OFFSETS, values):
            self.data[self.slot_record_offset(slot_index, offset - MECH_RECORD_BASE)] = _validate_int(
                "Mech armor damage byte", value, 0, 0xFF
            )

    def slot_armor_percent(self, slot_index: int) -> int:
        damage = sum(self.slot_armor_damage_raw(slot_index))
        remaining = max(0, PLAYER_JENNER_ARMOR_DAMAGE_DENOMINATOR - damage)
        return (remaining * 100) // PLAYER_JENNER_ARMOR_DAMAGE_DENOMINATOR

    def slot_armor_text(self, slot_index: int) -> str:
        raw = ",".join(str(value) for value in self.slot_armor_damage_raw(slot_index))
        return f"{self.slot_armor_percent(slot_index)}% ({raw})"

    def slot_ammo(self, slot_index: int) -> int:
        return _read_u16le(self.data, self.slot_ammo_offset(slot_index))

    def set_slot_ammo(self, slot_index: int, value: int) -> None:
        _write_u16le(
            self.data,
            self.slot_ammo_offset(slot_index),
            _validate_int(
                f"Mech slot {slot_index + 1} {self.slot_ammo_label(slot_index)}",
                value,
                0,
                self.slot_ammo_max(slot_index),
            ),
        )

    def jenner_component_condition(self, key: str) -> int:
        try:
            offset = JENNER_COMPONENT_OFFSETS_BY_KEY[key]
        except KeyError as exc:
            raise GamError(f"Unknown Jenner component key: {key}") from exc
        return self.data[offset]

    def set_jenner_component_condition(self, key: str, value: int) -> None:
        try:
            offset = JENNER_COMPONENT_OFFSETS_BY_KEY[key]
        except KeyError as exc:
            raise GamError(f"Unknown Jenner component key: {key}") from exc
        self.data[offset] = _validate_int(f"Jenner {key} condition", value, 0, 3)

    def jenner_weapon_condition(self, key: str) -> int:
        if key.startswith("weapon_"):
            try:
                return self.slot_weapon_condition(0, int(key.removeprefix("weapon_"), 10))
            except ValueError as exc:
                raise GamError(f"Unknown Jenner weapon key: {key}") from exc
        try:
            offset = JENNER_WEAPON_OFFSETS_BY_KEY[key]
        except KeyError as exc:
            raise GamError(f"Unknown Jenner weapon key: {key}") from exc
        return self.data[offset]

    def set_jenner_weapon_condition(self, key: str, value: int) -> None:
        if key.startswith("weapon_"):
            try:
                self.set_slot_weapon_condition(0, int(key.removeprefix("weapon_"), 10), value)
                return
            except ValueError as exc:
                raise GamError(f"Unknown Jenner weapon key: {key}") from exc
        try:
            offset = JENNER_WEAPON_OFFSETS_BY_KEY[key]
        except KeyError as exc:
            raise GamError(f"Unknown Jenner weapon key: {key}") from exc
        self.data[offset] = _validate_int(f"Jenner {key} condition", value, 0, 3)

    def jenner_armor_damage(self, key: str) -> int:
        try:
            offset = JENNER_ARMOR_OFFSETS_BY_KEY[key]
        except KeyError as exc:
            raise GamError(f"Unknown Jenner armor key: {key}") from exc
        return self.data[offset]

    def set_jenner_armor_damage(self, key: str, value: int) -> None:
        try:
            offset = JENNER_ARMOR_OFFSETS_BY_KEY[key]
        except KeyError as exc:
            raise GamError(f"Unknown Jenner armor key: {key}") from exc
        self.data[offset] = _validate_int(f"Jenner {key} armor damage", value, 0, 3)

    @property
    def inferred_chassis_name(self) -> str:
        return self.active_chassis_name

    @staticmethod
    def damage_text(value: int) -> str:
        return DAMAGE_STATES.get(value, f"raw {value}")

    @property
    def player_jenner_sensors(self) -> int:
        return self.jenner_component_condition("sensors")

    @player_jenner_sensors.setter
    def player_jenner_sensors(self, value: int) -> None:
        self.set_jenner_component_condition("sensors", value)

    @property
    def player_jenner_sensors_text(self) -> str:
        return self.damage_text(self.player_jenner_sensors)

    @property
    def player_jenner_life_support(self) -> int:
        return self.jenner_component_condition("life_support")

    @player_jenner_life_support.setter
    def player_jenner_life_support(self, value: int) -> None:
        self.set_jenner_component_condition("life_support", value)

    @property
    def player_jenner_life_support_text(self) -> str:
        return self.damage_text(self.player_jenner_life_support)

    @property
    def player_jenner_ra_actuator(self) -> int:
        return self.jenner_component_condition("ra_actuator")

    @player_jenner_ra_actuator.setter
    def player_jenner_ra_actuator(self, value: int) -> None:
        self.set_jenner_component_condition("ra_actuator", value)

    @property
    def player_jenner_ra_actuator_text(self) -> str:
        return self.damage_text(self.player_jenner_ra_actuator)

    @property
    def player_jenner_heat_sinks_working(self) -> int:
        return self.slot_heat_sinks_working(0)

    @player_jenner_heat_sinks_working.setter
    def player_jenner_heat_sinks_working(self, value: int) -> None:
        self.set_slot_heat_sinks_working(0, value)

    @property
    def active_jump_jets_working(self) -> int:
        return self.slot_jump_jets_working(0)

    @active_jump_jets_working.setter
    def active_jump_jets_working(self, value: int) -> None:
        self.set_slot_jump_jets_working(0, value)

    @property
    def player_jenner_mlas_ra_condition(self) -> int:
        return self.jenner_weapon_condition("mlas_ra_b")

    @player_jenner_mlas_ra_condition.setter
    def player_jenner_mlas_ra_condition(self, value: int) -> None:
        self.set_jenner_weapon_condition("mlas_ra_b", value)

    @property
    def player_jenner_mlas_ra_condition_text(self) -> str:
        return DAMAGE_STATES.get(
            self.player_jenner_mlas_ra_condition,
            f"raw {self.player_jenner_mlas_ra_condition}",
        )

    @property
    def player_jenner_armor_damage_raw(self) -> tuple[int, ...]:
        return self.slot_armor_damage_raw(0)

    @player_jenner_armor_damage_raw.setter
    def player_jenner_armor_damage_raw(self, values: tuple[int, ...]) -> None:
        if len(values) != len(PLAYER_JENNER_ARMOR_DAMAGE_OFFSETS):
            raise GamError("Player Jenner armor damage needs the observed raw byte tuple")
        self.set_slot_armor_damage_raw(0, values)

    @property
    def player_jenner_armor_percent(self) -> int:
        damage = sum(self.player_jenner_armor_damage_raw)
        remaining = max(0, PLAYER_JENNER_ARMOR_DAMAGE_DENOMINATOR - damage)
        return (remaining * 100) // PLAYER_JENNER_ARMOR_DAMAGE_DENOMINATOR

    @property
    def player_jenner_armor_preset(self) -> str:
        raw = self.player_jenner_armor_damage_raw
        for label, values in PLAYER_JENNER_ARMOR_PRESETS.items():
            if raw == values:
                return label
        return "custom"

    @player_jenner_armor_preset.setter
    def player_jenner_armor_preset(self, value: str) -> None:
        normalized = value.strip().lower()
        preset = PLAYER_JENNER_ARMOR_ALIASES.get(normalized)
        if preset is None:
            raise GamError("Player Jenner armor must be 77, 85, or full/100")
        self.player_jenner_armor_damage_raw = PLAYER_JENNER_ARMOR_PRESETS[preset]

    @property
    def player_jenner_armor_text(self) -> str:
        preset = self.player_jenner_armor_preset
        raw = ",".join(str(value) for value in self.player_jenner_armor_damage_raw)
        if preset == "custom":
            return f"{self.player_jenner_armor_percent}% custom ({raw})"
        return f"{self.player_jenner_armor_percent}% {preset} ({raw})"

    @property
    def player_jenner_srm4_ammo(self) -> int:
        return self.slot_ammo(0)

    @player_jenner_srm4_ammo.setter
    def player_jenner_srm4_ammo(self, value: int) -> None:
        self.set_slot_ammo(0, value)

    def extra_ammo(self, key: str) -> int:
        try:
            offset = EXTRA_AMMO_OFFSETS_BY_KEY[key]
        except KeyError as exc:
            raise GamError(f"Unknown extra ammo key: {key}") from exc
        return _read_u16le(self.data, offset)

    def set_extra_ammo(self, key: str, value: int) -> None:
        try:
            offset = EXTRA_AMMO_OFFSETS_BY_KEY[key]
        except KeyError as exc:
            raise GamError(f"Unknown extra ammo key: {key}") from exc
        label = next(label for field_key, label, _offset in EXTRA_AMMO_FIELDS if field_key == key)
        _write_u16le(
            self.data,
            offset,
            _validate_int(f"{label} extra ammo", value, 0, EXTRA_AMMO_MAX_IN_HOLD),
        )

    @property
    def extra_ammo_counts(self) -> dict[str, int]:
        return {key: self.extra_ammo(key) for key, _label, _offset in EXTRA_AMMO_FIELDS}

    @property
    def extra_ammo_text(self) -> str:
        return ", ".join(
            f"{label}={self.extra_ammo(key)}" for key, label, _offset in EXTRA_AMMO_FIELDS
        )

    @property
    def sound_enabled(self) -> bool:
        value = self.data[OFF_SOUND_DISABLED]
        if value not in (0, 1):
            raise GamError(f"Sound flag has unsupported raw value {value}")
        return value == 0

    @sound_enabled.setter
    def sound_enabled(self, value: bool) -> None:
        self.data[OFF_SOUND_DISABLED] = 0 if value else 1

    @property
    def sound_text(self) -> str:
        return "on" if self.sound_enabled else "off"

    @property
    def detail_level(self) -> int:
        return _validate_int("Detail level", self.data[OFF_DETAIL_LEVEL], 0, 2)

    @detail_level.setter
    def detail_level(self, value: int) -> None:
        self.data[OFF_DETAIL_LEVEL] = _validate_int("Detail level", value, 0, 2)

    @property
    def detail_text(self) -> str:
        return DETAIL_LEVELS[self.detail_level]

    def known_fields(self) -> dict[str, int | str]:
        return {
            "mech_label": self.mech_label,
            "age": self.age,
            "raw_age_or_start_age": self.raw_age_or_start_age,
            "planet": self.planet_name,
            "money": self.money,
            "family_attitudes": ", ".join(
                f"{name}={value}" for name, value in self.family_attitudes.items()
            ),
            "year": self.year,
            "month": self.month,
            "news_day": self.news_day,
            "month_day_counter": self.month_day_counter,
            "periodic_14_day_counter": self.periodic_14_day_counter,
            "reputation": self.reputation_text,
            "player_jenner_sensors": self.player_jenner_sensors_text,
            "player_jenner_life_support": self.player_jenner_life_support_text,
            "player_jenner_heat_sinks_working": self.player_jenner_heat_sinks_working,
            "player_jenner_ra_actuator": self.player_jenner_ra_actuator_text,
            "player_jenner_mlas_ra_condition": self.player_jenner_mlas_ra_condition_text,
            "player_jenner_armor": self.player_jenner_armor_text,
            "player_jenner_srm4_ammo": self.player_jenner_srm4_ammo,
            "extra_ammo": self.extra_ammo_text,
            "sound": self.sound_text,
            "detail": self.detail_text,
        }

    def save(self, path: Path | str | None = None, backup: bool = True) -> Path:
        path = Path(path) if path is not None else self.path
        if len(self.data) != GAM_SIZE:
            raise GamError(f"Refusing to save: buffer is {len(self.data)} bytes")
        if backup and path.exists():
            backup_path = path.with_suffix(path.suffix + ".bak")
            if not backup_path.exists():
                shutil.copy2(path, backup_path)
        path.write_bytes(self.data)
        self.path = path
        return path


def print_save(save: GamSave) -> None:
    print(f"File: {save.path}")
    print(f"Mech label: {save.mech_label} (read-only; unsafe to rename)")
    print(f"Age: {save.age}")
    print(f"Raw age/start-age byte: {save.raw_age_or_start_age}")
    print(f"Raw reputation: {save.reputation_index} ({save.reputation_text})")
    print(f"Planet: {save.planet_name}")
    print(f"Active mech chassis: {save.active_chassis_name} ({save.active_chassis_save_id})")
    print(f"Owned mech count: {save.mech_count}")
    print("Mech slots:")
    for slot_index in range(MECH_SLOT_COUNT):
        chassis = save.slot_chassis_name(slot_index)
        if chassis == "EMPTY":
            print(f"  {slot_index + 1}: EMPTY")
            continue
        print(
            f"  {slot_index + 1}: {chassis}, armor {save.slot_armor_percent(slot_index)}%, "
            f"{save.slot_ammo_label(slot_index)} {save.slot_ammo(slot_index)}"
        )
    print(f"Money: {save.money}")
    print(
        "Family attitudes: "
        + ", ".join(f"{name}={value}" for name, value in save.family_attitudes.items())
    )
    print(f"Year: {save.year}")
    print(f"Month: {save.month}")
    print(f"News day: {save.news_day}")
    print(f"News date: {save.news_date}")
    print(f"Raw month-day counter: {save.month_day_counter}")
    print(f"Raw 14-day periodic counter: {save.periodic_14_day_counter}")
    print("Active mech components:")
    for key, label, _offset, _confirmed in JENNER_COMPONENT_FIELDS:
        print(f"  {label}: {save.damage_text(save.jenner_component_condition(key))}")
    print(
        "Player mech heat sinks: "
        f"{save.player_jenner_heat_sinks_working} of {save.active_heat_sinks_total()}"
    )
    print(f"Player mech jump jets: {save.active_jump_jets_working} of {save.active_jump_jets_total()}")
    print("Active mech weapons:")
    for key, label, _offset, _confirmed in save.active_weapon_fields():
        print(f"  {label}: {save.damage_text(save.jenner_weapon_condition(key))}")
    print(f"Player mech armor: {save.player_jenner_armor_text}")
    print(f"Player mech {save.active_ammo_label}: {save.player_jenner_srm4_ammo}")
    print(f"Extra ammo in hold: {save.extra_ammo_text}")
    print(f"Sound: {save.sound_text}")
    print(f"Detail: {save.detail_text}")


def _parse_sound(value: str) -> bool:
    normalized = value.strip().lower()
    if normalized in {"on", "1", "true", "yes", "enabled"}:
        return True
    if normalized in {"off", "0", "false", "no", "disabled"}:
        return False
    raise GamError("Sound must be on or off")


def _parse_detail(value: str) -> int:
    normalized = value.strip().lower()
    if normalized not in DETAIL_NAMES:
        raise GamError("Detail must be low, medium/med, or high")
    return DETAIL_NAMES[normalized]


def _parse_damage_state(value: str) -> int:
    normalized = value.strip().lower()
    if normalized not in DAMAGE_NAMES:
        raise GamError("Damage state must be functional, light damage, heavy damage, or junk")
    return DAMAGE_NAMES[normalized]


def _armor_damage_to_quality(value: int) -> str:
    return ARMOR_QUALITY_LABELS_BY_DAMAGE.get(value, f"raw {value}")


def _parse_armor_quality(value: str) -> int:
    normalized = value.strip().lower()
    if normalized in ARMOR_DAMAGE_BY_QUALITY_LABEL:
        return ARMOR_DAMAGE_BY_QUALITY_LABEL[normalized]
    normalized_percent = normalized + "%"
    if normalized_percent in ARMOR_DAMAGE_BY_QUALITY_LABEL:
        return ARMOR_DAMAGE_BY_QUALITY_LABEL[normalized_percent]
    try:
        raw = int(normalized, 10)
    except ValueError as exc:
        raise GamError("Armor quality must be 100%, 66%, 33%, 0%, or raw 0..3") from exc
    return _validate_int("Armor damage byte", raw, 0, 3)


def run_cli(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Edit known MechWarrior 1989 .GAM fields.")
    parser.add_argument("gam", nargs="?", type=Path, help="Path to a .GAM save file")
    parser.add_argument("--show", action="store_true", help="Print known fields and exit")
    parser.add_argument("--set-name", help=argparse.SUPPRESS)
    parser.add_argument("--unsafe-set-mech-label", help=argparse.SUPPRESS)
    parser.add_argument("--set-money", type=int, help="Set C-bills, 0..4294967295")
    parser.add_argument(
        "--set-jenner-armor",
        help="Set observed player Jenner armor preset, e.g. data6-66, data12-85, or full/100",
    )
    parser.add_argument("--set-jenner-srm4", type=int, help="Set player Jenner SRM4 ammo packs, 0..25")
    parser.add_argument("--set-jenner-sensors", help="Set observed player Jenner sensors condition")
    parser.add_argument("--set-jenner-life-support", help="Set observed player Jenner life support condition")
    parser.add_argument("--set-jenner-heat-sinks", type=int, help="Set player Jenner working heat sinks, 0..10")
    parser.add_argument("--set-jenner-ra-actuator", help="Set observed player Jenner RA actuator condition")
    parser.add_argument("--set-jenner-mlas-ra", help="Set observed player Jenner M LAS RA condition")
    parser.add_argument("--set-extra-ac5", type=int, help="Set AC 5-PKS extra ammo in hold")
    parser.add_argument("--set-extra-lrm5", type=int, help="Set LRM 5-PKS extra ammo in hold")
    parser.add_argument("--set-extra-srm2", type=int, help="Set SRM 2-PKS extra ammo in hold")
    parser.add_argument("--set-extra-srm4", type=int, help="Set SRM 4-PKS extra ammo in hold")
    parser.add_argument("--set-extra-srm6", type=int, help="Set SRM 6-PKS extra ammo in hold")
    parser.add_argument("--set-extra-mg", type=int, help="Set MACH GUN extra ammo in hold")
    parser.add_argument("--set-sound", help="Set sound on/off")
    parser.add_argument("--set-detail", help="Set detail low, medium/med, or high")
    parser.add_argument("--set-year", type=int, help="Set campaign year, 0..65535")
    parser.add_argument("--set-month", type=int, help="Set campaign month, 1..12")
    parser.add_argument(
        "--set-day",
        type=int,
        help="Set NEWS/date-condition day, 1..30; stored as (day - 1) * 2",
    )
    parser.add_argument("--set-date", help="Set NEWS/date-condition date as YYYY-MM-DD")
    parser.add_argument(
        "--set-day-counter",
        type=int,
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "--set-periodic-counter",
        type=int,
        help=argparse.SUPPRESS,
    )
    parser.add_argument("--output", type=Path, help="Write changed save to this path")
    parser.add_argument("--in-place", action="store_true", help="Overwrite input save, with .bak")
    parser.add_argument("--no-backup", action="store_true", help="Do not create .bak on overwrite")
    args = parser.parse_args(argv)

    if args.gam is None:
        run_gui()
        return 0

    save = GamSave.load(args.gam)
    changed = False

    if args.set_name is not None:
        raise GamError(
            "--set-name is disabled because renaming this field can break combat; "
            "the current editor keeps mech labels read-only"
        )

    if args.unsafe_set_mech_label is not None:
        raise GamError(
            "--unsafe-set-mech-label is disabled in this safe editor build; "
            "mech labels are read-only until combat handoff behavior is confirmed"
        )

    date_changed = False

    if args.set_date is not None:
        year, month, day = _parse_date(args.set_date)
        save.set_news_date(year, month, day)
        changed = True
        date_changed = True

    if args.set_sound is not None:
        save.sound_enabled = _parse_sound(args.set_sound)
        changed = True

    if args.set_detail is not None:
        save.detail_level = _parse_detail(args.set_detail)
        changed = True

    if args.set_jenner_armor is not None:
        save.player_jenner_armor_preset = args.set_jenner_armor
        changed = True

    if args.set_jenner_srm4 is not None:
        save.player_jenner_srm4_ammo = args.set_jenner_srm4
        changed = True

    if args.set_jenner_sensors is not None:
        save.player_jenner_sensors = _parse_damage_state(args.set_jenner_sensors)
        changed = True

    if args.set_jenner_life_support is not None:
        save.player_jenner_life_support = _parse_damage_state(args.set_jenner_life_support)
        changed = True

    if args.set_jenner_heat_sinks is not None:
        save.player_jenner_heat_sinks_working = args.set_jenner_heat_sinks
        changed = True

    if args.set_jenner_ra_actuator is not None:
        save.player_jenner_ra_actuator = _parse_damage_state(args.set_jenner_ra_actuator)
        changed = True

    if args.set_jenner_mlas_ra is not None:
        save.player_jenner_mlas_ra_condition = _parse_damage_state(args.set_jenner_mlas_ra)
        changed = True

    for key, _label, _offset in EXTRA_AMMO_FIELDS:
        value = getattr(args, f"set_extra_{key}")
        if value is not None:
            save.set_extra_ammo(key, value)
            changed = True

    for arg_name, attr in [
        ("set_money", "money"),
        ("set_year", "year"),
        ("set_month", "month"),
        ("set_day", "news_day"),
    ]:
        value = getattr(args, arg_name)
        if value is not None:
            setattr(save, attr, value)
            changed = True
            if arg_name in {"set_year", "set_month", "set_day"}:
                date_changed = True

    for raw_arg_name, raw_label in [
        ("set_day_counter", "--set-day-counter"),
        ("set_periodic_counter", "--set-periodic-counter"),
    ]:
        if getattr(args, raw_arg_name) is not None:
            raise GamError(
                f"{raw_label} is disabled in this safe editor build; edit the friendly "
                "date fields and let the editor derive raw counters"
            )

    if date_changed and args.set_periodic_counter is None:
        save.sync_periodic_counter_from_date()

    if args.show or not changed:
        print_save(save)

    if changed:
        if args.output is None and not args.in_place:
            raise GamError("Use --output or --in-place when changing a save")
        out_path = args.output if args.output is not None else save.path
        save.save(out_path, backup=(not args.no_backup and out_path == save.path))
        print(f"Saved: {out_path}")

    return 0


def run_gui() -> None:
    import tkinter as tk
    from tkinter import filedialog, messagebox
    from tkinter import ttk

    root = tk.Tk()
    root.title("MechWarrior .GAM Editor")
    root.minsize(1120, 760)

    current: dict[str, GamSave | None] = {"save": None}

    path_var = tk.StringVar(value="")
    name_var = tk.StringVar()
    age_var = tk.StringVar()
    reputation_var = tk.StringVar()
    planet_var = tk.StringVar()
    money_var = tk.StringVar()
    family_vars = [tk.StringVar() for _name in FAMILY_ATTITUDE_NAMES]
    extra_ammo_vars = {
        key: tk.StringVar() for key, _label, _offset in EXTRA_AMMO_FIELDS
    }
    sound_var = tk.StringVar()
    detail_var = tk.StringVar()
    year_var = tk.StringVar()
    month_var = tk.StringVar()
    day_var = tk.StringVar()
    month_day_counter_var = tk.StringVar()
    periodic_counter_var = tk.StringVar()
    mech_index_var = tk.StringVar(value="1")
    chassis_var = tk.StringVar(value="")
    ammo_label_var = tk.StringVar(value="Ammo")
    heat_sinks_var = tk.StringVar()
    jump_jets_var = tk.StringVar()
    srm4_ammo_var = tk.StringVar()
    component_vars = {
        key: tk.StringVar() for key, _label, _offset, _confirmed in JENNER_COMPONENT_FIELDS
    }
    weapon_vars = {f"weapon_{index}": tk.StringVar() for index in range(MAX_WEAPON_SLOTS)}
    armor_vars = {
        key: tk.StringVar() for key, _label, _offset in PLAYER_JENNER_ARMOR_FIELDS
    }
    armor_var = tk.StringVar()
    status_var = tk.StringVar(value="Open a .GAM file to begin.")

    damage_values = tuple(DAMAGE_STATES[value] for value in sorted(DAMAGE_STATES))
    editable_widgets: list[tk.Widget] = []
    readonly_widgets: list[tk.Widget] = []
    heat_sinks_spinbox: tk.Spinbox | None = None
    jump_jets_spinbox: tk.Spinbox | None = None
    ammo_spinbox: tk.Spinbox | None = None
    weapon_label_widgets: dict[str, tk.Label] = {}
    weapon_combo_widgets: dict[str, ttk.Combobox] = {}

    def add_entry(
        parent: tk.Widget,
        row: int,
        label: str,
        var: tk.StringVar,
        editable: bool = True,
        width: int = 18,
    ) -> tk.Entry:
        tk.Label(parent, text=label, anchor="w").grid(row=row, column=0, sticky="w", padx=(0, 8), pady=3)
        entry = tk.Entry(parent, textvariable=var, width=width)
        entry.grid(row=row, column=1, sticky="ew", pady=3)
        if editable:
            editable_widgets.append(entry)
        else:
            entry.configure(state="readonly")
            readonly_widgets.append(entry)
        return entry

    def add_combo(
        parent: tk.Widget,
        row: int,
        label: str,
        var: tk.StringVar,
        values: tuple[str, ...],
        confirmed: bool = True,
    ) -> ttk.Combobox:
        suffix = "" if confirmed else " (test)"
        tk.Label(parent, text=label + suffix, anchor="w").grid(row=row, column=0, sticky="w", padx=(0, 8), pady=3)
        combo = ttk.Combobox(parent, textvariable=var, values=values, state="readonly", width=20)
        combo.grid(row=row, column=1, sticky="ew", pady=3)
        editable_widgets.append(combo)
        return combo

    def add_spinbox(
        parent: tk.Widget,
        row: int,
        label: str,
        var: tk.StringVar,
        min_value: int,
        max_value: int,
    ) -> tk.Spinbox:
        tk.Label(parent, text=label, anchor="w").grid(row=row, column=0, sticky="w", padx=(0, 8), pady=3)
        spinbox = tk.Spinbox(
            parent,
            textvariable=var,
            from_=min_value,
            to=max_value,
            increment=1,
            width=20,
        )
        spinbox.grid(row=row, column=1, sticky="ew", pady=3)
        editable_widgets.append(spinbox)
        return spinbox

    def set_form_enabled(enabled: bool) -> None:
        for widget in editable_widgets:
            if isinstance(widget, ttk.Combobox):
                widget.configure(state="readonly" if enabled else "disabled")
            else:
                widget.configure(state="normal" if enabled else "disabled")
        button_state = "normal" if enabled else "disabled"
        save_button.configure(state=button_state)
        export_as_button.configure(state=button_state)

    def selected_slot_index() -> int:
        return _validate_int("Mech slot", int(mech_index_var.get(), 10), 1, MECH_SLOT_COUNT) - 1

    def update_selected_chassis_controls(enabled: bool = True) -> None:
        chassis_name = _normalize_chassis_name(chassis_var.get())
        active_keys = set()
        active_fields = GamSave.weapon_fields_for_chassis(chassis_name)
        for key, label, _offset, _confirmed in active_fields:
            active_keys.add(key)
            if key in weapon_label_widgets:
                weapon_label_widgets[key].configure(text=label)
            if key in weapon_combo_widgets:
                weapon_combo_widgets[key].configure(state="readonly" if enabled else "disabled")
        ammo_label_var.set(GamSave.ammo_label_for_chassis(chassis_name))
        if ammo_spinbox is not None:
            ammo_spinbox.configure(to=GamSave.ammo_max_for_chassis(chassis_name))
        if heat_sinks_spinbox is not None:
            heat_sinks_spinbox.configure(to=GamSave.heat_sinks_total_for_chassis(chassis_name))
        if jump_jets_spinbox is not None:
            jump_jets_spinbox.configure(to=GamSave.jump_jets_total_for_chassis(chassis_name))
        for key, combo in weapon_combo_widgets.items():
            if key not in active_keys:
                if key in weapon_label_widgets:
                    weapon_label_widgets[key].configure(text="Unused")
                combo.configure(state="disabled")

    def load_mech_slot_into_form(save: GamSave, slot_index: int) -> None:
        chassis_var.set(_chassis_display_name(save.slot_chassis_name(slot_index)))
        ammo_label_var.set(save.slot_ammo_label(slot_index))
        if heat_sinks_spinbox is not None:
            heat_sinks_spinbox.configure(to=save.slot_heat_sinks_total(slot_index))
        if jump_jets_spinbox is not None:
            jump_jets_spinbox.configure(to=save.slot_jump_jets_total(slot_index))
        if ammo_spinbox is not None:
            ammo_spinbox.configure(to=save.slot_ammo_max(slot_index))
        heat_sinks_var.set(str(save.slot_heat_sinks_working(slot_index)))
        jump_jets_var.set(str(save.slot_jump_jets_working(slot_index)))
        srm4_ammo_var.set(str(save.slot_ammo(slot_index)))
        for key, _label, _relative_offset, _confirmed in JENNER_COMPONENT_FIELDS:
            component_vars[key].set(save.damage_text(save.slot_component_condition(slot_index, key)))
        for weapon_index in range(MAX_WEAPON_SLOTS):
            weapon_vars[f"weapon_{weapon_index}"].set(
                save.damage_text(save.slot_weapon_condition(slot_index, weapon_index))
            )
        for key, _label, _offset in PLAYER_JENNER_ARMOR_FIELDS:
            armor_vars[key].set(_armor_damage_to_quality(save.slot_armor_damage(slot_index, key)))
        armor_var.set(save.slot_armor_text(slot_index))
        update_selected_chassis_controls(True)

    def load_into_form(save: GamSave) -> None:
        path_var.set(str(save.path))
        name_var.set(save.mech_label)
        age_var.set(str(save.age))
        reputation_var.set(f"{save.reputation_index} ({save.reputation_text})")
        planet_var.set(save.planet_name)
        money_var.set(str(save.money))
        for index, var in enumerate(family_vars):
            var.set(str(save.family_attitude(index)))
        for key, _label, _offset in EXTRA_AMMO_FIELDS:
            extra_ammo_vars[key].set(str(save.extra_ammo(key)))
        sound_var.set(save.sound_text)
        detail_var.set(save.detail_text)
        year_var.set(str(save.year))
        month_var.set(str(save.month))
        day_var.set(str(save.news_day))
        month_day_counter_var.set(str(save.month_day_counter))
        periodic_counter_var.set(str(save.periodic_14_day_counter))
        mech_index_var.set("1")
        load_mech_slot_into_form(save, 0)
        status_var.set(
            f"Loaded. Mechs: {save.mech_count}. Select slot 1-12 to edit chassis, components, weapons, ammo, and armor."
        )
        set_form_enabled(True)

    def choose_open() -> None:
        initialdir = DEFAULT_GAM_DIR if DEFAULT_GAM_DIR.exists() else Path.cwd()
        filename = filedialog.askopenfilename(
            title="Open .GAM save",
            initialdir=str(initialdir),
            filetypes=[("MechWarrior saves", "*.GAM"), ("All files", "*.*")],
        )
        if not filename:
            return
        try:
            save = GamSave.load(filename)
        except Exception as exc:
            messagebox.showerror("Could not open save", str(exc))
            return
        current["save"] = save
        load_into_form(save)

    def choose_mech_slot(_event: object | None = None) -> None:
        save = current["save"]
        if save is None:
            return
        try:
            load_mech_slot_into_form(save, selected_slot_index())
            status_var.set(f"Editing mech slot {selected_slot_index() + 1} of 12. Mechs: {save.mech_count}.")
        except Exception as exc:
            messagebox.showerror("Could not switch mech slot", str(exc))

    def parse_int(label: str, var: tk.StringVar) -> int:
        value = var.get().strip()
        if not value:
            raise GamError(f"{label} is required")
        try:
            return int(value, 10)
        except ValueError as exc:
            raise GamError(f"{label} must be a decimal number") from exc

    def choose_chassis(_event: object | None = None) -> None:
        chassis_name = _normalize_chassis_name(chassis_var.get())
        update_selected_chassis_controls(True)
        heat_sinks_var.set(str(GamSave.heat_sinks_total_for_chassis(chassis_name)))
        jump_jets_var.set(str(GamSave.jump_jets_total_for_chassis(chassis_name)))

    def apply_form(save: GamSave) -> None:
        money = parse_int("Money", money_var)
        year = parse_int("Year", year_var)
        month = parse_int("Month", month_var)
        news_day = parse_int("News day", day_var)
        date_changed = (
            year != save.year
            or month != save.month
            or news_day != save.news_day
        )
        slot_index = selected_slot_index()
        selected_chassis = _normalize_chassis_name(chassis_var.get())

        save.money = money
        for index, var in enumerate(family_vars):
            save.set_family_attitude(index, parse_int(FAMILY_ATTITUDE_NAMES[index], var))
        for key, label, _offset in EXTRA_AMMO_FIELDS:
            save.set_extra_ammo(key, parse_int(label, extra_ammo_vars[key]))
        save.sound_enabled = _parse_sound(sound_var.get())
        save.detail_level = _parse_detail(detail_var.get())
        save.set_slot_chassis_name(slot_index, selected_chassis)
        if selected_chassis != "EMPTY":
            for key, _label, _offset, _confirmed in JENNER_COMPONENT_FIELDS:
                save.set_slot_component_condition(slot_index, key, _parse_damage_state(component_vars[key].get()))
            save.set_slot_heat_sinks_working(slot_index, parse_int("Heat sinks", heat_sinks_var))
            save.set_slot_jump_jets_working(slot_index, parse_int("Jump jets", jump_jets_var))
            for key, _label, weapon_index, _confirmed in save.slot_weapon_fields(slot_index):
                save.set_slot_weapon_condition(
                    slot_index,
                    weapon_index,
                    _parse_damage_state(weapon_vars[key].get()),
                )
            save.clear_unused_slot_weapon_conditions(slot_index)
            save.set_slot_ammo(slot_index, parse_int(save.slot_ammo_label(slot_index), srm4_ammo_var))
            for key, label, _offset in PLAYER_JENNER_ARMOR_FIELDS:
                save.set_slot_armor_damage(slot_index, key, _parse_armor_quality(armor_vars[key].get()))
        armor_var.set(save.slot_armor_text(slot_index))
        if date_changed:
            save.year = year
            save.month = month
            save.news_day = news_day
            save.sync_periodic_counter_from_date()
            month_day_counter_var.set(str(save.month_day_counter))
            periodic_counter_var.set(str(save.periodic_14_day_counter))

    def save_current(path: Path | None = None, export_copy: bool = False) -> None:
        save = current["save"]
        if save is None:
            return
        try:
            apply_form(save)
            written = save.save(path, backup=True)
        except Exception as exc:
            messagebox.showerror("Could not save", str(exc))
            return
        path_var.set(str(written))
        if export_copy:
            status_var.set(
                f"Exported: {written.name}. Original DOS restore lists only first 12 *.GAM."
            )
        else:
            status_var.set(f"Saved: {written.name} (.bak created if needed)")

    def choose_save_as() -> None:
        save = current["save"]
        if save is None:
            return
        filename = filedialog.asksaveasfilename(
            title="Export .GAM copy as",
            initialdir=str(save.path.parent),
            initialfile=save.path.name,
            defaultextension=".GAM",
            filetypes=[("MechWarrior saves", "*.GAM"), ("All files", "*.*")],
        )
        if filename:
            save_current(Path(filename), export_copy=True)

    wrapper = tk.Frame(root, padx=14, pady=14)
    wrapper.pack(fill="both", expand=True)
    wrapper.columnconfigure(0, weight=1)
    wrapper.columnconfigure(1, weight=1)
    wrapper.columnconfigure(2, weight=1)
    wrapper.rowconfigure(1, weight=1)

    path_row = tk.Frame(wrapper)
    path_row.grid(row=0, column=0, columnspan=3, sticky="ew", pady=(0, 12))
    path_row.columnconfigure(1, weight=1)
    tk.Label(path_row, text="File", width=10, anchor="w").pack(side="left")
    tk.Entry(path_row, textvariable=path_var, state="readonly").pack(side="left", fill="x", expand=True)
    tk.Button(path_row, text="Open", command=choose_open).pack(side="left", padx=(8, 0))

    left = tk.Frame(wrapper)
    left.grid(row=1, column=0, sticky="nsew", padx=(0, 10))
    middle = tk.Frame(wrapper)
    middle.grid(row=1, column=1, sticky="nsew", padx=10)
    armor_column = tk.Frame(wrapper)
    armor_column.grid(row=1, column=2, sticky="nsew", padx=(10, 0))

    campaign_frame = ttk.LabelFrame(left, text="Campaign")
    campaign_frame.pack(fill="x", pady=(0, 10))
    campaign_frame.columnconfigure(1, weight=1)
    add_entry(campaign_frame, 0, "Mech label", name_var, editable=False)
    add_entry(campaign_frame, 1, "Age", age_var, editable=False)
    add_entry(campaign_frame, 2, "Reputation", reputation_var, editable=False)
    add_entry(campaign_frame, 3, "Planet", planet_var, editable=False)
    add_entry(campaign_frame, 4, "Money", money_var)
    add_entry(campaign_frame, 5, "Year", year_var)
    add_entry(campaign_frame, 6, "Month", month_var)
    add_entry(campaign_frame, 7, "News day", day_var)
    add_entry(campaign_frame, 8, "Day counter", month_day_counter_var, editable=False)
    add_entry(campaign_frame, 9, "14-day ctr", periodic_counter_var, editable=False)

    family_frame = ttk.LabelFrame(left, text="Family Attitudes")
    family_frame.pack(fill="x", pady=(0, 10))
    family_frame.columnconfigure(1, weight=1)
    for index, name in enumerate(FAMILY_ATTITUDE_NAMES):
        add_entry(family_frame, index, name, family_vars[index])

    ammo_frame = ttk.LabelFrame(left, text="Extra Ammo In Hold")
    ammo_frame.pack(fill="x", pady=(0, 10))
    ammo_frame.columnconfigure(1, weight=1)
    for row, (key, label, _offset) in enumerate(EXTRA_AMMO_FIELDS):
        add_entry(ammo_frame, row, label, extra_ammo_vars[key])

    settings_frame = ttk.LabelFrame(left, text="Game Settings")
    settings_frame.pack(fill="x")
    settings_frame.columnconfigure(1, weight=1)
    add_combo(settings_frame, 0, "Sound", sound_var, ("on", "off"))
    add_combo(settings_frame, 1, "Detail", detail_var, ("low", "medium", "high"))

    mech_frame = ttk.LabelFrame(middle, text="Mech 1")
    mech_frame.pack(fill="x", pady=(0, 10))
    mech_frame.columnconfigure(1, weight=1)
    slot_combo = add_combo(
        mech_frame,
        0,
        "Slot",
        mech_index_var,
        tuple(str(index) for index in range(1, MECH_SLOT_COUNT + 1)),
    )
    slot_combo.bind("<<ComboboxSelected>>", choose_mech_slot)
    chassis_combo = add_combo(mech_frame, 1, "Chassis", chassis_var, CHASSIS_CHOICES)
    chassis_combo.bind("<<ComboboxSelected>>", choose_chassis)

    components_frame = ttk.LabelFrame(middle, text="Components")
    components_frame.pack(fill="x", pady=(0, 10))
    components_frame.columnconfigure(1, weight=1)
    component_row = 0
    for key, label, _offset, confirmed in JENNER_COMPONENT_FIELDS:
        add_combo(components_frame, component_row, label, component_vars[key], damage_values, confirmed)
        component_row += 1
    heat_sinks_spinbox = add_spinbox(
        components_frame,
        component_row,
        "HEAT SINK",
        heat_sinks_var,
        0,
        PLAYER_JENNER_HEAT_SINKS_TOTAL,
    )
    component_row += 1
    jump_jets_spinbox = add_spinbox(
        components_frame,
        component_row,
        "JUMP JETS",
        jump_jets_var,
        0,
        PLAYER_JENNER_JUMP_JETS_TOTAL,
    )

    weapons_frame = ttk.LabelFrame(middle, text="Weapons")
    weapons_frame.pack(fill="x", pady=(0, 10))
    weapons_frame.columnconfigure(1, weight=1)
    tk.Label(weapons_frame, textvariable=ammo_label_var, anchor="w").grid(
        row=0, column=0, sticky="w", padx=(0, 8), pady=3
    )
    ammo_spinbox = tk.Spinbox(
        weapons_frame,
        textvariable=srm4_ammo_var,
        from_=0,
        to=255,
        increment=1,
        width=20,
    )
    ammo_spinbox.grid(row=0, column=1, sticky="ew", pady=3)
    editable_widgets.append(ammo_spinbox)
    for row in range(1, MAX_WEAPON_SLOTS + 1):
        key = f"weapon_{row - 1}"
        label_widget = tk.Label(weapons_frame, text="Unused", anchor="w")
        label_widget.grid(row=row, column=0, sticky="w", padx=(0, 8), pady=3)
        combo = ttk.Combobox(
            weapons_frame,
            textvariable=weapon_vars[key],
            values=damage_values,
            state="readonly",
            width=20,
        )
        combo.grid(row=row, column=1, sticky="ew", pady=3)
        editable_widgets.append(combo)
        weapon_label_widgets[key] = label_widget
        weapon_combo_widgets[key] = combo

    armor_frame = ttk.LabelFrame(armor_column, text="Armor Damage")
    armor_frame.pack(fill="both", expand=True)
    for column in range(5):
        armor_frame.columnconfigure(column, weight=1)
    tk.Label(armor_frame, textvariable=armor_var, anchor="center").grid(
        row=0, column=0, columnspan=5, sticky="ew", pady=(0, 10)
    )

    def place_armor(label: str, key: str, row: int, column: int) -> None:
        cell = tk.Frame(armor_frame)
        cell.grid(row=row, column=column, sticky="n", padx=5, pady=5)
        tk.Label(cell, text=label).pack()
        combo = ttk.Combobox(
            cell,
            textvariable=armor_vars[key],
            values=ARMOR_QUALITY_VALUES,
            state="readonly",
            width=6,
        )
        combo.pack()
        editable_widgets.append(combo)

    place_armor("HEAD", "head", 1, 2)
    place_armor("TL", "tl", 2, 1)
    place_armor("CT", "ct", 2, 2)
    place_armor("TR", "tr", 2, 3)
    place_armor("LA", "la", 3, 0)
    place_armor("RA", "ra", 3, 4)
    place_armor("LL", "ll", 4, 1)
    place_armor("RL", "rl", 4, 3)
    place_armor("BACK", "back", 5, 2)

    button_row = tk.Frame(wrapper)
    button_row.grid(row=2, column=0, columnspan=3, sticky="ew", pady=(14, 8))
    save_button = tk.Button(button_row, text="Save", command=lambda: save_current())
    save_button.pack(side="left")
    export_as_button = tk.Button(button_row, text="Export As", command=choose_save_as)
    export_as_button.pack(side="left", padx=(8, 0))

    tk.Label(wrapper, textvariable=status_var, anchor="w").grid(
        row=3, column=0, columnspan=3, sticky="ew", pady=(4, 0)
    )

    set_form_enabled(False)
    root.mainloop()


def main() -> int:
    try:
        return run_cli(sys.argv[1:])
    except GamError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
