#pragma once

// Game adapter contract. Each supported game implements GameAdapter and owns
// everything game-specific: installation and user-data discovery, descriptor
// parsing and identity association, version detection, load-order
// translation, generated configuration, executable/arguments/environment, and
// the process names used to detect running instances.
//
// Adapters never write files themselves: they return a LaunchPlan whose
// ConfigWrites are applied by platform::FileTransactionManager (backups,
// rollback, crash recovery). Adapters must not assume another game's file
// formats, flags or precedence rules.

#include "core/catalog.hpp"
#include "core/model.hpp"
#include "core/result.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::games {

namespace fs = std::filesystem;

// Everything discovery depends on, injectable so tests never touch real
// Steam or Documents folders.
struct DiscoveryEnvironment {
    std::optional<fs::path> steamRoot; // nullopt: not installed / not found
    fs::path documentsDir;             // Paradox user data parent
    fs::path home;

    // Real environment of the current process (uses platform::steam and
    // platform::standardDirectories()).
    static DiscoveryEnvironment current();
};

struct LoadOrderInfo {
    // e.g. "dlc_load.json enabled_mods: index 0 is read first; for identical
    // file paths the later mod wins".
    std::string summary;
    bool verifiedInGame = false; // true only with recorded in-game evidence
    std::string evidence;        // where the evidence is documented
    // Shown to users when the playset order is not the order the game applies
    // (EU4 picks the winner of a shared file by mod name); empty otherwise.
    std::string userNote;
};

struct ProcessNames {
    std::vector<std::string> game;               // e.g. {"stellaris.exe"}
    std::vector<std::string> conflictingWriters; // e.g. Paradox launcher processes
};

class GameAdapter {
public:
    virtual ~GameAdapter() = default;

    virtual const GameCatalogEntry& catalog() const = 0;
    std::string_view key() const { return catalog().key; }

    virtual Capabilities capabilities(const GameInstallation& installation) const = 0;

    // Finds installations, Steam libraries first. Empty when not installed.
    virtual std::vector<GameInstallation> discover(const DiscoveryEnvironment& env) const = 0;

    // Validates a manually selected installation and user-data directory and
    // fills executable, version and arguments. userDataPath may be empty to
    // use the OS default for this game.
    virtual Result<GameInstallation> inspectInstallation(const fs::path& installPath,
                                                         const fs::path& userDataPath,
                                                         const DiscoveryEnvironment& env) const = 0;

    // Installed inventory: registered descriptors in the user mod folder plus
    // Workshop content folders (registered or not). A registered Workshop
    // item whose content is not on disk is not installed and is left out.
    // Problems are reported per mod, not as a failure of the whole scan.
    virtual Result<std::vector<InstalledMod>>
    scanMods(const GameInstallation& installation) const = 0;

    // The game's currently configured enabled mods in order (e.g. from
    // dlc_load.json), resolved against the inventory. An entry that matches
    // no installed mod keeps the identity its registration implies (a
    // Workshop item stays steam-workshop, so it can be acquired); anything
    // else is returned with source "local" and the reference stem as id.
    virtual Result<std::vector<PlaysetEntry>>
    readCurrentConfiguration(const GameInstallation& installation,
                             std::span<const InstalledMod> inventory) const = 0;

    // Builds a launch plan for the enabled mods in playset order (index 0
    // first). Generates only the minimum required ConfigWrites, preserving
    // unrelated settings (e.g. disabled DLC choices) from existing files.
    virtual Result<LaunchPlan>
    buildLaunchPlan(const GameInstallation& installation,
                    std::span<const InstalledMod> orderedEnabledMods) const = 0;

    virtual ProcessNames processNames(const GameInstallation& installation) const = 0;
    virtual LoadOrderInfo loadOrderInfo() const = 0;

    // Non-blocking compatibility warnings, e.g. supported_version mismatch.
    virtual std::vector<std::string> compatibilityWarnings(const GameInstallation& installation,
                                                           const InstalledMod& mod) const = 0;
};

// Registry of implemented adapters (EU4, Stellaris).
std::span<const std::unique_ptr<GameAdapter>> adapters();
const GameAdapter* findAdapter(std::string_view gameKey);

} // namespace pmm::games
