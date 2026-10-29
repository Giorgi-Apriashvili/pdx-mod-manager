#pragma once

// Shared implementation for games whose mod configuration is
// "<user data>/dlc_load.json + <user data>/mod/*.mod" (the scheme the Paradox
// launcher v2 uses for Stellaris and, per leftover launcher output, EU4).
// Everything that may differ per game is a GameTraits field; each game keeps
// its own adapter class so that it can diverge once evidence requires it.
//
// Files read: <install>/launcher-settings.json (or launcher/...), Steam
// libraryfolders.vdf, appmanifest_<app>.acf, appworkshop_<app>.acf,
// <library>/steamapps/workshop/content/<app>/<id>/descriptor.mod,
// <user data>/mod/*.mod and <user data>/dlc_load.json.
// Files written (through LaunchPlan::configWrites only): dlc_load.json and
// mod/ugc_<id>.mod for Workshop items that are not (correctly) registered.
// The launcher's own database (launcher-v2.sqlite) is never touched.
//
// Mod identity:
//   - a registered .mod whose path/archive lies inside any
//     .../steamapps/workshop/content/<app>/<id>, or a file named
//     ugc_<id>.mod whose remote_file_id is <id>: {game, steam-workshop, <id>};
//   - any other registered .mod: {game, local, <file stem>}. The path decides,
//     so a developer's local copy that carries a remote_file_id stays local;
//   - Workshop content folders without a registration: steam-workshop, with
//     descriptorRef "mod/ugc_<id>.mod" (generated at launch).

#include "games/game_adapter.hpp"

#include <span>
#include <string_view>

namespace pmm::games {

struct GameTraits {
    std::string_view key;            // catalog key, "stellaris"
    std::string_view steamAppId;     // "281990"
    std::string_view userDataFolder; // folder under "Paradox Interactive"
    std::string_view launcherGameId; // "gameId" in launcher-settings.json
    // Executable relative to the installation folder when launcher-settings
    // is missing or unusable.
    std::string_view windowsExecutable;
    std::string_view linuxExecutable;
    std::string_view macExecutable;
    // Processes that rewrite the configuration (the Paradox launcher).
    std::span<const std::string_view> windowsLauncherProcesses;
    std::span<const std::string_view> linuxLauncherProcesses;
    std::span<const std::string_view> macLauncherProcesses;
    // Wildcards before the patch number make the game log "Invalid
    // supported_version" (see games/supported_version.hpp).
    bool rejectsEarlyWildcards = false;
    std::string_view loadOrderSummary;
    std::string_view loadOrderEvidence;
    std::string_view logComponent; // "games.stellaris"
};

class DlcLoadJsonAdapter : public GameAdapter {
public:
    explicit DlcLoadJsonAdapter(const GameTraits& traits);

    const GameTraits& traits() const noexcept { return m_traits; }

    const GameCatalogEntry& catalog() const override;
    Capabilities capabilities(const GameInstallation& installation) const override;
    std::vector<GameInstallation> discover(const DiscoveryEnvironment& env) const override;
    Result<GameInstallation> inspectInstallation(const fs::path& installPath,
                                                 const fs::path& userDataPath,
                                                 const DiscoveryEnvironment& env) const override;
    Result<std::vector<InstalledMod>> scanMods(const GameInstallation& installation) const override;
    Result<std::vector<PlaysetEntry>>
    readCurrentConfiguration(const GameInstallation& installation,
                             std::span<const InstalledMod> inventory) const override;
    Result<LaunchPlan>
    buildLaunchPlan(const GameInstallation& installation,
                    std::span<const InstalledMod> orderedEnabledMods) const override;
    ProcessNames processNames(const GameInstallation& installation) const override;
    LoadOrderInfo loadOrderInfo() const override;
    std::vector<std::string> compatibilityWarnings(const GameInstallation& installation,
                                                   const InstalledMod& mod) const override;

    // <documents>/Paradox Interactive/<folder> (Windows, macOS) or
    // <xdgDataHome>/Paradox Interactive/<folder> (Linux). Empty when the base
    // folder is unknown.
    fs::path defaultUserDataPath(const DiscoveryEnvironment& env) const;
    // Fallback executable relative to the installation folder.
    fs::path defaultExecutable(OsPlatform os) const;

private:
    enum class Mode { Discovered, Manual };
    Result<GameInstallation> describe(const fs::path& installPath, const fs::path& userDataPath,
                                      const DiscoveryEnvironment& env, Mode mode) const;

    GameTraits m_traits;
    const GameCatalogEntry* m_catalog = nullptr;
};

// Steam Workshop id of `path` when it lies inside
// .../steamapps/workshop/content/<appId>/<id>; the id must be canonical
// decimal. `libraryRoot` receives the folder that contains steamapps.
std::optional<std::string> workshopIdFromPath(const fs::path& path, std::string_view appId,
                                              fs::path* libraryRoot = nullptr);

// "mod/ugc_123.mod" style reference for a descriptor file name.
std::string descriptorReference(const fs::path& descriptorFile);

} // namespace pmm::games
