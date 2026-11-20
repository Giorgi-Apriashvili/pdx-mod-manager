#include "core/catalog.hpp"
#include "core/catalog_testing.hpp"
#include "games/ck3.hpp"
#include "games/dlc_load_adapter.hpp"
#include "games/eu4.hpp"
#include "games/game_adapter.hpp"
#include "games/hoi4.hpp"
#include "games/stellaris.hpp"
#include "games/vic3.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

using namespace pmm;
using namespace pmm::games;
using test::u8path;

TEST_CASE("The registry lists the CK3, EU4, Hearts of Iron IV, Stellaris and Victoria 3 adapters "
          "in catalog order",
          "[games][registry]") {
    const auto list = adapters();
    REQUIRE(list.size() == 5);
    CHECK(list[0]->key() == "ck3");
    CHECK(list[1]->key() == "eu4");
    CHECK(list[2]->key() == "hoi4");
    CHECK(list[3]->key() == "stellaris");
    CHECK(list[4]->key() == "vic3");
    for (const auto& adapter : list) {
        const GameCatalogEntry* entry = findGame(adapter->key());
        REQUIRE(entry != nullptr);
        CHECK(&adapter->catalog() == entry);
        CHECK(entry->adapterAvailable);
    }
    CHECK(findAdapter("ck3") == list[0].get());
    CHECK(findAdapter("eu4") == list[1].get());
    CHECK(findAdapter("hoi4") == list[2].get());
    CHECK(findAdapter("stellaris") == list[3].get());
    CHECK(findAdapter("vic3") == list[4].get());
    CHECK(dynamic_cast<const DlcLoadJsonAdapter*>(list[2].get()) != nullptr);
    CHECK(dynamic_cast<const ContentLoadJsonAdapter*>(list[4].get()) != nullptr);
    // Known games the catalog does not support have no adapter. The real
    // catalog has none at present, so a test-only one is added; adapters()
    // was built above, before the swap.
    {
        const pmm::testing::ScopedGameCatalog catalog(pmm::testing::catalogWithUnsupportedGame());
        int unsupported = 0;
        for (const GameCatalogEntry& entry : gameCatalog()) {
            if (entry.adapterAvailable)
                continue;
            ++unsupported;
            CHECK(findAdapter(entry.key) == nullptr);
        }
        CHECK(unsupported >= 1);
        CHECK(findAdapter(pmm::testing::kUnsupportedGameKey) == nullptr);
    }
    CHECK(findAdapter("not-a-game") == nullptr);
    CHECK(findAdapter("") == nullptr);

    // Every game the catalog calls supported has its adapter, in the same order.
    std::vector<std::string_view> supported;
    for (const GameCatalogEntry& entry : gameCatalog()) {
        if (entry.adapterAvailable)
            supported.push_back(entry.key);
    }
    REQUIRE(supported.size() == list.size());
    for (std::size_t i = 0; i < list.size(); ++i)
        CHECK(list[i]->key() == supported[i]);
}

TEST_CASE("Game traits agree with the catalog", "[games][registry]") {
    for (const GameTraits* traits :
         {&ck3Traits(), &stellarisTraits(), &eu4Traits(), &hoi4Traits(), &vic3Traits()}) {
        const GameCatalogEntry* entry = findGame(traits->key);
        REQUIRE(entry != nullptr);
        CHECK(traits->steamAppId == entry->steamAppId);
        CHECK(traits->userDataFolder == entry->userDataFolder);
        // The launcher's own id, the catalog key except for Victoria 3.
        CHECK(traits->launcherGameId ==
              (traits->key == "vic3" ? std::string_view("victoria3") : traits->key));
        CHECK(traits->supportedVersionKey ==
              (traits->key == "vic3" ? "supported_game_version" : "supported_version"));
        CHECK(traits->extraGameProcesses.size() == (traits->key == "vic3" ? 1u : 0u));
        // Only Hearts of Iron IV reads gameDataPath before its userdir.txt.
        CHECK(traits->gameDataPathOverridesUserDirFile == (traits->key == "hoi4"));
        // Every game's rule for a shared file was measured with probe mods.
        // EU4 and Hearts of Iron IV do not follow the list, and a note says so.
        CHECK(traits->loadOrderVerifiedInGame);
        CHECK(traits->loadOrderUserNote.empty() == (traits->key != "eu4" && traits->key != "hoi4"));
    }
    CHECK(ck3Traits().rejectsEarlyWildcards);
    CHECK(stellarisTraits().rejectsEarlyWildcards);
    CHECK_FALSE(eu4Traits().rejectsEarlyWildcards);
    CHECK(ck3Traits().executable == "binaries/ck3.exe");
    CHECK(ck3Traits().userDirFile == "game/userdir.txt");
    CHECK(ck3Traits().loadOrderVerifiedInGame);
    CHECK_FALSE(vic3Traits().rejectsEarlyWildcards);
    CHECK(vic3Traits().loneWildcardMismatches);
    CHECK_FALSE(ck3Traits().loneWildcardMismatches);
    CHECK_FALSE(stellarisTraits().loneWildcardMismatches);
    CHECK_FALSE(eu4Traits().loneWildcardMismatches);
    CHECK(vic3Traits().executable == "binaries/victoria3.exe");
    CHECK(vic3Traits().userDirFile == "game/userdir.txt");
    CHECK(vic3Traits().loadOrderVerifiedInGame);
    CHECK(vic3Traits().loadOrderUserNote.empty());
    // "1.*.*" is reported as invalid by 1.19.5.0 and by 1.20.0.0.
    CHECK(hoi4Traits().rejectsEarlyWildcards);
    // A lone "*" is untested for Hearts of Iron IV (docs/games/hoi4.md).
    CHECK_FALSE(hoi4Traits().loneWildcardMismatches);
    CHECK(hoi4Traits().executable == "hoi4.exe");
    CHECK(hoi4Traits().userDirFile == "userdir.txt");
    CHECK(hoi4Traits().droppedArguments.size() == 1);
}

TEST_CASE("The real discovery environment describes this process", "[games][registry]") {
    // Read-only: looks up standard folders and the Steam root.
    const DiscoveryEnvironment env = DiscoveryEnvironment::current();
    CHECK_FALSE(env.home.empty());
    CHECK_FALSE(env.documentsDir.empty());
}

TEST_CASE("Workshop ids are recognised in content paths", "[games][registry]") {
    fs::path library;
    CHECK(workshopIdFromPath(u8path("D:/Steam Library/steamapps/workshop/content/281990/123"),
                             "281990", &library) == "123");
    CHECK(library == u8path("D:/Steam Library"));
    CHECK(workshopIdFromPath(u8path("C:/Program Files (x86)/Steam/SteamApps/Workshop/Content/"
                                    "281990/9/sub/file.zip"),
                             "281990") == "9");
    CHECK_FALSE(workshopIdFromPath(u8path("/x/steamapps/workshop/content/236850/123"), "281990"));
    CHECK_FALSE(workshopIdFromPath(u8path("/x/steamapps/workshop/content/281990/0123"), "281990"));
    CHECK_FALSE(workshopIdFromPath(u8path("/x/steamapps/workshop/content/281990"), "281990"));
    CHECK_FALSE(workshopIdFromPath(u8path("/x/mod/local"), "281990"));
    CHECK(descriptorReference(u8path("/a/b/ugc_1.mod")) == "mod/ugc_1.mod");
    // Victoria 3's app id, as content_load.json names its Workshop folders.
    CHECK(workshopIdFromPath(u8path("C:\\Program Files (x86)\\Steam\\steamapps\\workshop\\"
                                    "content\\529340\\5000000001"),
                             "529340") == "5000000001");
}
