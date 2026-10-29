#include "persistence/launch_history_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/testing.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>

using namespace pmm;
using namespace pmm::persistence;
using namespace pmm::persistence::test;

TEST_CASE("Launch history records launches and returns the newest first",
          "[persistence][launches]") {
    auto db = openMemory();
    testing::setClock(*db, [] { return std::int64_t{9000}; });
    PlaysetRepository playsets(*db);
    Playset p;
    p.game = "stellaris";
    p.name = "Launched";
    const auto playsetId = require(playsets.create(p));

    LaunchHistoryRepository launches(*db);
    LaunchRecord started;
    started.game = "stellaris";
    started.playsetId = playsetId;
    started.startedAt = 1000;
    started.pid = 4242;
    started.outcome = "started";
    started.transactionId = "tx-20261029-0001";
    const auto firstId = require(launches.record(started));

    LaunchRecord failed;
    failed.game = "stellaris";
    failed.outcome = std::string(errorCodeName(ErrorCode::ProcessStartFailed));
    failed.detail = "CreateProcessW failed: access denied";
    const auto secondId = require(launches.record(failed)); // startedAt 0 -> now (9000)

    LaunchRecord other;
    other.game = "eu4";
    other.startedAt = 5000;
    other.outcome = "started";
    require(launches.record(other));

    auto latest = require(launches.latest("stellaris", 10));
    REQUIRE(latest.size() == 2);
    CHECK(latest[0].id == secondId);
    CHECK(latest[0].startedAt == 9000);
    CHECK(latest[0].outcome == "process_start_failed");
    CHECK(latest[0].detail == "CreateProcessW failed: access denied");
    CHECK_FALSE(latest[0].pid.has_value());
    CHECK_FALSE(latest[0].playsetId.has_value());
    CHECK(latest[1].id == firstId);
    CHECK(latest[1].pid == std::optional<std::int64_t>(4242));
    CHECK(latest[1].playsetId == std::optional<std::int64_t>(playsetId));
    CHECK(latest[1].transactionId == "tx-20261029-0001");

    CHECK(require(launches.latest("stellaris", 1)).size() == 1);
    CHECK(require(launches.latest("stellaris", 0)).empty());
    CHECK(require(launches.latest("eu4", 5)).size() == 1);

    LaunchRecord invalid;
    invalid.game = "stellaris";
    CHECK_FALSE(launches.record(invalid).ok());
}
