#pragma once

// Shared implementation for games whose mod configuration is
// "<user data>/dlc_load.json + <user data>/mod/*.mod" (the scheme the Paradox
// launcher v2 uses for Stellaris, EU4 and Crusader Kings III, observed on all
// three). Discovery, launch arguments and the other parts every launcher-v2
// game shares are in games/launcher_adapter.hpp.
//
// Files read: those of LauncherGameAdapter, appworkshop_<app>.acf, the content
// descriptor of <library>/steamapps/workshop/content/<app>/<id> (see
// contentDescriptorFile), <user data>/mod/*.mod and <user data>/dlc_load.json.
// Files written (through LaunchPlan::configWrites only): dlc_load.json and
// mod/ugc_<id>.mod for Workshop items that are not (correctly) registered.
// The launcher's own database (launcher-v2.sqlite) is never touched.
//
// Mod identity:
//   - a registered .mod whose path/archive lies inside any
//     .../steamapps/workshop/content/<app>/<id>, or a file named
//     ugc_<id>.mod whose remote_file_id is <id>: {game, steam-workshop, <id>};
//   - any other registered .mod: {game, local, <file stem>}. The path decides,
//     so a developer's local copy that carries a remote_file_id stays local.
//     Files whose stems read the same (names with unpaired surrogates read
//     as U+FFFD) become one Broken entry that names them all;
//   - Workshop content folders without a registration: steam-workshop, with
//     descriptorRef "mod/ugc_<id>.mod" (generated at launch).
// A Workshop registration whose content exists in no Steam library (e.g. the
// game was reinstalled and Steam has not downloaded the item yet) is not an
// installed mod: it is left out of the inventory, so that the item reads as
// missing and can be acquired. A local mod without its folder stays Broken.

#include "games/launcher_adapter.hpp"

#include <optional>
#include <span>
#include <string_view>

namespace pmm::games {

class DlcLoadJsonAdapter : public LauncherGameAdapter {
public:
    explicit DlcLoadJsonAdapter(const GameTraits& traits);

    Result<std::vector<InstalledMod>> scanMods(const GameInstallation& installation) const override;
    Result<std::vector<PlaysetEntry>>
    readCurrentConfiguration(const GameInstallation& installation,
                             std::span<const InstalledMod> inventory) const override;
    Result<LaunchPlan>
    buildLaunchPlan(const GameInstallation& installation,
                    std::span<const InstalledMod> orderedEnabledMods) const override;
};

// "mod/ugc_123.mod" style reference for a descriptor file name.
std::string descriptorReference(const fs::path& descriptorFile);

// The descriptor of a Workshop content folder: its descriptor.mod, else the
// only *.mod file directly inside it (Workshop items whose author shipped
// "<Name>.mod" instead, observed for CK3). nullopt when there is neither,
// when several *.mod files leave it open which one describes the mod, or when
// the folder holds a .zip (a legacy zipped item, described inside its archive).
std::optional<fs::path> contentDescriptorFile(const fs::path& folder);

} // namespace pmm::games
