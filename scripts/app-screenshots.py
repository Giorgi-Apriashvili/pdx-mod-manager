#!/usr/bin/env python3
"""Captures the real application's screenshots in docs/screenshots with
made-up games and mods (docs/app.md, "Hidden verification switches").

The application finds games through Steam and `Documents/Paradox Interactive`;
the captures must not show the mods of whoever runs them. This script writes a
made-up Steam library and made-up user data folders for all five games under
--root: launcher-settings.json, an executable placeholder, and for Stellaris
and EU4 descriptors, `.mod` registrations and a dlc_load.json with invented
names and Workshop ids (no game or mod files). It stores these folders in new
profiles as manual locations with their user data folders, which is what
"Locate installation..." stores, and runs pmm-app against them:

  python scripts/app-screenshots.py [--app build/debug/bin/pmm-app.exe]
                                    [--root build/app-screenshots]
                                    [--out docs/screenshots]

Qt's bin folder must be on PATH. Every pmm-app run uses --profile and a
verification switch, so it is read-only and starts no Steam helper. The first
run creates a profile database; like any start it discovers the installed
games (read-only), so its capture is deleted unseen, the discovered
installations and cached mod lists are removed before the made-up locations
are stored, and the profile is deleted at the end; the profiles of the
captures are copies of its database. The captures are written to --out; look
at every one before committing it (docs/app.md: focus ring, tool tip).

Uses only the Python standard library. Apart from Qt's graphics pipeline cache
(docs/app.md, "Files and folders"), nothing is written outside --root and --out.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sqlite3
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MARKER = ".pmm-app-screenshots"

# Made-up mods: (source, name, version, supported_version, tags, dependencies).
# "W" is a Workshop item, "L" a local mod. Names and ids are invented.
STELLARIS_CONFIG = [  # dlc_load.json order
    ("W", "Galaxy Map Labels", "2.1", "v4.3.*", ["Interface"], []),
    ("W", "Colony Core Library", "1.4.0", "v4.5.*", ["Gameplay"], []),
    ("W", "Frontier Colonies", "1.0.0", "v4.3.*", ["Balance"], ["Colony Core Library"]),
    ("W", "Calm Event Pacing", "3.0", "v4.4.*", ["Events"], []),
    ("L", "Personal UI Tweaks", "1.0", "v4.5.*", ["Interface"], []),
    ("L", "Starport Queue Helper", "0.3", "v4.5.*", ["Gameplay"], []),
    ("W", "Leader Portrait Collection", "2.6.1", "v4.5.*", ["Graphics"], []),
    ("W", "Nomad Fleet Events", "1.2", "v4.2.*", ["Events"], []),
    ("L", "Outliner Spacing Fix", "1.0", "v4.5.*", ["Interface"], []),
    ("L", "Habitat Name Lists", "1.1", "v4.5.*", ["Gameplay"], []),
    ("L", "Shorter Tooltips", "1.0", "v4.5.*", ["Interface"], []),
    ("L", "Trade Value Display", "0.9", "v4.5.*", ["Economy"], []),
    ("W", "Research Speed Options", "4.0", "v4.3.*", ["Technologies"], []),
    ("W", "Quiet Starbase Alerts", "1.5", "v4.1.*", ["Interface"], []),
]
STELLARIS_OTHERS = [  # installed, not enabled
    ("W", "Ambient Music Collection", "1.0", "v4.5.*", ["Sound"], []),
    ("W", "Asteroid Mining Outposts", "2.2", "v4.5.*", ["Economy"], []),
    ("W", "Bigger Galaxy Sizes", "1.3", "v4.5.*", ["Gameplay"], []),
    ("L", "Ceremonial Ship Names", "1.0", "v4.5.*", ["Gameplay"], []),
    ("W", "Crisis Difficulty Options", "3.1", "v4.4.*", ["Balance"], []),
    ("W", "Dark Nebula Skyboxes", "1.0", "v4.5.*", ["Graphics"], []),
    ("W", "Distant Stars Encyclopedia", "2.0", "v4.5.*", ["Events"], []),
    ("W", "Early Game Pacing", "1.7", "v4.5.*", ["Balance"], []),
    ("W", "Gas Giant Habitats", "1.1", "v4.3.*", ["Gameplay"], []),
    ("W", "Larger Fleet Icons", "1.0", "v4.5.*", ["Interface"], []),
    ("L", "Merchant Guild Contracts", "0.5", "v4.5.*", ["Economy"], []),
    ("W", "Planet Name Generator", "2.4", "v4.5.*", ["Gameplay"], []),
    ("W", "Robot Pop Variety", "1.6", "v4.5.*", ["Graphics"], []),
    ("W", "Sector Border Styles", "1.0", "v4.5.*", ["Interface"], []),
    ("W", "Wormhole Map Markers", "1.2", "v4.5.*", ["Interface"], []),
]
EU4_CONFIG = [
    ("W", "Clearer Map Modes", None, "v1.37.*", ["Graphics"], []),
    ("L", "Readable Fonts", "1.1", "v1.37.*", ["Interface"], []),
    ("W", "Province Name Fixes", "1.2", "v1.37.*", ["Map"], []),
]

# key: (Steam folder, app id, executable, launcher-settings.json folder,
#       user data folder, gameId, version, rawVersion, first made-up id)
GAMES = {
    "ck3": ("Crusader Kings III", "1158310", "binaries/ck3.exe", "launcher",
            "Crusader Kings III", "ck3", "1.21.0.3", "1.21.0.3", 0),
    "eu4": ("Europa Universalis IV", "236850", "eu4.exe", ".", "Europa Universalis IV",
            "eu4", "EU4 v1.37.5.0", "v1.37.5.0", 3100000201),
    "hoi4": ("Hearts of Iron IV", "394360", "hoi4.exe", ".", "Hearts of Iron IV", "hoi4",
             "v1.20.0.0", "1.20.0.0", 0),
    "stellaris": ("Stellaris", "281990", "stellaris.exe", ".", "Stellaris", "stellaris",
                  "Cygnus v4.5.4", "v4.5.4", 3100000101),
    "vic3": ("Victoria 3", "529340", "binaries/victoria3.exe", "launcher", "Victoria 3",
             "victoria3", "1.15.2", "1.15.2", 0),
}
MODS = {"stellaris": (STELLARIS_CONFIG, STELLARIS_OTHERS), "eu4": (EU4_CONFIG, [])}


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def descriptor(name, version, supported, tags, dependencies, extra=()):
    lines = [f'name="{name}"']
    if version:
        lines.append(f'version="{version}"')
    lines.append("tags={")
    lines += [f'\t"{tag}"' for tag in tags]
    lines.append("}")
    if dependencies:
        lines.append("dependencies={")
        lines += [f'\t"{dependency}"' for dependency in dependencies]
        lines.append("}")
    lines.append(f'supported_version="{supported}"')
    lines += list(extra)
    return "\n".join(lines) + "\n"


def folder_name(name: str) -> str:
    return "".join(c if c.isalnum() else "_" for c in name.lower()).strip("_")


def make_installation(root: Path) -> dict[str, tuple[Path, Path, Path]]:
    """Writes the made-up games; returns key -> (install, user data, library)."""
    library = root / "Steam"
    documents = root / "Documents" / "Paradox Interactive"
    locations = {}
    for key, (folder, app_id, exe, settings_dir, data_folder, game_id, version, raw,
              first_id) in GAMES.items():
        install = library / "steamapps" / "common" / folder
        user_data = documents / data_folder
        exe_path = install / exe
        write(exe_path, "Placeholder: not a game executable.\n")
        settings_path = install / settings_dir / "launcher-settings.json"
        relative_exe = os.path.relpath(exe_path, settings_path.parent).replace(os.sep, "/")
        write(settings_path, json.dumps({
            "gameId": game_id,
            "version": version,
            "rawVersion": raw,
            "distPlatform": "steam",
            "gameDataPath": f"%USER_DOCUMENTS%/Paradox Interactive/{data_folder}",
            "exePath": "./" + relative_exe if not relative_exe.startswith(".") else relative_exe,
            "exeArgs": ["-gdpr-compliant"],
        }, indent="\t") + "\n")
        (user_data / "mod").mkdir(parents=True, exist_ok=True)
        locations[key] = (install, user_data, library)
        if key not in MODS:
            continue
        enabled_mods, others = MODS[key]
        next_id = first_id
        enabled = []
        for index, (source, name, version_text, supported, tags,
                    dependencies) in enumerate(enabled_mods + others):
            if source == "W":
                mod_id = str(next_id)
                next_id += 1
                content = library / "steamapps" / "workshop" / "content" / app_id / mod_id
                remote = [f'remote_file_id="{mod_id}"']
                write(content / "descriptor.mod",
                      descriptor(name, version_text, supported, tags, dependencies, remote))
                registration = f"ugc_{mod_id}.mod"
            else:
                local = folder_name(name)
                content = user_data / "mod" / local
                write(content / "descriptor.mod",
                      descriptor(name, version_text, supported, tags, dependencies))
                registration = f"{local}.mod"
                remote = []
            path = [f'path="{content.as_posix()}"']
            write(user_data / "mod" / registration,
                  descriptor(name, version_text, supported, tags, dependencies, path + remote))
            if index < len(enabled_mods):
                enabled.append(f"mod/{registration}")
        write(user_data / "dlc_load.json",
              json.dumps({"disabled_dlcs": [], "enabled_mods": enabled}) + "\n")
    return locations


def run_app(app: Path, profile: Path, args: list[str], env: dict[str, str]) -> None:
    command = [str(app), "--profile", str(profile), *args]
    result = subprocess.run(command, env=env, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=600)
    if result.returncode != 0:
        sys.exit(f"app-screenshots: {' '.join(args)} failed ({result.returncode}):\n"
                 f"{result.stdout[-2000:]}{result.stderr[-2000:]}")


def make_base_profile(app: Path, root: Path, locations, env) -> Path:
    """A profile whose games are the made-up ones, located manually."""
    base = root / "profiles" / "base"
    discard = root / "discard.png"
    # Creates the database. Discovers the installed games (read-only); the
    # capture shows them, so it is deleted without being looked at.
    run_app(app, base, ["--game", "stellaris", "--screenshot", str(discard)], env)
    discard.unlink(missing_ok=True)
    db = sqlite3.connect(base / "state.sqlite")
    with db:
        for table in ("installed_mods", "game_installations", "playset_entries",
                      "playset_history", "playsets", "acquisition_jobs", "launch_history"):
            db.execute(f"DELETE FROM {table}")
        for key, (install, user_data, library) in locations.items():
            app_id, exe = GAMES[key][1:3]
            db.execute(
                "INSERT INTO game_installations (game_key, install_path, user_data_path, "
                "executable_path, default_args_json, distribution, steam_app_id, "
                "steam_library_path, version, version_label, manually_configured, updated_at) "
                "VALUES (?, ?, ?, ?, '[]', 'steam', ?, ?, '', '', 1, ?)",
                (key, str(install), str(user_data), str(install / exe), app_id, str(library),
                 int(time.time() * 1000)))
    db.execute("VACUUM")  # the copies keep nothing of what was deleted
    db.close()
    return base


def new_profile(base: Path, name: str) -> Path:
    profile = base.parent / name
    profile.mkdir(parents=True)
    source = sqlite3.connect(base / "state.sqlite")
    target = sqlite3.connect(profile / "state.sqlite")
    source.backup(target)
    target.close()
    source.close()
    return profile


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--app", type=Path, default=REPO / "build/debug/bin/pmm-app.exe")
    parser.add_argument("--root", type=Path, default=REPO / "build/app-screenshots")
    parser.add_argument("--out", type=Path, default=REPO / "docs/screenshots")
    options = parser.parse_args()
    app, root, out = options.app.resolve(), options.root.resolve(), options.out.resolve()
    if not app.is_file():
        sys.exit(f"app-screenshots: {app} not found; build it or pass --app")
    if root.exists():
        if not (root / MARKER).is_file():
            sys.exit(f"app-screenshots: {root} exists and was not made by this script")
        shutil.rmtree(root)
    root.mkdir(parents=True)
    (root / MARKER).touch()
    out.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env.pop("QT_SCALE_FACTOR", None)
    # No Workshop lookups leave the machine, whatever the captures show.
    env["PMM_WORKSHOP_METADATA_URL"] = "http://127.0.0.1:9/"

    locations = make_installation(root)
    base = make_base_profile(app, root, locations, env)
    code = root / "code.txt"

    def shot(profile: Path, name: str, *args: str, theme: str = "light",
             scale: str | None = None) -> None:
        shot_env = dict(env)
        if scale:
            shot_env["QT_SCALE_FACTOR"] = scale
        run_app(app, profile, [*args, "--theme", theme, "--screenshot", str(out / name)],
                shot_env)
        print("wrote", out / name)

    # Stellaris: one entry disabled, two moved, a share code for the import.
    stellaris = new_profile(base, "stellaris")
    run_app(app, stellaris, ["--game", "stellaris", "--self-test",
                             f"config,disable:2,move:0:5,move:13:1,export:{code}"], env)
    shot(stellaris, "app-main-light.png", "--game", "stellaris", "--select-row", "2")
    shot(stellaris, "app-main-dark.png", "--game", "stellaris", "--select-row", "2",
         theme="dark")
    shot(stellaris, "app-filtered.png", "--game", "stellaris", "--filter", "disabled",
         "--select-row", "0")
    shot(stellaris, "app-export.png", "--game", "stellaris", "--select-row", "2",
         "--open", "export")
    shot(stellaris, "app-add-mods.png", "--game", "stellaris", "--select-row", "2",
         "--open", "add-mods")
    shot(new_profile(base, "import"), "app-import-preview.png", "--game", "stellaris",
         "--open", f"import:{code}")

    eu4 = new_profile(base, "eu4")
    run_app(app, eu4, ["--game", "eu4", "--self-test", "config"], env)
    shot(eu4, "app-eu4.png", "--game", "eu4", "--select-row", "0")

    # The playset from the configuration as it is, at 150 % and 200 %.
    scaled = new_profile(base, "scaled")
    run_app(app, scaled, ["--game", "stellaris", "--self-test", "config"], env)
    shot(scaled, "a11y-scale-150.png", "--game", "stellaris", scale="1.5")
    shot(scaled, "a11y-scale-200.png", "--game", "stellaris", scale="2")
    # Its log names the games and mods its first run discovered.
    shutil.rmtree(base)


if __name__ == "__main__":
    main()
