#include "games/ck3.hpp"

#include <array>

namespace pmm::games {
namespace {

// Paradox launcher v2, as for Stellaris and EU4; CK3 ships dowser.exe in
// <install>/launcher/. The other names are unverified for CK3.
constexpr std::array<std::string_view, 2> kLauncher{"Paradox Launcher.exe", "dowser.exe"};
// Not requested by 1.20.1.2's launcher-settings.json; dropped all the same.
constexpr std::array<std::string_view, 1> kDroppedArguments{"-enabletelemetry"};

constexpr GameTraits kTraits{
    .key = "ck3",
    .steamAppId = "1158310",
    .userDataFolder = "Crusader Kings III",
    .launcherGameId = "ck3",
    // The installation folder holds no executable; launcher-settings.json
    // names "../binaries/ck3.exe" from <install>/launcher/.
    .executable = "binaries/ck3.exe",
    .launcherProcesses = kLauncher,
    // Where the CK3 wiki documents it (none ships with 1.20.1.2; ck3.exe
    // references userdir.txt). Whether the game honours it is unverified.
    .userDirFile = "game/userdir.txt",
    .droppedArguments = kDroppedArguments,
    // error.log (1.19 build) reports "Invalid supported_version" for "1.*", and
    // not for "1.19.*", "1.18.*", "1.16.*", "1.19.0.6" or "1.13.0.3".
    .rejectsEarlyWildcards = true,
    .loadOrderSummary = "dlc_load.json enabled_mods, index 0 first; which mod wins when two "
                        "enabled mods contain the same file is not verified for Crusader Kings III",
    .loadOrderEvidence = "docs/games/ck3.md (read-only: debug.log mounts the mods in enabled_mods "
                         "order; precedence not probed)",
    .loadOrderVerifiedInGame = false,
    // Until the probe run: then removed, or replaced as for EU4.
    .loadOrderUserNote = "It is not yet verified that Crusader Kings III follows this order when "
                         "two mods change the same file.",
    .logComponent = "games.ck3",
};

} // namespace

const GameTraits& ck3Traits() noexcept {
    return kTraits;
}

} // namespace pmm::games
