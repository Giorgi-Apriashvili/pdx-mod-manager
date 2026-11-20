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
    // victoria3.exe has "Invalid supported_version in {}", but which patterns
    // it rejects is not known yet; a mismatch does not stop a mod loading.
    .rejectsEarlyWildcards = false,
    .loadOrderSummary = "content_load.json enabledMods, index 0 first; when two enabled mods "
                        "contain the same file, the one later in the list wins",
    .loadOrderEvidence = "documented, not yet verified in the game: reverse engineering of "
                         "Victoria 3 1.15.1 (each mounted mod is searched before the earlier "
                         "ones); see docs/games/vic3.md",
    .loadOrderVerifiedInGame = false,
    // Shown above a Victoria 3 playset until a probe-mod run settles it.
    .loadOrderUserNote = "It is not yet verified that Victoria 3 follows this order when two "
                         "mods change the same file.",
    .logComponent = "games.vic3",
    .supportedVersionKey = "supported_game_version",
    .extraGameProcesses = kExtraGameProcesses,
};

} // namespace

const GameTraits& vic3Traits() noexcept {
    return kTraits;
}

} // namespace pmm::games
