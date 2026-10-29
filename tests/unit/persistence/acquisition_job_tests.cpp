#include "persistence/acquisition_job_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/testing.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

using namespace pmm;
using namespace pmm::persistence;
using namespace pmm::persistence::test;

namespace {

ModIdentity workshop(const std::string& id, const GameKey& game = "stellaris") {
    return ModIdentity{game, source::SteamWorkshop, id};
}

std::vector<std::int64_t> jobIds(const std::vector<AcquisitionJob>& jobs) {
    std::vector<std::int64_t> out;
    for (const auto& j : jobs)
        out.push_back(j.id);
    return out;
}

} // namespace

TEST_CASE("createOrReuse returns the unfinished job for an identity", "[persistence][jobs]") {
    auto db = openMemory();
    testing::setClock(*db, [] { return std::int64_t{5000}; });
    AcquisitionJobRepository jobs(*db);

    auto first = require(jobs.createOrReuse(workshop("18446744073709551615"), std::nullopt));
    CHECK(first.id > 0);
    CHECK(first.identity == workshop("18446744073709551615"));
    CHECK(first.state == AcquisitionState::Pending);
    CHECK(first.attempts == 0);
    CHECK_FALSE(first.playsetId.has_value());
    CHECK(first.createdAt == 5000);
    CHECK(first.updatedAt == 5000);

    first.state = AcquisitionState::Downloading;
    requireOk(jobs.update(first));
    auto again = require(jobs.createOrReuse(workshop("18446744073709551615"), std::nullopt));
    CHECK(again.id == first.id);
    CHECK(again.state == AcquisitionState::Downloading);

    // Same id in another game is a different identity.
    auto otherGame = require(jobs.createOrReuse(workshop("18446744073709551615", "eu4"), {}));
    CHECK(otherGame.id != first.id);

    // Once finished, a new request creates a new job.
    again.state = AcquisitionState::Installed;
    requireOk(jobs.update(again));
    auto fresh = require(jobs.createOrReuse(workshop("18446744073709551615"), std::nullopt));
    CHECK(fresh.id != first.id);
    CHECK(fresh.state == AcquisitionState::Pending);
    CHECK(require(jobs.listForGame("stellaris")).size() == 2);

    CHECK(queryText(*db, "SELECT typeof(provider_id) FROM acquisition_jobs LIMIT 1") == "text");
}

TEST_CASE("update persists every mutable field", "[persistence][jobs]") {
    auto db = openMemory();
    std::int64_t now = 1000;
    testing::setClock(*db, [&now] { return now; });
    PlaysetRepository playsets(*db);
    Playset p;
    p.game = "stellaris";
    p.name = "Requester";
    const auto playsetId = require(playsets.create(p));

    AcquisitionJobRepository jobs(*db);
    auto job = require(jobs.createOrReuse(workshop("42"), playsetId));
    CHECK(job.playsetId == std::optional<std::int64_t>(playsetId));

    now = 2000;
    job.state = AcquisitionState::Failed;
    job.errorCode = "offline";
    job.errorMessage = "Steam is offline. Reconnect and retry.";
    job.attempts = 3;
    job.bytesDownloaded = std::numeric_limits<std::uint64_t>::max();
    job.bytesTotal = 1ull << 40;
    job.playsetId.reset();
    requireOk(jobs.update(job));

    auto loaded = require(jobs.get(job.id));
    CHECK(loaded.identity == workshop("42"));
    CHECK(loaded.state == AcquisitionState::Failed);
    CHECK(loaded.errorCode == "offline");
    CHECK(loaded.errorMessage == "Steam is offline. Reconnect and retry.");
    CHECK(loaded.attempts == 3);
    CHECK(loaded.bytesDownloaded == std::numeric_limits<std::uint64_t>::max());
    CHECK(loaded.bytesTotal == (1ull << 40));
    CHECK_FALSE(loaded.playsetId.has_value());
    CHECK(loaded.createdAt == 1000);
    CHECK(loaded.updatedAt == 2000);

    AcquisitionJob unknown = job;
    unknown.id = 987654;
    auto missing = jobs.update(unknown);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK(jobs.get(987654).error().code == ErrorCode::NotFound);
}

TEST_CASE("Reopening a finished job conflicts with another unfinished job", "[persistence][jobs]") {
    auto db = openMemory();
    AcquisitionJobRepository jobs(*db);
    auto failed = require(jobs.createOrReuse(workshop("7"), std::nullopt));
    failed.state = AcquisitionState::Failed;
    requireOk(jobs.update(failed));
    auto active = require(jobs.createOrReuse(workshop("7"), std::nullopt));
    REQUIRE(active.id != failed.id);

    failed.state = AcquisitionState::Pending; // retry while another job is active
    auto status = jobs.update(failed);
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().code == ErrorCode::AlreadyExists);
    CHECK(require(jobs.get(failed.id)).state == AcquisitionState::Failed);
}

TEST_CASE("listUnfinished returns non-terminal jobs for restart reconciliation",
          "[persistence][jobs]") {
    TempDir temp;
    std::vector<std::int64_t> expected;
    {
        auto db = require(Database::open(temp.dbFile()));
        AcquisitionJobRepository jobs(*db);
        const AcquisitionState states[] = {
            AcquisitionState::Pending,         AcquisitionState::Subscribing,
            AcquisitionState::Subscribed,      AcquisitionState::DownloadRequested,
            AcquisitionState::Downloading,     AcquisitionState::Installed,
            AcquisitionState::Failed,          AcquisitionState::Unavailable,
            AcquisitionState::NeedsUserAction, AcquisitionState::Cancelled};
        int n = 0;
        for (auto state : states) {
            auto job = require(jobs.createOrReuse(workshop(std::to_string(100 + n++)), {}));
            job.state = state;
            requireOk(jobs.update(job));
            if (!isTerminal(state))
                expected.push_back(job.id);
        }
    }
    // Simulates an application restart.
    auto db = require(Database::open(temp.dbFile()));
    AcquisitionJobRepository jobs(*db);
    auto unfinished = require(jobs.listUnfinished());
    CHECK(jobIds(unfinished) == expected);
    CHECK(unfinished.size() == 5);
    for (const auto& job : unfinished)
        CHECK_FALSE(isTerminal(job.state));
    CHECK(require(jobs.listForGame("stellaris")).size() == 10);
    CHECK(require(jobs.listForGame("eu4")).empty());
}

TEST_CASE("removeFinishedOlderThan only removes old terminal jobs", "[persistence][jobs]") {
    auto db = openMemory();
    std::int64_t now = 1000;
    testing::setClock(*db, [&now] { return now; });
    AcquisitionJobRepository jobs(*db);

    auto oldDone = require(jobs.createOrReuse(workshop("1"), {}));
    oldDone.state = AcquisitionState::Installed;
    requireOk(jobs.update(oldDone));
    auto oldActive = require(jobs.createOrReuse(workshop("2"), {}));
    now = 50'000;
    auto newDone = require(jobs.createOrReuse(workshop("3"), {}));
    newDone.state = AcquisitionState::Cancelled;
    requireOk(jobs.update(newDone));

    CHECK(require(jobs.removeFinishedOlderThan(10'000)) == 1);
    auto remaining = require(jobs.listForGame("stellaris"));
    CHECK(jobIds(remaining) == std::vector<std::int64_t>{oldActive.id, newDone.id});
}

TEST_CASE("Jobs reject incomplete identities", "[persistence][jobs]") {
    auto db = openMemory();
    AcquisitionJobRepository jobs(*db);
    auto job = jobs.createOrReuse(ModIdentity{"stellaris", source::SteamWorkshop, ""}, {});
    REQUIRE_FALSE(job.ok());
    CHECK(job.error().code == ErrorCode::InvalidArgument);
}
