#include "games/hoi4.hpp"

#include <array>

namespace pmm::games {
namespace {

// Paradox launcher v2, as for the other games; dowser.exe ships in the
// installation folder, as for Stellaris.
constexpr std::array<std::string_view, 2> kLauncher{"Paradox Launcher.exe", "dowser.exe"};
// Not requested by 1.20.0.0's launcher-settings.json (hoi4.exe has no such
// switch); dropped all the same.
constexpr std::array<std::string_view, 1> kDroppedArguments{"-enabletelemetry"};

constexpr GameTraits kTraits{
    .key = "hoi4",
    .steamAppId = "394360",
    .userDataFolder = "Hearts of Iron IV",
    .launcherGameId = "hoi4",
    // In the installation folder, where launcher-settings.json names
    // "./hoi4.exe".
    .executable = "hoi4.exe",
    .launcherProcesses = kLauncher,
    // hoi4.exe references userdir.txt in the game folder (none ships with
    // 1.20.0.0), and calls it overridden by gameDataPath: see
    // gameDataPathOverridesUserDirFile below.
    .userDirFile = "userdir.txt",
    .droppedArguments = kDroppedArguments,
    // error.log (1.19.5.0, started by the launcher) reports "Invalid
    // supported_version" for each "1.*.*" registration, and not for "1.19.*",
    // "1.17.*", "1.19.4.*" or the 3- and 4-part plain forms. "*", "1.*" and
    // "1.20" are untested.
    .rejectsEarlyWildcards = true,
    .loadOrderSummary = "dlc_load.json enabled_mods, index 0 first; which mod wins when two "
                        "enabled mods contain the same file is not verified for Hearts of Iron IV",
    .loadOrderEvidence = "docs/games/hoi4.md (read-only; precedence not probed)",
    .loadOrderVerifiedInGame = false,
    // Until a probe run shows the rule: then removed, or replaced as for EU4.
    // The HOI4 wiki orders mods by the names of their mod/*.mod files, and
    // the launcher turns its own load-order controls off for this game.
    .loadOrderUserNote = "Hearts of Iron IV may not follow this order when two mods change the "
                         "same file; this is not verified yet.",
    .logComponent = "games.hoi4",
    // hoi4.exe: a game-folder userdir.txt is "old style" and "Overridden by
    // the gameDataPath field in launcher-settings.json". Inferred from these
    // strings; not tested in the game.
    .gameDataPathOverridesUserDirFile = true,
};

} // namespace

const GameTraits& hoi4Traits() noexcept {
    return kTraits;
}

} // namespace pmm::games
