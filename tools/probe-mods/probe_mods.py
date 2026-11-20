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
so the event files carry the same trigger names. `analyze` finds a trigger
name anywhere in a line, so either source is enough.

The event syntax depends on the game (--game):

  stellaris, eu4  country_event = { id = <ns>.1 ... is_triggered_only = yes }
                  (the default; both games, in-game checked on 2026-10-30)
  ck3             <ns>.1 = { type = character_event  hidden = yes ... }, the
                  CK3 form (no CK3 event file uses country_event). The files
                  start with a UTF-8 BOM, as every event and scripted-trigger
                  file of CK3 1.20.1.2 does. CK3's error.log reports unknown
                  triggers in common/scripted_triggers (seen on a 1.19 build,
                  and on 1.20.1.2 in the probe run of 2026-11-13).
  vic3            <ns>.1 = { type = country_event  hidden = yes ... }, the
                  form of Victoria 3 1.15.2's own events, with a UTF-8 BOM and
                  LF line ends as all of its event and scripted-trigger files
                  have (the other games' probes are written in text mode, so
                  with CRLF on Windows, which CK3 loaded). Not yet used
                  against a running game; how its error.log words an unknown
                  trigger has not been seen.

Victoria 3 has no .mod files: a probe is a folder with
.metadata/metadata.json, and the game reads the absolute folder paths from
content_load.json. `create --game vic3` prints those paths.

Usage:
  probe_mods.py create <mod-dir> [--game ck3|vic3] [--names a b c] [--supported-version v4.5.*]
                       [--no-shared] [--no-game-custom-data]
      Writes probe folders and .mod descriptors (Victoria 3: metadata.json)
      into the game's user mod directory (e.g. "Documents/Paradox
      Interactive/Stellaris/mod"). --supported-version must use the game's
      version format and must not make the game report it as invalid:
      "v4.5.*" for Stellaris, "1.20.*" for CK3 (never "1.*"), "1.15.*" for
      Victoria 3. --no-shared leaves out the shared file, for probes that only
      test a supported version and take no part in the precedence test.
      --no-game-custom-data (Victoria 3) leaves "game_custom_data" out of the
      metadata, which reportedly makes the game skip the mod.
  probe_mods.py analyze <error.log> --enabled c a [--disabled b] [--game vic3]
      Reads the game's error.log after it reached the main menu and reports
      which probes loaded and which one won the shared file, as JSON. With
      --game vic3 it also reads debug.log and debug.1.log to debug.5.log next
      to it (the log rotates within a session) and reports the latest
      session's "Mod:" block, its "Mounted Data:" lines, the supported version
      lines of the probes and every "Invalid supported_version" line.
  probe_mods.py remove <mod-dir> [--game vic3] [--names a b c]
      Deletes the probe folders and descriptors created by `create`
      (Victoria 3: the folders only).

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
GAMES = ["stellaris", "eu4", "ck3", "vic3"]
# Victoria 3's debug.log rotates at about 512 KB, also within one session.
VIC3_DEBUG_LOGS = ["debug.log"] + [f"debug.{n}.log" for n in range(1, 6)]


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


def metadata(name: str, supported_version: str | None, game_custom_data: bool) -> str:
    # Victoria 3's .metadata/metadata.json, as its launcher writes one for a
    # local mod (UTF-8 without BOM, LF, two-space indent).
    document = {
        "name": f"PMM Probe {name.upper()}",
        "id": "",
        "version": "1.0",
        "supported_game_version": supported_version or "",
        "short_description": "",
        "tags": [],
        "relationships": [],
    }
    if game_custom_data:
        document["game_custom_data"] = {"multiplayer_synchronized": True}
    return json.dumps(document, indent=2) + "\n"


def windows_path(path: Path) -> str:
    # The form content_load.json holds: absolute, backslashes, upper-case drive.
    text = str(path.resolve())
    if len(text) >= 2 and text[1] == ":":
        text = text[0].upper() + text[1:]
    return text


def probe_event(namespace: str, trigger: str, game: str) -> str:
    # Never fires (triggered only or hidden, unknown trigger); the game reports
    # the unknown trigger with the file it came from while loading events.
    if game in ("ck3", "vic3"):
        event_type = "character_event" if game == "ck3" else "country_event"
        return (
            f"namespace = {namespace}\n\n"
            f"{namespace}.1 = {{\n"
            f"\ttype = {event_type}\n"
            "\thidden = yes\n"
            f"\ttrigger = {{\n\t\t{trigger} = yes\n\t}}\n"
            "}\n"
        )
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


def script_encoding(game: str) -> str:
    # CK3's and Victoria 3's own script files all start with a UTF-8 BOM;
    # Stellaris and EU4 probes are written without one, as checked in game.
    return "utf-8-sig" if game in ("ck3", "vic3") else "utf-8"


def script_newline(game: str) -> str | None:
    # Victoria 3's own script files have LF line ends; the other games' probes
    # keep the platform's (CRLF on Windows), as checked in game.
    return "\n" if game == "vic3" else None


def create(
    mod_dir: Path,
    names: list[str],
    supported_version: str | None,
    game: str,
    shared: bool = True,
    game_custom_data: bool = True,
) -> dict:
    if not mod_dir.is_dir():
        raise SystemExit(f"mod directory does not exist: {mod_dir}")
    encoding = script_encoding(game)
    newline = script_newline(game)
    created = []
    for name in names:
        folder = mod_dir / probe_folder(name)
        triggers = folder / "common" / "scripted_triggers"
        triggers.mkdir(parents=True, exist_ok=True)
        (triggers / f"pmm_probe_only_{name}.txt").write_text(
            f"pmm_probe_only_{name}_trigger = {{\n\tpmm_probe_loaded_{name} = yes\n}}\n",
            encoding=encoding,
            newline=newline,
        )
        if shared:
            (triggers / "pmm_probe_shared.txt").write_text(
                f"pmm_probe_shared_trigger = {{\n\tpmm_probe_shared_from_{name} = yes\n}}\n",
                encoding=encoding,
                newline=newline,
            )
        events = folder / "events"
        events.mkdir(parents=True, exist_ok=True)
        (events / f"pmm_probe_only_{name}.txt").write_text(
            probe_event(f"pmm_probe_{name}", f"pmm_probe_loaded_{name}", game),
            encoding=encoding,
            newline=newline,
        )
        if shared:
            (events / "pmm_probe_shared.txt").write_text(
                probe_event("pmm_probe_shared", f"pmm_probe_shared_from_{name}", game),
                encoding=encoding,
                newline=newline,
            )
        if game == "vic3":
            # No .mod files: the folder is listed in content_load.json by path.
            meta = folder / ".metadata"
            meta.mkdir(parents=True, exist_ok=True)
            (meta / "metadata.json").write_bytes(
                metadata(name, supported_version, game_custom_data).encode("utf-8")
            )
            created.append(windows_path(folder))
            continue
        (folder / "descriptor.mod").write_text(descriptor(name, None, supported_version), encoding="utf-8")
        (mod_dir / f"{probe_folder(name)}.mod").write_text(
            descriptor(name, folder.resolve(), supported_version), encoding="utf-8"
        )
        created.append(f"mod/{probe_folder(name)}.mod")
    return {"created": created}


def remove(mod_dir: Path, names: list[str], game: str = "stellaris") -> dict:
    removed = []
    for name in names:
        folder = mod_dir / probe_folder(name)
        desc = mod_dir / f"{probe_folder(name)}.mod"
        if folder.is_dir():
            shutil.rmtree(folder)
            removed.append(str(folder.name))
        if game != "vic3" and desc.is_file():
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


def probe_names_in(lines: list[str]) -> list[str]:
    names = []
    for line in lines:
        match = re.search(r"pmm_probe_([a-z0-9]+)", line)
        if match:
            names.append(match.group(1))
    return names


def latest_session(logs_dir: Path) -> tuple[list[str], list[str]]:
    # The debug logs that exist, oldest first, and the lines of the latest
    # session: from the last "DLC:" block, which the engine prints once per
    # start, before its "Mod:" block.
    files = [logs_dir / name for name in VIC3_DEBUG_LOGS if (logs_dir / name).is_file()]
    files.sort(key=lambda f: (f.stat().st_mtime, -VIC3_DEBUG_LOGS.index(f.name)))
    lines = []
    for f in files:
        lines.extend(f.read_text(encoding="utf-8", errors="replace").splitlines())
    starts = [i for i, line in enumerate(lines) if re.search(r"\]: DLC:\s*$", line)]
    return [f.name for f in files], lines[starts[-1] :] if starts else lines


def analyze_vic3(error_log: Path, enabled: list[str], disabled: list[str], result: dict) -> None:
    read, session = latest_session(error_log.parent)
    mod_block = []
    in_block = False
    for line in session:
        if line.strip() == "Mod:" or re.search(r"\]: Mod:\s*$", line):
            in_block = True
            mod_block = []
            continue
        if in_block:
            # "<name>|<absolute path>" lines, until the next logged line.
            if line.startswith("[") or "|" not in line:
                in_block = False
                continue
            mod_block.append(line)
    mounted = [line for line in session if "Mounted Data:" in line]
    versions = [
        line.split("]: ", 1)[-1]
        for line in session
        if "PMM Probe" in line and ("matched game version" in line or "does not match game version" in line)
    ]
    error_text = error_log.read_text(encoding="utf-8", errors="replace").splitlines()
    invalid = [line.split("]: ", 1)[-1] for line in error_text + session if "Invalid supported_version" in line]
    block_probes = probe_names_in(mod_block)
    mounted_probes = probe_names_in(mounted)
    result["debugLogs"] = read
    result["modBlock"] = {"probes": block_probes, "others": len(mod_block) - len(block_probes)}
    result["mounted"] = {"probes": mounted_probes, "others": len(mounted) - len(mounted_probes)}
    result["supportedVersionLines"] = versions
    result["invalidSupportedVersion"] = invalid
    checks = result["checks"]
    checks["modBlockIsEnabledOrder"] = block_probes == enabled
    checks["noDisabledInModBlock"] = not any(n in block_probes for n in disabled)
    result["ok"] = result["ok"] and checks["modBlockIsEnabledOrder"] and checks["noDisabledInModBlock"]


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
    p_create.add_argument(
        "--game", choices=GAMES, default="stellaris", help="event syntax (stellaris and eu4 share one)"
    )
    p_create.add_argument(
        "--no-shared", action="store_true", help="leave out the shared file (version probes)"
    )
    p_create.add_argument(
        "--no-game-custom-data", action="store_true", help='vic3: no "game_custom_data" in the metadata'
    )
    p_remove = sub.add_parser("remove")
    p_remove.add_argument("mod_dir", type=Path)
    p_remove.add_argument("--names", nargs="+", default=DEFAULT_NAMES)
    p_remove.add_argument("--game", choices=GAMES, default="stellaris", help="vic3: folders only")
    p_analyze = sub.add_parser("analyze")
    p_analyze.add_argument("error_log", type=Path)
    p_analyze.add_argument("--enabled", nargs="+", required=True)
    p_analyze.add_argument("--disabled", nargs="*", default=[])
    p_analyze.add_argument("--game", choices=GAMES, default="stellaris", help="vic3: also the debug logs")
    args = parser.parse_args(argv)

    if args.command == "create":
        out = create(
            args.mod_dir,
            validated_names(args.names),
            args.supported_version,
            args.game,
            shared=not args.no_shared,
            game_custom_data=not args.no_game_custom_data,
        )
    elif args.command == "remove":
        out = remove(args.mod_dir, validated_names(args.names), args.game)
    else:
        enabled = validated_names(args.enabled)
        disabled = validated_names(args.disabled)
        out = analyze(args.error_log, enabled, disabled)
        if args.game == "vic3":
            analyze_vic3(args.error_log, enabled, disabled, out)
    print(json.dumps(out, indent=2))
    return 0 if out.get("ok", True) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
