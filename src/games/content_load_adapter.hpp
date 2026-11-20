#pragma once

// Shared implementation for games whose mod configuration is
// "<user data>/content_load.json", which lists each enabled mod by the
// absolute path of its folder (or .zip), and whose mods describe themselves
// in <mod>/.metadata/metadata.json (the scheme the Paradox launcher v2 uses
// for Victoria 3, observed on 1.15.2). There are no registration files:
// Workshop folders are used in place. Discovery, launch arguments and the
// other parts every launcher-v2 game shares are in games/launcher_adapter.hpp.
//
// Files read: those of LauncherGameAdapter, appworkshop_<app>.acf, the
// .metadata/metadata.json of every folder directly in <user data>/mod and of
// every <library>/steamapps/workshop/content/<app>/<id>, and
// <user data>/content_load.json.
// Files written (through LaunchPlan::configWrites only): content_load.json,
// of which only "enabledMods" changes. The launcher's own database
// (launcher-v2.sqlite) is never touched.
//
// Mod identity (the entry's name in mod/, so a folder and a .zip never share
// an id; the metadata "id" is chosen freely by authors and is mostly empty):
//   - each folder directly in <user data>/mod: {game, local, <folder name>},
//     except Paradox Mods installs (pdx_<n>, pdx_<n>_<n>), which the launcher
//     skips too. Without a readable metadata.json it is Broken (the game
//     leaves such a mod out);
//   - each *.zip directly in <user data>/mod: {game, local, <file name>}. Its
//     metadata lies inside the archive and is not read: Ready, named after the
//     file;
//   - each <library>/steamapps/workshop/content/<app>/<id>:
//     {game, steam-workshop, <id>}, Downloading or NeedsUpdate as Steam's
//     Workshop manifest says.
// descriptorRef is the path that content_load.json holds for the mod.

#include "games/launcher_adapter.hpp"

#include <span>
#include <string>

namespace pmm::games {

class ContentLoadJsonAdapter : public LauncherGameAdapter {
public:
    explicit ContentLoadJsonAdapter(const GameTraits& traits);

    Result<std::vector<InstalledMod>> scanMods(const GameInstallation& installation) const override;
    // Each enabledMods path inside .../workshop/content/<app>/<id> is that
    // Workshop item, installed or not; any other path is the inventory mod
    // at that location, else a local mod named after the path's last
    // component that is not installed.
    Result<std::vector<PlaysetEntry>>
    readCurrentConfiguration(const GameInstallation& installation,
                             std::span<const InstalledMod> inventory) const override;
    // content_load.json with the mods' paths in playset order as
    // "enabledMods"; every other key ("disabledDLC", "enabledUGC", ...) is
    // kept as it is. A path the file already holds for a mod is reused as
    // written; new ones take the launcher's form (contentLoadPath). Written
    // as the launcher writes it, and not at all when the file already says
    // the same.
    Result<LaunchPlan>
    buildLaunchPlan(const GameInstallation& installation,
                    std::span<const InstalledMod> orderedEnabledMods) const override;
    // A folder needs a non-empty .metadata/metadata.json; a single file must
    // not be empty.
    Status verifyWorkshopContent(const fs::path& installPath) const override;
};

// The form the launcher writes into content_load.json: absolute, Windows
// separators, an upper-case drive letter, no trailing separator.
std::string contentLoadPath(const fs::path& path);

// A Paradox Mods install folder ("pdx_<digits>" or "pdx_<digits>_<digits>"),
// which the launcher leaves out of the local mods.
bool isParadoxModsFolder(std::string_view name) noexcept;

} // namespace pmm::games
