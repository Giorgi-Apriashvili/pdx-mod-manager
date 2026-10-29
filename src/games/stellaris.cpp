#include "games/stellaris.hpp"

#include <array>

namespace pmm::games {
namespace {

// Paradox launcher v2: "Paradox Launcher.exe" plus its "dowser" helper, which
// also ships in the Stellaris install folder.
constexpr std::array<std::string_view, 2> kWindowsLauncher{"Paradox Launcher.exe", "dowser.exe"};
constexpr std::array<std::string_view, 2> kLinuxLauncher{"dowser", "paradox-launcher"};
constexpr std::array<std::string_view, 1> kMacLauncher{"Paradox Launcher"};

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
    // error.log of v4.5.2 reports "Invalid supported_version" for "v4.*.*",
    // "v4.*" and "*".
    .rejectsEarlyWildcards = true,
    .loadOrderSummary = "dlc_load.json enabled_mods, index 0 first; file override precedence: "
                        "see docs/load-order.md",
    .loadOrderEvidence = "docs/load-order.md (Stellaris: launcher order observed read-only; "
                         "in-game precedence pending the probe-mod run)",
    .logComponent = "games.stellaris",
};

} // namespace

const GameTraits& stellarisTraits() noexcept {
    return kTraits;
}

} // namespace pmm::games
