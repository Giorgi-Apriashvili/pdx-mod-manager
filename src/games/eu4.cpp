#include "games/eu4.hpp"

#include <array>

namespace pmm::games {
namespace {

// Paradox launcher v2, as for Stellaris (unverified for EU4).
constexpr std::array<std::string_view, 2> kWindowsLauncher{"Paradox Launcher.exe", "dowser.exe"};
constexpr std::array<std::string_view, 2> kLinuxLauncher{"dowser", "paradox-launcher"};
constexpr std::array<std::string_view, 1> kMacLauncher{"Paradox Launcher"};
// launcher-settings.json of v1.37.5.0 asks for "-enabletelemetry"; the
// manager never opts the user into the game's telemetry.
constexpr std::array<std::string_view, 1> kDroppedArguments{"-enabletelemetry"};

constexpr GameTraits kTraits{
    .key = "eu4",
    .steamAppId = "236850",
    .userDataFolder = "Europa Universalis IV",
    .launcherGameId = "eu4",
    .windowsExecutable = "eu4.exe",
    .linuxExecutable = "eu4",
    .macExecutable = "eu4.app/Contents/MacOS/eu4",
    .windowsLauncherProcesses = kWindowsLauncher,
    .linuxLauncherProcesses = kLinuxLauncher,
    .macLauncherProcesses = kMacLauncher,
    // v1.37.5.0 ships an empty userdir.txt, and eu4.exe references the file.
    .userDirFile = "userdir.txt",
    .droppedArguments = kDroppedArguments,
    // No evidence of such a rule for EU4: its leftover error.log has no
    // "Invalid supported_version" lines.
    .rejectsEarlyWildcards = false,
    .loadOrderSummary = "dlc_load.json enabled_mods, index 0 first; file override precedence: "
                        "see docs/load-order.md",
    .loadOrderEvidence = "docs/load-order.md (EU4: unverified, game not installed)",
    .logComponent = "games.eu4",
};

} // namespace

const GameTraits& eu4Traits() noexcept {
    return kTraits;
}

} // namespace pmm::games
