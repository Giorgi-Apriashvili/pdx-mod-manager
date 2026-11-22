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

TEST_CASE("Update and reinstall jobs keep their kind, target and step", "[persistence][jobs]") {
    auto db = openMemory();
    AcquisitionJobRepository jobs(*db);
    AcquisitionJobRepository::NewJob update;
    update.kind = AcquisitionKind::Update;
    update.targetTimeUpdated = 1762000000;
    update.targetManifest = "1100000000000000301";
    auto created = require(jobs.createOrReuse(workshop("41"), std::nullopt, update));
    CHECK(created.kind == AcquisitionKind::Update);
    CHECK(created.targetTimeUpdated == 1762000000);
    CHECK(created.targetManifest == "1100000000000000301");
    CHECK(created.reinstallStep == ReinstallStep::Start);
    auto loaded = require(jobs.get(created.id));
    CHECK(loaded.kind == AcquisitionKind::Update);
    CHECK(loaded.targetTimeUpdated == 1762000000);
    CHECK(loaded.targetManifest == "1100000000000000301");
    CHECK(queryInt(*db, "SELECT target_time_updated FROM acquisition_jobs") == 1762000000000);

    // An unfinished job of another kind is reused unchanged.
    auto again = require(jobs.createOrReuse(workshop("41"), std::nullopt));
    CHECK(again.id == created.id);
    CHECK(again.kind == AcquisitionKind::Update);

    AcquisitionJobRepository::NewJob reinstall;
    reinstall.kind = AcquisitionKind::Reinstall;
    auto redo = require(jobs.createOrReuse(workshop("42"), std::nullopt, reinstall));
    redo.state = AcquisitionState::Unsubscribing;
    redo.reinstallStep = ReinstallStep::Unsubscribed;
    redo.targetTimeUpdated = 1763000000; // mutable, like the target manifest
    requireOk(jobs.update(redo));
    auto reloaded = require(jobs.get(redo.id));
    CHECK(reloaded.kind == AcquisitionKind::Reinstall);
    CHECK(reloaded.state == AcquisitionState::Unsubscribing);
    CHECK(reloaded.reinstallStep == ReinstallStep::Unsubscribed);
    CHECK(reloaded.targetTimeUpdated == 1763000000);
    redo.reinstallStep = ReinstallStep::Unsubscribing;
    requireOk(jobs.update(redo));
    CHECK(require(jobs.get(redo.id)).reinstallStep == ReinstallStep::Unsubscribing);
    CHECK(require(jobs.listUnfinished()).size() == 2);

    // 'unsubscribing' is unfinished: a second unfinished job for the mod conflicts.
    auto finished = require(jobs.createOrReuse(workshop("43"), std::nullopt));
    finished.state = AcquisitionState::Failed;
    requireOk(jobs.update(finished));
    auto running = require(jobs.createOrReuse(workshop("43"), std::nullopt, reinstall));
    running.state = AcquisitionState::Unsubscribing;
    requireOk(jobs.update(running));
    finished.state = AcquisitionState::Pending;
    auto conflict = jobs.update(finished);
    REQUIRE_FALSE(conflict.ok());
    CHECK(conflict.error().code == ErrorCode::AlreadyExists);

    execRaw(*db, "UPDATE acquisition_jobs SET kind = 'sideload' WHERE provider_id = '41'");
    CHECK(jobs.get(created.id).error().code == ErrorCode::DatabaseError);
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
            AcquisitionState::Pending,           AcquisitionState::Unsubscribing,
            AcquisitionState::Subscribing,       AcquisitionState::Subscribed,
            AcquisitionState::DownloadRequested, AcquisitionState::Downloading,
            AcquisitionState::Installed,         AcquisitionState::Failed,
            AcquisitionState::Unavailable,       AcquisitionState::NeedsUserAction,
            AcquisitionState::Cancelled};
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
    CHECK(unfinished.size() == 6);
    for (const auto& job : unfinished)
        CHECK_FALSE(isTerminal(job.state));
    CHECK(require(jobs.listForGame("stellaris")).size() == 11);
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

TEST_CASE("removeWithdrawnImportJobs removes only cancelled jobs without an error or a run",
          "[persistence][jobs]") {
    auto db = openMemory();
    AcquisitionJobRepository jobs(*db);
    // As an import with downloads off in 0.1.0 and 0.1.1 left it.
    auto withdrawn = require(jobs.createOrReuse(workshop("1"), {}));
    withdrawn.state = AcquisitionState::Cancelled;
    requireOk(jobs.update(withdrawn));
    // Cancelled by the user before anything was requested from Steam.
    auto userCancelled = require(jobs.createOrReuse(workshop("2"), {}));
    userCancelled.state = AcquisitionState::Cancelled;
    userCancelled.errorCode = "cancelled";
    userCancelled.errorMessage = "Cancelled before anything was requested from Steam.";
    requireOk(jobs.update(userCancelled));
    // Never matched without an error either: a run or bytes, another state.
    auto attempted = require(jobs.createOrReuse(workshop("3"), {}));
    attempted.state = AcquisitionState::Cancelled;
    attempted.attempts = 1;
    requireOk(jobs.update(attempted));
    auto withBytes = require(jobs.createOrReuse(workshop("4"), {}));
    withBytes.state = AcquisitionState::Cancelled;
    withBytes.bytesTotal = 4000;
    requireOk(jobs.update(withBytes));
    auto installed = require(jobs.createOrReuse(workshop("5"), {}));
    installed.state = AcquisitionState::Installed;
    requireOk(jobs.update(installed));
    auto pending = require(jobs.createOrReuse(workshop("6"), {}));

    CHECK(require(jobs.removeWithdrawnImportJobs()) == 1);
    CHECK(jobIds(require(jobs.listForGame("stellaris"))) ==
          std::vector<std::int64_t>{userCancelled.id, attempted.id, withBytes.id, installed.id,
                                    pending.id});
    CHECK(require(jobs.removeWithdrawnImportJobs()) == 0);
}

TEST_CASE("Jobs reject incomplete identities", "[persistence][jobs]") {
    auto db = openMemory();
    AcquisitionJobRepository jobs(*db);
    auto job = jobs.createOrReuse(ModIdentity{"stellaris", source::SteamWorkshop, ""}, {});
    REQUIRE_FALSE(job.ok());
    CHECK(job.error().code == ErrorCode::InvalidArgument);
}
