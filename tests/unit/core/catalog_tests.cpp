#include "core/catalog.hpp"
#include "core/catalog_testing.hpp"
#include "core/model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

TEST_CASE("Catalog exposes CK3, EU4, Hearts of Iron IV, Stellaris and Victoria 3 as supported "
          "games",
          "[core][catalog]") {
    const auto* ck3 = pmm::findGame("ck3");
    const auto* eu4 = pmm::findGame("eu4");
    const auto* hoi4 = pmm::findGame("hoi4");
    const auto* stellaris = pmm::findGame("stellaris");
    const auto* vic3 = pmm::findGame("vic3");
    REQUIRE(ck3 != nullptr);
    REQUIRE(eu4 != nullptr);
    REQUIRE(hoi4 != nullptr);
    REQUIRE(stellaris != nullptr);
    REQUIRE(vic3 != nullptr);
    CHECK(ck3->adapterAvailable);
    CHECK(eu4->adapterAvailable);
    CHECK(hoi4->adapterAvailable);
    CHECK(stellaris->adapterAvailable);
    CHECK(vic3->adapterAvailable);
    CHECK(ck3->steamAppId == "1158310");
    CHECK(eu4->steamAppId == "236850");
    CHECK(hoi4->steamAppId == "394360");
    CHECK(stellaris->steamAppId == "281990");
    CHECK(vic3->steamAppId == "529340");
    CHECK(ck3->displayName == "Crusader Kings III");
    CHECK(ck3->userDataFolder == "Crusader Kings III");
    CHECK(hoi4->displayName == "Hearts of Iron IV");
    CHECK(hoi4->userDataFolder == "Hearts of Iron IV");
    CHECK(vic3->displayName == "Victoria 3");
    CHECK(vic3->userDataFolder == "Victoria 3");
    for (const char* key : {"ck3", "hoi4", "vic3"}) {
        CHECK(pmm::isSourceValidForGame(key, pmm::source::SteamWorkshop));
        CHECK(pmm::isSourceValidForGame(key, pmm::source::Local));
    }
}

TEST_CASE("Catalog lists supported games first, alphabetically", "[core][catalog]") {
    // A fresh profile opens the first installed supported game in this order.
    std::vector<std::string_view> keys;
    for (const pmm::GameCatalogEntry& entry : pmm::gameCatalog())
        keys.push_back(entry.key);
    CHECK(keys == std::vector<std::string_view>{"ck3", "eu4", "hoi4", "stellaris", "vic3"});
    const auto supportedFirst = [] {
        bool unsupportedSeen = false;
        for (const pmm::GameCatalogEntry& entry : pmm::gameCatalog()) {
            CHECK_FALSE((unsupportedSeen && entry.adapterAvailable));
            unsupportedSeen = unsupportedSeen || !entry.adapterAvailable;
        }
        return unsupportedSeen;
    };
    // Every game in the catalog is supported at present.
    CHECK_FALSE(supportedFirst());
    // A game the catalog knows without supporting it comes after them.
    const pmm::testing::ScopedGameCatalog catalog(pmm::testing::catalogWithUnsupportedGame());
    CHECK(supportedFirst());
}

TEST_CASE("Catalog lists a known game without claiming support", "[core][catalog]") {
    const std::size_t realSize = pmm::gameCatalog().size();
    {
        const pmm::testing::ScopedGameCatalog catalog(pmm::testing::catalogWithUnsupportedGame());
        REQUIRE(pmm::gameCatalog().size() == realSize + 1);
        const auto* game = pmm::findGame(pmm::testing::kUnsupportedGameKey);
        REQUIRE(game != nullptr);
        CHECK(game == &pmm::gameCatalog().back());
        CHECK_FALSE(game->adapterAvailable);
        CHECK(game->displayName == "Example Unsupported Game");
        CHECK(pmm::isSourceValidForGame(game->key, pmm::source::SteamWorkshop));
        CHECK(pmm::isSourceValidForGame(game->key, pmm::source::Local));
        // The real entries are all still there, in the same order.
        for (std::size_t i = 0; i < realSize; ++i) {
            const pmm::GameCatalogEntry& entry = pmm::gameCatalog()[i];
            REQUIRE(pmm::findGame(entry.key) == &entry);
        }
        CHECK(pmm::findGame("stellaris")->adapterAvailable);
    }
    // The guard restores the real catalog.
    CHECK(pmm::gameCatalog().size() == realSize);
    CHECK(pmm::findGame(pmm::testing::kUnsupportedGameKey) == nullptr);
    CHECK_FALSE(pmm::isSourceValidForGame(pmm::testing::kUnsupportedGameKey, pmm::source::Local));
    CHECK(pmm::findGame("not-a-game") == nullptr);
}

TEST_CASE("Only Victoria 3 requires the mods a mod names; Stellaris and CK3 keep their order",
          "[core][catalog]") {
    using pmm::DependencyMeaning;
    CHECK(pmm::dependencyMeaningOf("vic3") == DependencyMeaning::Required);
    // The list order decides which mod wins a shared file.
    for (const char* key : {"ck3", "stellaris"})
        CHECK(pmm::dependencyMeaningOf(key) == DependencyMeaning::LoadAfterInList);
    // EU4 ignores the list order; Hearts of Iron IV orders the mods itself.
    for (const char* key : {"eu4", "hoi4"})
        CHECK(pmm::dependencyMeaningOf(key) == DependencyMeaning::LoadAfter);
    CHECK(pmm::dependencyMeaningOf("not-a-game") == DependencyMeaning::Required);
    for (const pmm::GameCatalogEntry& entry : pmm::gameCatalog()) {
        CHECK(entry.dependencyMeaning == pmm::dependencyMeaningOf(entry.key));
        // Only Hearts of Iron IV's documentation says what the names do.
        CHECK(entry.dependencyLoadAfterDocumented == (entry.key == "hoi4"));
    }
    // A known game without an adapter checks them as required.
    const pmm::testing::ScopedGameCatalog catalog(pmm::testing::catalogWithUnsupportedGame());
    CHECK(pmm::dependencyMeaningOf(pmm::testing::kUnsupportedGameKey) ==
          DependencyMeaning::Required);
    CHECK_FALSE(pmm::findGame(pmm::testing::kUnsupportedGameKey)->dependencyLoadAfterDocumented);
    CHECK(pmm::dependencyMeaningOf("hoi4") == DependencyMeaning::LoadAfter);
}

TEST_CASE("Provider/game combinations are validated", "[core][catalog]") {
    CHECK(pmm::isSourceValidForGame("stellaris", pmm::source::SteamWorkshop));
    CHECK(pmm::isSourceValidForGame("eu4", pmm::source::Local));
    CHECK_FALSE(pmm::isSourceValidForGame("stellaris", "paradox-mods"));
    CHECK_FALSE(pmm::isSourceValidForGame("unknown", pmm::source::Local));
}

TEST_CASE("Acquisition states round-trip through their names", "[core][model]") {
    using pmm::AcquisitionState;
    for (auto s :
         {AcquisitionState::Pending, AcquisitionState::Subscribing, AcquisitionState::Subscribed,
          AcquisitionState::DownloadRequested, AcquisitionState::Downloading,
          AcquisitionState::Installed, AcquisitionState::Failed, AcquisitionState::Unavailable,
          AcquisitionState::NeedsUserAction, AcquisitionState::Cancelled}) {
        const auto parsed = pmm::acquisitionStateFromString(pmm::toString(s));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == s);
    }
    CHECK_FALSE(pmm::acquisitionStateFromString("bogus").has_value());
}
