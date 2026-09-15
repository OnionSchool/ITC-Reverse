from __future__ import annotations

import argparse
import csv
import json
import shutil
import subprocess
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Callable


class ImportError(RuntimeError):
    pass


@dataclass
class Terminal:
    id: int
    name: str
    address: str
    play_volume: int | None
    forward_address: str | None
    terminal_type: int | None
    number: str | None
    control_center: str | None
    terminal_version: int | None
    default_call_priority: int | None
    default_logon_id: str | None


@dataclass
class Group:
    id: int
    name: str
    number: str | None
    terminal_ids: list[int] = field(default_factory=list)


@dataclass
class LegacyConfiguration:
    source: str
    terminals: list[Terminal]
    groups: list[Group]


Runner = Callable[[list[str]], str]


def import_legacy_configuration(database: Path, mdb_export: str = "mdb-export", runner: Runner | None = None) -> LegacyConfiguration:
    database = database.resolve()
    if not database.is_file():
        raise ImportError(f"找不到旧数据库：{database}")
    run = runner or _run_export
    export = _exporter(mdb_export)
    terminals = [_terminal(row) for row in _read_csv(run([export, "-H", str(database), "DB_Term"]))]
    group_members = _read_csv(run([export, "-H", str(database), "DB_GroupMember"]))
    members_by_group: dict[int, list[int]] = {}
    for member in group_members:
        members_by_group.setdefault(_integer(member, "GroupId"), []).append(_integer(member, "TermId"))
    groups = [
        Group(
            id=_integer(row, "GroupId"),
            name=row["Name"],
            number=_optional_text(row.get("Number")),
            terminal_ids=sorted(members_by_group.get(_integer(row, "GroupId"), [])),
        )
        for row in _read_csv(run([export, "-H", str(database), "DB_Group"]))
    ]
    return LegacyConfiguration(str(database), sorted(terminals, key=lambda item: item.id), sorted(groups, key=lambda item: item.id))


def write_configuration(configuration: LegacyConfiguration, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(asdict(configuration), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _exporter(value: str) -> str:
    resolved = shutil.which(value) if "/" not in value else value
    if not resolved:
        raise ImportError("未找到 mdb-export；请通过 Nix 使用 nix shell nixpkgs#mdbtools --command ...")
    return resolved


def _run_export(command: list[str]) -> str:
    result = subprocess.run(command, check=False, capture_output=True, text=True)
    if result.returncode:
        raise ImportError(f"无法读取 Access 表：{result.stderr.strip()}")
    return result.stdout


def _read_csv(content: str) -> list[dict[str, str]]:
    return list(csv.DictReader(content.splitlines())) if content.strip() else []


def _terminal(row: dict[str, str]) -> Terminal:
    return Terminal(
        id=_integer(row, "ID"),
        name=row["Name"],
        address=row["Address"],
        play_volume=_optional_integer(row.get("PlayVol")),
        forward_address=_optional_text(row.get("FwdAddr")),
        terminal_type=_optional_integer(row.get("TermType")),
        number=_optional_text(row.get("Number")),
        control_center=_optional_text(row.get("CCenter")),
        terminal_version=_optional_integer(row.get("TermVer")),
        default_call_priority=_optional_integer(row.get("DefCallPri")),
        default_logon_id=_optional_text(row.get("DefLogonID")),
    )


def _integer(row: dict[str, str], field: str) -> int:
    try:
        return int(row[field])
    except (KeyError, TypeError, ValueError) as error:
        raise ImportError(f"旧数据库字段 {field} 无效") from error


def _optional_integer(value: str | None) -> int | None:
    return int(value) if value else None


def _optional_text(value: str | None) -> str | None:
    return value or None


def main() -> None:
    parser = argparse.ArgumentParser(description="只读导入 ITC 旧终端和分组配置")
    parser.add_argument("database", type=Path, help="旧系统 DB_Server.mdb 的路径")
    parser.add_argument("output", type=Path, help="导出的 JSON 配置路径")
    parser.add_argument("--mdb-export", default="mdb-export", help="mdbtools 的 mdb-export 路径")
    args = parser.parse_args()
    try:
        write_configuration(import_legacy_configuration(args.database, args.mdb_export), args.output)
    except ImportError as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
