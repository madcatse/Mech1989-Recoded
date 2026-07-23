#!/usr/bin/env python3
"""Editor for known MechWarrior 1989 .GAM save fields.

This intentionally edits only low-risk fields confirmed by save/screenshot
comparison. Mech labels are shown but not edited by default because changing
them can break the external combat module handoff.

The original game does not store a normal calendar day. It stores a day counter
within the displayed month at save offset 0x0031. NEWS NET date records compare
against `(published_day - 1) * 2`, so this editor exposes a friendly "news day"
for testing publication thresholds and also keeps the raw counter available.
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

START_YEAR = 3024
START_MONTH_ONE_BASED = 4
START_MONTH_DAY_COUNTER = 1

DEFAULT_GAM_DIR = Path(__file__).resolve().parents[1] / "Sorted Original Files" / "GAM"
KNOWN_PLANETS = {
    0x00: "LUTHIEN",
    0x0D: "BENJAMIN",
    0x76: "KESAI IV",
}


class GamError(ValueError):
    pass


def _read_u16le(data: bytearray, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little", signed=False)


def _write_u16le(data: bytearray, offset: int, value: int) -> None:
    data[offset : offset + 2] = value.to_bytes(2, "little", signed=False)


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

    def known_fields(self) -> dict[str, int | str]:
        return {
            "mech_label": self.mech_label,
            "age": self.age,
            "raw_age_or_start_age": self.raw_age_or_start_age,
            "planet": self.planet_name,
            "money": self.money,
            "year": self.year,
            "month": self.month,
            "news_day": self.news_day,
            "month_day_counter": self.month_day_counter,
            "periodic_14_day_counter": self.periodic_14_day_counter,
            "reputation": self.reputation_text,
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
    print(f"Planet: {save.planet_name} ({save.planet_index})")
    print(f"Money: {save.money}")
    print(f"Year: {save.year}")
    print(f"Month: {save.month}")
    print(f"News day: {save.news_day}")
    print(f"News date: {save.news_date}")
    print(f"Raw month-day counter: {save.month_day_counter}")
    print(f"Raw 14-day periodic counter: {save.periodic_14_day_counter}")


def run_cli(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Edit known MechWarrior 1989 .GAM fields.")
    parser.add_argument("gam", nargs="?", type=Path, help="Path to a .GAM save file")
    parser.add_argument("--show", action="store_true", help="Print known fields and exit")
    parser.add_argument("--set-name", help=argparse.SUPPRESS)
    parser.add_argument("--unsafe-set-mech-label", help="Risky: set mech label, ASCII, up to 10 chars")
    parser.add_argument("--set-money", type=int, help="Set C-bills, 0..4294967295")
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
        help="Set raw month-day counter, 0..59, for exact save experiments",
    )
    parser.add_argument(
        "--set-periodic-counter",
        type=int,
        help="Set raw 14-day campaign update counter, 0..13",
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
            "use --unsafe-set-mech-label only for experiments"
        )

    if args.unsafe_set_mech_label is not None:
        save.mech_label = args.unsafe_set_mech_label
        changed = True

    date_changed = False

    if args.set_date is not None:
        year, month, day = _parse_date(args.set_date)
        save.set_news_date(year, month, day)
        changed = True
        date_changed = True

    for arg_name, attr in [
        ("set_money", "money"),
        ("set_year", "year"),
        ("set_month", "month"),
        ("set_day", "news_day"),
        ("set_day_counter", "month_day_counter"),
        ("set_periodic_counter", "periodic_14_day_counter"),
    ]:
        value = getattr(args, arg_name)
        if value is not None:
            setattr(save, attr, value)
            changed = True
            if arg_name in {"set_year", "set_month", "set_day", "set_day_counter"}:
                date_changed = True

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

    root = tk.Tk()
    root.title("MechWarrior .GAM Editor")
    root.minsize(440, 280)

    current: dict[str, GamSave | None] = {"save": None}

    path_var = tk.StringVar(value="")
    name_var = tk.StringVar()
    age_var = tk.StringVar()
    money_var = tk.StringVar()
    year_var = tk.StringVar()
    month_var = tk.StringVar()
    day_var = tk.StringVar()
    month_day_counter_var = tk.StringVar()
    periodic_counter_var = tk.StringVar()
    status_var = tk.StringVar(value="Open a .GAM file to begin.")

    fields = [
        ("Mech label", name_var, False),
        ("Age", age_var, False),
        ("Money", money_var, True),
        ("Year", year_var, True),
        ("Month", month_var, True),
        ("News day", day_var, True),
        ("Day counter", month_day_counter_var, False),
        ("14-day ctr", periodic_counter_var, False),
    ]

    def set_form_enabled(enabled: bool) -> None:
        for entry, editable in entries:
            state = "normal" if enabled and editable else "disabled"
            entry.configure(state=state)
        save_button.configure(state=state)
        save_as_button.configure(state=state)

    def load_into_form(save: GamSave) -> None:
        path_var.set(str(save.path))
        name_var.set(save.mech_label)
        age_var.set(str(save.age))
        money_var.set(str(save.money))
        year_var.set(str(save.year))
        month_var.set(str(save.month))
        day_var.set(str(save.news_day))
        month_day_counter_var.set(str(save.month_day_counter))
        periodic_counter_var.set(str(save.periodic_14_day_counter))
        status_var.set(
            "Loaded. Money/date are editable; mech label is read-only for combat safety."
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

    def apply_form(save: GamSave) -> None:
        save.money = int(money_var.get(), 10)
        save.year = int(year_var.get(), 10)
        save.month = int(month_var.get(), 10)
        save.news_day = int(day_var.get(), 10)
        save.sync_periodic_counter_from_date()
        month_day_counter_var.set(str(save.month_day_counter))
        periodic_counter_var.set(str(save.periodic_14_day_counter))

    def save_current(path: Path | None = None) -> None:
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
        status_var.set(f"Saved: {written.name} (.bak created if needed)")

    def choose_save_as() -> None:
        save = current["save"]
        if save is None:
            return
        filename = filedialog.asksaveasfilename(
            title="Save .GAM as",
            initialdir=str(save.path.parent),
            initialfile=save.path.name,
            defaultextension=".GAM",
            filetypes=[("MechWarrior saves", "*.GAM"), ("All files", "*.*")],
        )
        if filename:
            save_current(Path(filename))

    wrapper = tk.Frame(root, padx=14, pady=14)
    wrapper.pack(fill="both", expand=True)

    path_row = tk.Frame(wrapper)
    path_row.pack(fill="x", pady=(0, 12))
    tk.Label(path_row, text="File", width=10, anchor="w").pack(side="left")
    tk.Entry(path_row, textvariable=path_var, state="readonly").pack(side="left", fill="x", expand=True)
    tk.Button(path_row, text="Open", command=choose_open).pack(side="left", padx=(8, 0))

    entries: list[tuple[tk.Entry, bool]] = []
    for label, var, editable in fields:
        row = tk.Frame(wrapper)
        row.pack(fill="x", pady=4)
        tk.Label(row, text=label, width=10, anchor="w").pack(side="left")
        entry = tk.Entry(row, textvariable=var)
        entry.pack(side="left", fill="x", expand=True)
        entries.append((entry, editable))

    button_row = tk.Frame(wrapper)
    button_row.pack(fill="x", pady=(14, 8))
    save_button = tk.Button(button_row, text="Save", command=lambda: save_current())
    save_button.pack(side="left")
    save_as_button = tk.Button(button_row, text="Save As", command=choose_save_as)
    save_as_button.pack(side="left", padx=(8, 0))

    tk.Label(wrapper, textvariable=status_var, anchor="w").pack(fill="x", pady=(4, 0))

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
