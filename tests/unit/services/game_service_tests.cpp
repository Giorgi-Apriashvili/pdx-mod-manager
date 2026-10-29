#include "persistence/installation_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "services/app_paths.hpp"
#include "services/game_service.hpp"
#include "services/playset_service.hpp"
#include "sharing/manifest.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>

using namespace pmm;
using namespace pmm::services;
using namespace pmm::services::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

namespace {

// string_view: a std::string parameter would bind string literals to a temporary,
// which GCC flags as a possibly dangling reference (-Wdangling-reference).
const GameState& stateFor(const std::vector<GameState>& states, std::string_view key) {
    const auto it = std::find_if(states.begin(), states.end(),
                                 [&](const GameState& s) { return s.key == key; });
    REQUIRE(it != states.end());
    return *it;
}

bool hasMod(const std::vector<InstalledMod>& mods, const std::string& source,
            const std::string& id) {
    return std::any_of(mods.begin(), mods.end(), [&](const InstalledMod& m) {
        return m.identity.source == source && m.identity.id == id;
    });
}

} // namespace

TEST_CASE("Profile paths keep every application file under one folder", "[services][paths]") {
    TempDir temp;
    const fs::path profile = temp.path() / u8("clean profile ü");
    const AppPaths paths = AppPaths::fromProfileDir(profile);
    CHECK(paths.dataDir == profile);
    CHECK(paths.databaseFile == profile / "state.sqlite");
    CHECK(paths.transactionsDir.parent_path() == profile);
    CHECK(paths.logsDir.parent_path() == profile);
    CHECK(paths.cacheDir.parent_path() == profile);
    requireOk(paths.createDirectories());
    CHECK(fs::is_directory(paths.transactionsDir));
    CHECK(fs::is_directory(paths.logsDir));
    CHECK(fs::is_directory(paths.cacheDir));

    const AppPaths standard = AppPaths::standard();
    CHECK(standard.databaseFile.filename() == "state.sqlite");
    CHECK(standard.transactionsDir.parent_path() == standard.dataDir);
}

TEST_CASE("Refreshing discovers, persists and scans supported games only", "[services][games]") {
    World w;
    w.installFixtureMods();
    w.adapter.loadOrderNote = "Order is not applied as written.";
    GameService games(w.ctx);

    const std::vector<GameState> states = games.refreshAll();
    REQUIRE(states.size() == gameCatalog().size());
    const GameState& stellaris = stateFor(states, kGame);
    CHECK(stellaris.supported);
    CHECK(stellaris.loadOrderNote == "Order is not applied as written."); // from the adapter
    REQUIRE(stellaris.installation);
    CHECK(stellaris.installation->installPath == w.install);
    CHECK_FALSE(stellaris.installation->manuallyConfigured);
    CHECK(stellaris.capabilities.directLaunch);
    CHECK_FALSE(stellaris.scanError);
    CHECK(stellaris.inventory.size() == 4);
    CHECK(hasMod(stellaris.inventory, source::SteamWorkshop, "2000000001"));
    CHECK(hasMod(stellaris.inventory, source::Local, "example_local_tweaks"));

    // EU4 has a catalog adapter but none was injected; HOI4 is not supported yet.
    CHECK_FALSE(stateFor(states, "eu4").supported);
    CHECK_FALSE(stateFor(states, "eu4").installation);
    CHECK_FALSE(stateFor(states, "hoi4").supported);

    persistence::InstallationRepository installations(*w.db);
    REQUIRE(require(installations.get(kGame)));
    const GameState cached = require(games.cachedState(kGame));
    REQUIRE(cached.installation);
    CHECK(cached.installation->installPath == w.install);
    CHECK(cached.inventory.size() == 4);

    SECTION("an uninstalled game drops the stale installation and inventory") {
        w.adapter.discovered.clear();
        const GameState gone = require(games.refreshGame(kGame));
        CHECK_FALSE(gone.installation);
        CHECK(gone.inventory.empty());
        CHECK_FALSE(require(installations.get(kGame)));
        CHECK(require(games.cachedInventory(kGame)).empty());
    }

    SECTION("a failed scan keeps the cached inventory and reports the problem") {
        w.adapter.scanError = makeError(ErrorCode::IoError, "disk unplugged");
        const GameState failed = require(games.refreshGame(kGame));
        REQUIRE(failed.scanError);
        CHECK_THAT(failed.scanError->message, ContainsSubstring("disk unplugged"));
        CHECK(failed.installation);
        CHECK(failed.inventory.size() == 4);
        CHECK(require(games.cachedInventory(kGame)).size() == 4);
    }

    SECTION("rescanMods picks up newly installed content without rediscovery") {
        w.addWorkshopMod("2000000099", "Fresh Download");
        w.adapter.discovered.clear(); // would drop the installation if rediscovered
        const GameState rescanned = require(games.rescanMods(kGame));
        REQUIRE(rescanned.installation);
        CHECK(hasMod(rescanned.inventory, source::SteamWorkshop, "2000000099"));
    }
}

TEST_CASE("Manual locations are validated, stored, re-inspected and clearable",
          "[services][games]") {
    World w;
    w.installFixtureMods();
    GameService games(w.ctx);
    persistence::InstallationRepository installations(*w.db);

    // A second copy of the game somewhere else, chosen by the user.
    const fs::path manualInstall = w.root / u8("Other Drive ö") / "Stellaris";
    const fs::path manualUserData = w.root / u8("Other Drive ö") / u8("user data");
    writeText(manualInstall / kExeName, "placeholder");
    fs::create_directories(manualUserData / "mod");

    const auto rejected = games.setManualLocation(kGame, w.root / "empty folder", {});
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == ErrorCode::ValidationFailed);
    CHECK_FALSE(require(installations.get(kGame)));

    const GameState manual = require(games.setManualLocation(kGame, manualInstall, manualUserData));
    REQUIRE(manual.installation);
    CHECK(manual.installation->manuallyConfigured);
    CHECK(manual.installation->installPath == manualInstall);
    CHECK(manual.inventory.empty()); // nothing in the manual user-data folder
    auto stored = require(installations.get(kGame));
    REQUIRE(stored);
    CHECK(stored->manuallyConfigured);

    // Refresh keeps the manual installation although discovery finds another.
    const GameState refreshed = require(games.refreshGame(kGame));
    REQUIRE(refreshed.installation);
    CHECK(refreshed.installation->installPath == manualInstall);

    // The manual folder disappears: reported, not silently replaced.
    fs::remove(manualInstall / kExeName);
    const GameState broken = require(games.refreshGame(kGame));
    CHECK_FALSE(broken.installation);
    REQUIRE(broken.scanError);
    CHECK_THAT(broken.scanError->message, ContainsSubstring("manually selected"));
    CHECK(require(installations.get(kGame))->manuallyConfigured);

    const GameState cleared = require(games.clearManualLocation(kGame));
    REQUIRE(cleared.installation);
    CHECK(cleared.installation->installPath == w.install);
    CHECK_FALSE(cleared.installation->manuallyConfigured);
    CHECK(cleared.inventory.size() == 4);
}

TEST_CASE("Rescans drop cached local fingerprints that the folder's time cannot vouch for",
          "[services][games]") {
    World w;
    w.installFixtureMods();
    GameService games(w.ctx);
    std::vector<InstalledMod> mods = w.refresh().inventory;
    for (InstalledMod& mod : mods) {
        if (mod.identity.source == source::Local)
            mod.fingerprint = "sha256:" + std::string(64, 'b');
    }
    persistence::InventoryRepository inventory(*w.db);
    requireOk(inventory.replaceForGame(kGame, mods));

    // The folder looks unchanged, but a file inside it may have been edited.
    const GameState again = require(games.rescanMods(kGame));
    const auto local =
        std::find_if(again.inventory.begin(), again.inventory.end(),
                     [](const InstalledMod& m) { return m.identity.source == source::Local; });
    REQUIRE(local != again.inventory.end());
    CHECK(local->fingerprint.empty());
    const auto stored = require(inventory.find(local->identity));
    REQUIRE(stored);
    CHECK(stored->fingerprint.empty());
}

TEST_CASE("The game's current configuration becomes a new active playset", "[services][games]") {
    World w;
    w.installFixtureMods();
    GameService games(w.ctx);

    const auto missing = games.importCurrentConfiguration(kGame);
    REQUIRE_FALSE(missing); // not refreshed yet: no installation known
    CHECK(missing.error().code == ErrorCode::NotFound);

    w.refresh();
    w.adapter.currentConfiguration = {
        workshopEntry("2000000002", ""),
        workshopEntry("2000000001", ""),
        localEntry("mod/unresolved.mod", "Unresolved"),
        workshopEntry("2000000002", ""), // listed twice by the game file
    };
    const std::int64_t id = require(games.importCurrentConfiguration(kGame));
    PlaysetService playsets(w.ctx);
    const Playset playset = require(playsets.playset(id));
    CHECK(playset.name == "Current game configuration");
    REQUIRE(playset.entries.size() == 3);
    CHECK(playset.entries[0].identity.id == "2000000002");
    CHECK(playset.entries[0].nameHint == "Example Compatibility Patch");
    CHECK(playset.entries[1].identity.id == "2000000001");
    CHECK(playset.entries[2].identity.id == "mod/unresolved.mod");
    CHECK(require(playsets.activePlaysetId(kGame)) == id);
    CHECK(require(playsets.lastGameKey()) == std::optional<GameKey>(kGame));

    const std::int64_t second = require(games.importCurrentConfiguration(kGame));
    CHECK(require(playsets.playset(second)).name == "Current game configuration (2)");
}

TEST_CASE("Names for the current configuration are validated and stay within the length limit",
          "[services][games]") {
    World w;
    w.installFixtureMods();
    w.refresh();
    w.adapter.currentConfiguration = {workshopEntry("2000000001", "")};
    GameService games(w.ctx);
    PlaysetService playsets(w.ctx);
    const std::string name(sharing::kMaxPlaysetNameBytes, 'c');

    CHECK(require(playsets.playset(require(games.importCurrentConfiguration(kGame, name)))).name ==
          name);
    const std::string again =
        require(playsets.playset(require(games.importCurrentConfiguration(kGame, name)))).name;
    CHECK(again.size() <= sharing::kMaxPlaysetNameBytes);
    CHECK_THAT(again, EndsWith(" (2)"));

    const std::size_t before = require(playsets.listPlaysets(kGame)).size();
    const auto tooLong = games.importCurrentConfiguration(kGame, name + "c");
    REQUIRE_FALSE(tooLong);
    CHECK(tooLong.error().code == ErrorCode::InvalidArgument);
    const auto control = games.importCurrentConfiguration(kGame, "line\nbreak");
    REQUIRE_FALSE(control);
    CHECK(control.error().code == ErrorCode::InvalidArgument);
    CHECK(require(playsets.listPlaysets(kGame)).size() == before);
}
