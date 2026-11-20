#pragma once

// Shared base of the adapters for games that the Paradox launcher v2 starts
// (launcher-settings.json next to the game or in <install>/launcher/). A
// family derives from it for each way the enabled mods are stored:
//   - DlcLoadJsonAdapter (games/dlc_load_adapter.hpp): dlc_load.json plus
//     mod/*.mod registrations (Stellaris, EU4, Crusader Kings III);
//   - ContentLoadJsonAdapter (games/content_load_adapter.hpp):
//     content_load.json with absolute paths, mods described by their own
//     .metadata/metadata.json (Victoria 3).
// Everything that may differ per game within a family is a GameTraits field;
// each game keeps its own adapter class so that it can diverge once evidence
// requires it.
//
// The base covers what the families share: installation and user-data
// discovery, the launcher-settings.json checks, the capabilities, the process
// names, the load-order information, the supported-version warnings and the
// generic part of a launch plan (beginLaunchPlan).
//
// Files read here: <install>/launcher-settings.json (or launcher/...), the
// GameTraits::userDirFile (<install>/userdir.txt, CK3:
// <install>/game/userdir.txt), Steam libraryfolders.vdf and
// appmanifest_<app>.acf.
//
// User data: a folder chosen by the user, else an absolute path in the
// GameTraits::userDirFile, else launcher-settings.json "gameDataPath", else
// the OS default.
//
// Launch: the executable from launcher-settings.json (else
// GameTraits::executable), started in its own folder (the installation folder
// for Stellaris and EU4, <install>/binaries for CK3 and Victoria 3).

#include "games/game_adapter.hpp"

#include <optional>
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
    std::string_view executable;
    // Processes that rewrite the configuration (the Paradox launcher).
    std::span<const std::string_view> launcherProcesses;
    // Path relative to the installation folder of the file that relocates
    // the user-data folder ("userdir.txt", CK3 and Victoria 3:
    // "game/userdir.txt"); empty when the game has no such file.
    std::string_view userDirFile;
    // launcher-settings.json arguments that are never passed on the user's
    // behalf, compared ASCII case-insensitively ("-enabletelemetry").
    std::span<const std::string_view> droppedArguments;
    // Wildcards before the patch number make the game log "Invalid
    // supported_version" (see games/supported_version.hpp).
    bool rejectsEarlyWildcards = false;
    // A pattern that is only "*" matches no version: the game logs that it
    // does not match and lists the mod as out of date (Victoria 3 1.15.2; see
    // games/supported_version.hpp).
    bool loneWildcardMismatches = false;
    std::string_view loadOrderSummary;
    std::string_view loadOrderEvidence;
    bool loadOrderVerifiedInGame = false; // probe-mod run recorded in the evidence
    std::string_view loadOrderUserNote;   // LoadOrderInfo::userNote
    std::string_view logComponent;        // "games.stellaris"
    // The descriptor key that compatibilityWarnings() names (Victoria 3:
    // "supported_game_version").
    std::string_view supportedVersionKey = "supported_version";
    // Further processes that count as the running game, e.g. a console
    // wrapper that starts the executable (Victoria 3).
    std::span<const std::string_view> extraGameProcesses = {};
};

class LauncherGameAdapter : public GameAdapter {
public:
    const GameTraits& traits() const noexcept { return m_traits; }

    const GameCatalogEntry& catalog() const override;
    Capabilities capabilities(const GameInstallation& installation) const override;
    std::vector<GameInstallation> discover(const DiscoveryEnvironment& env) const override;
    Result<GameInstallation> inspectInstallation(const fs::path& installPath,
                                                 const fs::path& userDataPath,
                                                 const DiscoveryEnvironment& env) const override;
    ProcessNames processNames(const GameInstallation& installation) const override;
    LoadOrderInfo loadOrderInfo() const override;
    std::vector<std::string> compatibilityWarnings(const GameInstallation& installation,
                                                   const InstalledMod& mod) const override;

    // <documents>/Paradox Interactive/<folder>. Empty when the base folder is
    // unknown.
    fs::path defaultUserDataPath(const DiscoveryEnvironment& env) const;
    // Fallback executable relative to the installation folder.
    fs::path defaultExecutable() const;
    // `arguments` without the droppedArguments, order kept.
    std::vector<std::string> launchArguments(const std::vector<std::string>& arguments) const;

protected:
    explicit LauncherGameAdapter(const GameTraits& traits);

    // A launch plan without configuration writes: the installation's
    // executable started in its own folder with the launcher's arguments and,
    // for Steam installations, SteamAppId/SteamGameId. NotFound when the
    // executable is missing, InvalidArgument when the user-data folder is not
    // set.
    Result<LaunchPlan> beginLaunchPlan(const GameInstallation& installation) const;

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

// The user-data folder named by a userdir.txt file: its first non-empty line,
// without surrounding whitespace and one pair of quotes, lexically
// normalised. nullopt when the file is missing or blank. ValidationFailed
// when the line is not an absolute path (relative paths are not supported:
// what they would be relative to is unverified) or not valid UTF-8.
Result<std::optional<fs::path>> readUserDirFile(const fs::path& file);

} // namespace pmm::games
