#include "games/stellaris.hpp"

#include <array>

namespace pmm::games {
namespace {

// Paradox launcher v2: "Paradox Launcher.exe" plus its "dowser" helper, which
// also ships in the Stellaris install folder.
constexpr std::array<std::string_view, 2> kWindowsLauncher{"Paradox Launcher.exe", "dowser.exe"};
constexpr std::array<std::string_view, 2> kLinuxLauncher{"dowser", "paradox-launcher"};
constexpr std::array<std::string_view, 1> kMacLauncher{"Paradox Launcher"};
// Not requested by v4.5.2's launcher-settings.json; dropped all the same.
constexpr std::array<std::string_view, 1> kDroppedArguments{"-enabletelemetry"};

constexpr GameTraits kTraits{
    .key = "stellaris",
    .steamAppId = "281990",
    .userDataFolder = "Stellaris",
    .launcherGameId = "stellaris",
    .windowsExecutable = "stellaris.exe",
    .linuxExecutable = "stellaris",
    .macExecutable = "stellaris.app/Contents/MacOS/stellaris",
    .windowsLauncherProcesses = kWindowsLauncher,
    .linuxLauncherProcesses = kLinuxLauncher,
    .macLauncherProcesses = kMacLauncher,
    // stellaris.exe v4.5.2 references userdir.txt as eu4.exe does (no such
    // file ships with the game).
    .userDirFile = "userdir.txt",
    .droppedArguments = kDroppedArguments,
    // error.log of v4.5.2 reports "Invalid supported_version" for "v4.*.*",
    // "v4.*" and "*".
    .rejectsEarlyWildcards = true,
    .loadOrderSummary = "dlc_load.json enabled_mods, index 0 first; when two enabled mods "
                        "contain the same file, the one later in the list wins",
    .loadOrderEvidence = "docs/evidence/live-validation-2026-10-30.md (probe mods, Stellaris "
                         "4.5.2, Windows 11)",
    .loadOrderVerifiedInGame = true,
    .loadOrderUserNote = {}, // the playset order is the order the game applies
    .logComponent = "games.stellaris",
};

} // namespace

const GameTraits& stellarisTraits() noexcept {
    return kTraits;
}

} // namespace pmm::games
