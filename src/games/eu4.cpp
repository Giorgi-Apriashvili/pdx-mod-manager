#include "games/eu4.hpp"

#include <array>

namespace pmm::games {
namespace {

// Paradox launcher v2, as for Stellaris: dowser.exe ships in the v1.37.5.0
// install folder too; the other names are unverified for EU4.
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
    // No evidence of such a rule for EU4: its error.log files (from before the
    // reinstall) have no "Invalid supported_version" lines.
    .rejectsEarlyWildcards = false,
    // Probe mods in EU4 1.37.5 (2026-10-30): swapping two mods in
    // enabled_mods did not change which one won a shared file; renaming one
    // did. The playset order is still written (it is what the launcher writes).
    .loadOrderSummary = "dlc_load.json enabled_mods lists the playset order, but EU4 does not use "
                        "it for file overrides: when two enabled mods contain the same file, the "
                        "mod whose name sorts first alphabetically wins",
    .loadOrderEvidence = "docs/evidence/live-validation-2026-10-30.md (probe mods, EU4 1.37.5, "
                         "Windows 11)",
    .loadOrderVerifiedInGame = true,
    .logComponent = "games.eu4",
};

} // namespace

const GameTraits& eu4Traits() noexcept {
    return kTraits;
}

} // namespace pmm::games
