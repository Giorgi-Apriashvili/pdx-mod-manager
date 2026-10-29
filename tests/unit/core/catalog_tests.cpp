#include "core/catalog.hpp"
#include "core/model.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Catalog exposes EU4 and Stellaris as supported games", "[core][catalog]") {
    const auto* eu4 = pmm::findGame("eu4");
    const auto* stellaris = pmm::findGame("stellaris");
    REQUIRE(eu4 != nullptr);
    REQUIRE(stellaris != nullptr);
    CHECK(eu4->adapterAvailable);
    CHECK(stellaris->adapterAvailable);
    CHECK(eu4->steamAppId == "236850");
    CHECK(stellaris->steamAppId == "281990");
}

TEST_CASE("Catalog lists future games without claiming support", "[core][catalog]") {
    for (auto key : {"hoi4", "ck3", "vic3"}) {
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
    for (auto s : {AcquisitionState::Pending, AcquisitionState::Subscribing,
                   AcquisitionState::Subscribed, AcquisitionState::DownloadRequested,
                   AcquisitionState::Downloading, AcquisitionState::Installed,
                   AcquisitionState::Failed, AcquisitionState::Unavailable,
                   AcquisitionState::NeedsUserAction, AcquisitionState::Cancelled}) {
        const auto parsed = pmm::acquisitionStateFromString(pmm::toString(s));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == s);
    }
    CHECK_FALSE(pmm::acquisitionStateFromString("bogus").has_value());
}
