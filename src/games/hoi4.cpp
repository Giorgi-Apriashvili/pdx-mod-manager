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
    // error.log reports "Invalid supported_version" for each "1.*.*"
    // registration, and not for "1.19.*", "1.17.*", "1.19.4.*" or the
    // four-part forms without a wildcard ("1.19.4.2"): 1.19.5.0 started by the
    // launcher, and 1.20.0.0 started directly in the probe run of 2026-11-21,
    // which reads every mod/*.mod at start-up, enabled or not. That run's
    // disabled probe with "1.20" was not reported either, so a two-part
    // pattern without a wildcard is valid (whether 1.20.0.0 counts it as a
    // match is not logged). "*", "1.*" and "1.20.0" are untested.
    .rejectsEarlyWildcards = true,
    // Probe mods in 1.20.0.0 (2026-11-21): swapping two mods in enabled_mods
    // and renaming one (name=) did not change which one won a shared file;
    // renaming its registration file (mod/<file>.mod) did, the later file
    // name winning. As for EU4, the playset order is still written (it is
    // what the launcher writes, and share codes carry it), and registrations
    // are never renamed to force an order.
    .loadOrderSummary = "dlc_load.json enabled_mods lists the playset order, but Hearts of Iron "
                        "IV does not use it for file overrides: when two enabled mods contain the "
                        "same file, the mod whose registration file name (mod/<file>.mod) sorts "
                        "last wins",
    .loadOrderEvidence = "docs/evidence/2026-11-21-hoi4-load-order.md (probe mods, Hearts of Iron "
                         "IV 1.20.0.0, Windows 11)",
    .loadOrderVerifiedInGame = true,
    // A Workshop mod's registration is mod/ugc_<id>.mod; that the same rule
    // orders those (as strings, so ugc_9... after ugc_1...) is inferred.
    .loadOrderUserNote = "Hearts of Iron IV does not use this order when two mods change the same "
                         "file: the mod whose .mod file in the game's mod folder comes last "
                         "alphabetically wins (ugc_<ID>.mod for a Workshop mod). The order is "
                         "still saved, shared and written for the game.",
    .logComponent = "games.hoi4",
    // hoi4.exe: a game-folder userdir.txt is "old style" and "Overridden by
    // the gameDataPath field in launcher-settings.json", and users should
    // "Update launcher-settings.json to point to the intended custom user
    // directory", so an absolute gameDataPath is taken too. Inferred from
    // these strings; not tested in the game.
    .gameDataPathOverridesUserDirFile = true,
};

} // namespace

const GameTraits& hoi4Traits() noexcept {
    return kTraits;
}

} // namespace pmm::games
