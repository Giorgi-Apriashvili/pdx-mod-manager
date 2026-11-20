#include "games/vic3.hpp"

#include <array>

namespace pmm::games {
namespace {

// Paradox launcher v2, as for the other games; Victoria 3 ships dowser.exe in
// <install>/launcher/. The other names are unverified for Victoria 3.
constexpr std::array<std::string_view, 2> kLauncher{"Paradox Launcher.exe", "dowser.exe"};
// binaries/victoria3_win_console.exe is a console wrapper that starts
// victoria3.exe with its output piped: while it runs, so does the game.
constexpr std::array<std::string_view, 1> kExtraGameProcesses{"victoria3_win_console.exe"};
// Not requested by 1.15.2's launcher-settings.json; dropped all the same.
constexpr std::array<std::string_view, 1> kDroppedArguments{"-enabletelemetry"};

constexpr GameTraits kTraits{
    .key = "vic3",
    .steamAppId = "529340",
    .userDataFolder = "Victoria 3",
    // The launcher's id differs from the catalog key.
    .launcherGameId = "victoria3",
    // The installation folder holds no executable; launcher-settings.json
    // names "../binaries/victoria3.exe" from <install>/launcher/.
    .executable = "binaries/victoria3.exe",
    .launcherProcesses = kLauncher,
    // By analogy with Crusader Kings III (the same Jomini engine): none ships
    // with 1.15.2, and victoria3.exe mentions userdir.txt without saying
    // where it reads it from. Unverified.
    .userDirFile = "game/userdir.txt",
    .droppedArguments = kDroppedArguments,
    // Probe run on 1.15.2 (2026-11-21): "1.*", "1.15" and "1.15.*" were
    // logged as matching 1.15.2 and none as invalid, but "*" as not matching;
    // every probe loaded, as a mismatch does not stop a mod loading.
    .rejectsEarlyWildcards = false,
    .loneWildcardMismatches = true,
    .loadOrderSummary = "content_load.json enabledMods, index 0 first; when two enabled mods "
                        "contain the same file, the one later in the list wins",
    .loadOrderEvidence = "docs/evidence/2026-11-21-vic3-load-order.md (probe mods, Victoria 3 "
                         "1.15.2, Windows 11)",
    .loadOrderVerifiedInGame = true,
    // The playset order is the order the game applies to whole files; which
    // definition of a database entry wins follows the game's own rules
    // (docs/games/vic3.md#load-order).
    .loadOrderUserNote = {},
    .logComponent = "games.vic3",
    .supportedVersionKey = "supported_game_version",
    .extraGameProcesses = kExtraGameProcesses,
};

} // namespace

const GameTraits& vic3Traits() noexcept {
    return kTraits;
}

} // namespace pmm::games
