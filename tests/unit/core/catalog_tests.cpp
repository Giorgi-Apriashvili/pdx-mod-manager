#include "core/catalog.hpp"
#include "core/model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>
#include <vector>

TEST_CASE("Catalog exposes CK3, EU4 and Stellaris as supported games", "[core][catalog]") {
    const auto* ck3 = pmm::findGame("ck3");
    const auto* eu4 = pmm::findGame("eu4");
    const auto* stellaris = pmm::findGame("stellaris");
    REQUIRE(ck3 != nullptr);
    REQUIRE(eu4 != nullptr);
    REQUIRE(stellaris != nullptr);
    CHECK(ck3->adapterAvailable);
    CHECK(eu4->adapterAvailable);
    CHECK(stellaris->adapterAvailable);
    CHECK(ck3->steamAppId == "1158310");
    CHECK(eu4->steamAppId == "236850");
    CHECK(stellaris->steamAppId == "281990");
    CHECK(ck3->displayName == "Crusader Kings III");
    CHECK(ck3->userDataFolder == "Crusader Kings III");
    CHECK(pmm::isSourceValidForGame("ck3", pmm::source::SteamWorkshop));
    CHECK(pmm::isSourceValidForGame("ck3", pmm::source::Local));
}

TEST_CASE("Catalog lists supported games first, alphabetically", "[core][catalog]") {
    // A fresh profile opens the first installed supported game in this order.
    std::vector<std::string_view> keys;
    for (const pmm::GameCatalogEntry& entry : pmm::gameCatalog())
        keys.push_back(entry.key);
    CHECK(keys == std::vector<std::string_view>{"ck3", "eu4", "stellaris", "hoi4", "vic3"});
    bool unsupportedSeen = false;
    for (const pmm::GameCatalogEntry& entry : pmm::gameCatalog()) {
        CHECK_FALSE((unsupportedSeen && entry.adapterAvailable));
        unsupportedSeen = unsupportedSeen || !entry.adapterAvailable;
    }
}

TEST_CASE("Catalog lists future games without claiming support", "[core][catalog]") {
    for (auto key : {"hoi4", "vic3"}) {
        const auto* game = pmm::findGame(key);
        REQUIRE(game != nullptr);
        CHECK_FALSE(game->adapterAvailable);
    }
    CHECK(pmm::findGame("not-a-game") == nullptr);
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
