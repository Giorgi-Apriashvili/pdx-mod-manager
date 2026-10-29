#!/usr/bin/env python3
"""Observable probe mods for verifying effective mod loading and order in-game.

A process starting is not evidence that mods loaded. These probe mods make the
game itself report what it loaded: each probe ships scripted triggers that use
deliberately unknown trigger names, which Clausewitz games report in
logs/error.log together with the file they came from.

  common/scripted_triggers/pmm_probe_only_<X>.txt, events/pmm_probe_only_<X>.txt
      unique to probe X -> its error proves probe X was loaded
  common/scripted_triggers/pmm_probe_shared.txt, events/pmm_probe_shared.txt
      same relative path in every probe -> only the winning copy is parsed, so
      the trigger name in the error reveals which mod won the file override

EU4 1.37 does not report unknown triggers inside scripted triggers that are
never used (observed 2026-10-30), but it parses every event file at start-up,
so the event files carry the same trigger names.

Usage:
  probe_mods.py create <mod-dir> [--names a b c] [--supported-version v4.5.*]
      Writes probe folders and .mod descriptors into the game's user mod
      directory (e.g. "Documents/Paradox Interactive/Stellaris/mod").
  probe_mods.py analyze <error.log> --enabled c a [--disabled b]
      Reads the game's error.log after it reached the main menu and reports
      which probes loaded and which one won the shared file, as JSON.
  probe_mods.py remove <mod-dir> [--names a b c]
      Deletes the probe folders and descriptors created by `create`.

The probes contain no gameplay content and are safe to leave disabled.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
from pathlib import Path

DEFAULT_NAMES = ["a", "b", "c"]
NAME_RE = re.compile(r"^[a-z0-9]{1,16}$")


def probe_folder(name: str) -> str:
    return f"pmm_probe_{name}"


def descriptor(name: str, path: Path | None, supported_version: str | None) -> str:
    lines = [
        'version="1.0"',
        f'name="PMM Probe {name.upper()}"',
        'tags={\n\t"Utilities"\n}',
    ]
    if supported_version:
        lines.append(f'supported_version="{supported_version}"')
    if path is not None:
        lines.append(f'path="{path.as_posix()}"')
    return "\n".join(lines) + "\n"


def probe_event(namespace: str, trigger: str) -> str:
    # Never fires (triggered only, unknown trigger); the game reports the
    # unknown trigger with the file it came from while loading events.
    return (
        f"namespace = {namespace}\n\n"
        "country_event = {\n"
        f"\tid = {namespace}.1\n"
        f"\ttitle = {namespace}.1.t\n"
        f"\tdesc = {namespace}.1.d\n"
        "\tis_triggered_only = yes\n"
        f"\ttrigger = {{\n\t\t{trigger} = yes\n\t}}\n"
        f"\toption = {{\n\t\tname = {namespace}.1.a\n\t}}\n"
        "}\n"
    )


def create(mod_dir: Path, names: list[str], supported_version: str | None) -> dict:
    if not mod_dir.is_dir():
        raise SystemExit(f"mod directory does not exist: {mod_dir}")
    created = []
    for name in names:
        folder = mod_dir / probe_folder(name)
        triggers = folder / "common" / "scripted_triggers"
        triggers.mkdir(parents=True, exist_ok=True)
        (triggers / f"pmm_probe_only_{name}.txt").write_text(
            f"pmm_probe_only_{name}_trigger = {{\n\tpmm_probe_loaded_{name} = yes\n}}\n",
            encoding="utf-8",
        )
        (triggers / "pmm_probe_shared.txt").write_text(
            f"pmm_probe_shared_trigger = {{\n\tpmm_probe_shared_from_{name} = yes\n}}\n",
            encoding="utf-8",
        )
        events = folder / "events"
        events.mkdir(parents=True, exist_ok=True)
        (events / f"pmm_probe_only_{name}.txt").write_text(
            probe_event(f"pmm_probe_{name}", f"pmm_probe_loaded_{name}"), encoding="utf-8"
        )
        (events / "pmm_probe_shared.txt").write_text(
            probe_event("pmm_probe_shared", f"pmm_probe_shared_from_{name}"), encoding="utf-8"
        )
        (folder / "descriptor.mod").write_text(descriptor(name, None, supported_version), encoding="utf-8")
        (mod_dir / f"{probe_folder(name)}.mod").write_text(
            descriptor(name, folder.resolve(), supported_version), encoding="utf-8"
        )
        created.append(f"mod/{probe_folder(name)}.mod")
    return {"created": created}


def remove(mod_dir: Path, names: list[str]) -> dict:
    removed = []
    for name in names:
        folder = mod_dir / probe_folder(name)
        desc = mod_dir / f"{probe_folder(name)}.mod"
        if folder.is_dir():
            shutil.rmtree(folder)
            removed.append(str(folder.name))
        if desc.is_file():
            desc.unlink()
            removed.append(desc.name)
    return {"removed": removed}


def analyze(error_log: Path, enabled: list[str], disabled: list[str]) -> dict:
    text = error_log.read_text(encoding="utf-8", errors="replace")
    loaded = sorted(set(re.findall(r"pmm_probe_loaded_([a-z0-9]+)", text)))
    shared = re.findall(r"pmm_probe_shared_from_([a-z0-9]+)", text)
    shared_winners = sorted(set(shared))
    result = {
        "enabledOrder": enabled,
        "disabled": disabled,
        "loaded": loaded,
        "sharedFileWinner": shared_winners,
        "checks": {},
    }
    checks = result["checks"]
    checks["allEnabledLoaded"] = all(n in loaded for n in enabled)
    checks["noDisabledLoaded"] = not any(n in loaded for n in disabled)
    checks["singleSharedWinner"] = len(shared_winners) == 1
    if len(shared_winners) == 1 and enabled:
        winner = shared_winners[0]
        if winner == enabled[-1]:
            checks["precedence"] = "last-enabled-wins"
        elif winner == enabled[0]:
            checks["precedence"] = "first-enabled-wins"
        else:
            checks["precedence"] = f"unexpected-winner:{winner}"
    else:
        checks["precedence"] = "undetermined"
    result["ok"] = (
        checks["allEnabledLoaded"] and checks["noDisabledLoaded"] and checks["singleSharedWinner"]
    )
    return result


def validated_names(values: list[str]) -> list[str]:
    for v in values:
        if not NAME_RE.match(v):
            raise SystemExit(f"invalid probe name: {v!r} (use 1-16 lowercase letters/digits)")
    return values


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p_create = sub.add_parser("create")
    p_create.add_argument("mod_dir", type=Path)
    p_create.add_argument("--names", nargs="+", default=DEFAULT_NAMES)
    p_create.add_argument("--supported-version", help='e.g. "v4.5.*"; must match the game version format')
    p_remove = sub.add_parser("remove")
    p_remove.add_argument("mod_dir", type=Path)
    p_remove.add_argument("--names", nargs="+", default=DEFAULT_NAMES)
    p_analyze = sub.add_parser("analyze")
    p_analyze.add_argument("error_log", type=Path)
    p_analyze.add_argument("--enabled", nargs="+", required=True)
    p_analyze.add_argument("--disabled", nargs="*", default=[])
    args = parser.parse_args(argv)

    if args.command == "create":
        out = create(args.mod_dir, validated_names(args.names), args.supported_version)
    elif args.command == "remove":
        out = remove(args.mod_dir, validated_names(args.names))
    else:
        out = analyze(args.error_log, validated_names(args.enabled), validated_names(args.disabled))
    print(json.dumps(out, indent=2))
    return 0 if out.get("ok", True) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
