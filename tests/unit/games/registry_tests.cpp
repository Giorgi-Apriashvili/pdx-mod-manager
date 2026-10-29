#include "core/catalog.hpp"
#include "games/dlc_load_adapter.hpp"
#include "games/eu4.hpp"
#include "games/game_adapter.hpp"
#include "games/stellaris.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace pmm;
using namespace pmm::games;
using test::u8path;

TEST_CASE("The registry lists the EU4 and Stellaris adapters in catalog order",
          "[games][registry]") {
    const auto list = adapters();
    REQUIRE(list.size() == 2);
    CHECK(list[0]->key() == "eu4");
    CHECK(list[1]->key() == "stellaris");
    for (const auto& adapter : list) {
        const GameCatalogEntry* entry = findGame(adapter->key());
        REQUIRE(entry != nullptr);
        CHECK(&adapter->catalog() == entry);
        CHECK(entry->adapterAvailable);
    }
    CHECK(findAdapter("stellaris") == list[1].get());
    CHECK(findAdapter("eu4") == list[0].get());
    CHECK(findAdapter("hoi4") == nullptr); // known game, no adapter
    CHECK(findAdapter("") == nullptr);
}

TEST_CASE("Game traits agree with the catalog", "[games][registry]") {
    for (const GameTraits* traits : {&stellarisTraits(), &eu4Traits()}) {
        const GameCatalogEntry* entry = findGame(traits->key);
        REQUIRE(entry != nullptr);
        CHECK(traits->steamAppId == entry->steamAppId);
        CHECK(traits->userDataFolder == entry->userDataFolder);
        CHECK(traits->launcherGameId == traits->key);
    }
    CHECK(stellarisTraits().rejectsEarlyWildcards);
    CHECK_FALSE(eu4Traits().rejectsEarlyWildcards);
}

TEST_CASE("The real discovery environment describes this process", "[games][registry]") {
    // Read-only: looks up standard folders and the Steam root.
    const DiscoveryEnvironment env = DiscoveryEnvironment::current();
    CHECK(env.platform == currentOsPlatform());
    CHECK_FALSE(env.home.empty());
    if (env.platform == OsPlatform::Linux)
        CHECK_FALSE(env.xdgDataHome.empty());
    else
        CHECK_FALSE(env.documentsDir.empty());
}

TEST_CASE("Workshop ids are recognised in content paths", "[games][registry]") {
    fs::path library;
    CHECK(workshopIdFromPath(u8path("D:/Steam Library/steamapps/workshop/content/281990/123"),
                             "281990", &library) == "123");
    CHECK(library == u8path("D:/Steam Library"));
    CHECK(workshopIdFromPath(u8path("/home/u/.local/share/Steam/SteamApps/Workshop/Content/281990/"
                                    "9/sub/file.zip"),
                             "281990") == "9");
    CHECK_FALSE(workshopIdFromPath(u8path("/x/steamapps/workshop/content/236850/123"), "281990"));
    CHECK_FALSE(workshopIdFromPath(u8path("/x/steamapps/workshop/content/281990/0123"), "281990"));
    CHECK_FALSE(workshopIdFromPath(u8path("/x/steamapps/workshop/content/281990"), "281990"));
    CHECK_FALSE(workshopIdFromPath(u8path("/x/mod/local"), "281990"));
    CHECK(descriptorReference(u8path("/a/b/ugc_1.mod")) == "mod/ugc_1.mod");
}
