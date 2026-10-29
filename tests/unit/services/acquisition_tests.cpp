#include "core/log.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "providers/fallback.hpp"
#include "services/acquisition_coordinator.hpp"
#include "services/playset_view.hpp"
#include "services/share_service.hpp"
#include "sharing/share_code.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <map>
#include <memory>

using namespace pmm;
using namespace pmm::services;
using namespace pmm::services::test;
using Catch::Matchers::ContainsSubstring;

namespace {

constexpr const char* kItem = "2000000101";

// World + fake helper + fake Web API + coordinator, wired like the application does.
struct Rig {
    World w;
    FakeSteam steam{w.workshop};
    FakeMetadata metadata;
    GameService games{w.ctx};
    CoordinatorOptions options;
    bool webApi = true; // false: the coordinator gets no metadata host
    std::unique_ptr<AcquisitionCoordinator> coordinator;

    Rig() {
        w.refresh();
        restart();
    }

    // A new coordinator over the same database: what an application restart sees.
    AcquisitionCoordinator& restart() {
        coordinator = std::make_unique<AcquisitionCoordinator>(
            w.ctx, games, steam, webApi ? &metadata : nullptr, options);
        return *coordinator;
    }

    AcquisitionCoordinator& c() { return *coordinator; }

    // Delivers Web API replies and helper events and advances the clock until
    // no job of the game is in progress or waiting for an automatic retry.
    std::vector<GameState> settle(int maxSteps = 400) {
        std::vector<GameState> rescans;
        const auto collect = [&rescans](CoordinatorUpdate update) {
            for (GameState& state : update.rescanned)
                rescans.push_back(std::move(state));
        };
        for (int i = 0; i < maxSteps; ++i) {
            for (CoordinatorUpdate& update : metadata.answer(c()))
                collect(std::move(update));
            for (CoordinatorUpdate& update : steam.pump(c()))
                collect(std::move(update));
            if (!c().hasActiveWork(kGame) && steam.pending.empty())
                return rescans;
            w.clockMs += 1000;
            collect(c().tick(w.clockMs));
        }
        FAIL("acquisition did not settle");
        return rescans;
    }

    std::int64_t playset(std::vector<PlaysetEntry> entries) {
        persistence::PlaysetRepository repo(*w.db);
        Playset p;
        p.game = kGame;
        p.name = "Acquire";
        p.entries = std::move(entries);
        return require(repo.create(p));
    }

    // Playset with the given entries plus jobs for the missing ones, started.
    // The Web API answers (when `metadata.answering`) before the helper does.
    std::int64_t acquire(std::vector<PlaysetEntry> entries) {
        const std::int64_t id = playset(std::move(entries));
        require(c().acquireMissing(id));
        metadata.answer(c());
        return id;
    }

    AcquisitionJob job(const std::string& id) {
        persistence::AcquisitionJobRepository repo(*w.db);
        AcquisitionJob found;
        bool seen = false;
        for (const AcquisitionJob& job : require(repo.listForGame(kGame))) {
            if (job.identity.id == id && (!seen || job.id > found.id)) {
                found = job;
                seen = true;
            }
        }
        REQUIRE(seen);
        return found;
    }

    const JobSnapshot& snapshotJob(const AcquisitionSnapshot& snapshot, const std::string& id) {
        for (const JobSnapshot& job : snapshot.jobs) {
            if (job.job.identity.id == id)
                return job;
        }
        FAIL("no job for " << id);
        return snapshot.jobs.front();
    }
};

std::string codeFor(const std::vector<PlaysetEntry>& entries) {
    sharing::Manifest manifest;
    manifest.game = kGame;
    manifest.playsetName = "Shared";
    for (const PlaysetEntry& e : entries)
        manifest.mods.push_back({e.identity.source, e.identity.id, e.nameHint, e.enabled, {}, {}});
    return require(sharing::encodeShareCode(manifest));
}

bool installedInInventory(World& w, const std::string& id) {
    persistence::InventoryRepository inventory(*w.db);
    return require(inventory.find({kGame, source::SteamWorkshop, id})).has_value();
}

// Web API reply for an item that belongs to EU4, not to the fake Stellaris.
providers::WorkshopItemMetadata eu4Item(const std::string& id) {
    providers::WorkshopItemMetadata item;
    item.id = id;
    item.found = true;
    item.resultCode = 1;
    item.title = "Some EU4 Mod";
    item.consumerAppId = "236850";
    item.creatorAppId = "236850";
    return item;
}

// Largest number of unfinished in-memory jobs that share one mod.
std::size_t unfinishedPerMod(const AcquisitionCoordinator& coordinator) {
    std::map<ModIdentity, std::size_t> counts;
    std::size_t most = 0;
    for (const AcquisitionJob& job : coordinator.liveJobs(kGame)) {
        if (!isTerminal(job.state))
            most = std::max(most, ++counts[job.identity]);
    }
    return most;
}

// Captures log records for the lifetime of the object.
class LogCapture {
public:
    LogCapture() {
        setLogSink([this](const LogRecord& record) { m_records.push_back(record); });
    }
    ~LogCapture() { setLogSink({}); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    std::size_t count(std::string_view message) const {
        return static_cast<std::size_t>(
            std::count_if(m_records.begin(), m_records.end(),
                          [&](const LogRecord& record) { return record.message == message; }));
    }

private:
    std::vector<LogRecord> m_records;
};

} // namespace

TEST_CASE("An imported missing mod is subscribed, downloaded, verified and rescanned",
          "[services][acquisition]") {
    Rig r;
    r.steam.items[kItem].name = "Fresh Example";
    ShareService share(r.w.ctx);
    const AppliedImport applied = require(share.applyImport(
        share.previewImport(codeFor({workshopEntry(kItem, "Fresh Example")})), ImportMode::New));
    REQUIRE(applied.jobs.size() == 1);

    const CoordinatorUpdate started = r.c().start(kGame);
    CHECK(started.changed);
    REQUIRE(r.steam.opens == 1);
    CHECK(r.steam.openedAppIds[0] == kAppId);
    CHECK(r.steam.openedLibraries[0] == r.w.install / platform::steam::steamApiLibraryName());
    REQUIRE(r.metadata.requests.size() == 1);
    CHECK(r.metadata.requests[0] == std::vector<std::string>{kItem});
    CHECK(r.c().snapshot(kGame).active);

    const std::vector<GameState> rescans = r.settle();

    const AcquisitionJob job = r.job(kItem);
    CHECK(job.state == AcquisitionState::Installed);
    CHECK(job.bytesTotal == 4000);
    CHECK(job.bytesDownloaded == 4000);
    CHECK(job.errorCode.empty());
    // Subscribe and download were requested exactly once, after fresh state queries.
    CHECK(r.steam.sentIds<providers::SubscribeCommand>() == std::vector<std::string>{kItem});
    CHECK(r.steam.sentIds<providers::DownloadCommand>() == std::vector<std::string>{kItem});
    REQUIRE(r.steam.sent.size() >= 4);
    CHECK(std::holds_alternative<providers::StateCommand>(r.steam.sent[0]));
    CHECK(std::holds_alternative<providers::SubscribeCommand>(r.steam.sent[1]));
    CHECK(std::holds_alternative<providers::StateCommand>(r.steam.sent[2]));
    CHECK(std::holds_alternative<providers::DownloadCommand>(r.steam.sent[3]));

    // The inventory was rescanned, so readiness flips to Ready from the inventory.
    REQUIRE(rescans.size() == 1);
    CHECK(installedInInventory(r.w, kItem));
    const PlaysetView view =
        require(loadPlaysetView(r.w.ctx, applied.playsetId, r.c().liveJobs(kGame)));
    CHECK(view.rows[0].readiness.status == EntryStatus::Ready);
    CHECK(view.rows[0].displayName == "Fresh Example");
    CHECK(view.launch.canLaunch);

    // The session is closed as soon as nothing waits for Steam.
    CHECK(r.steam.closes == 1);
    CHECK_FALSE(r.c().sessionOpen(kGame));
    const AcquisitionSnapshot snapshot = r.c().snapshot(kGame);
    CHECK(snapshot.providerStatus == ProviderStatus::Ready);
    CHECK_FALSE(snapshot.active);
    CHECK(r.snapshotJob(snapshot, kItem).stateText == "Installed");
    CHECK_FALSE(r.snapshotJob(snapshot, kItem).canRetry);
}

TEST_CASE("Content Steam already installed is verified, never downloaded again",
          "[services][acquisition]") {
    Rig r;
    r.metadata.answering = false;               // verifying needs no Web API confirmation
    r.w.addWorkshopMod(kItem, "Already There"); // on disk, inventory not rescanned yet
    r.steam.items[kItem].flags = providers::item_flag::Subscribed | providers::item_flag::Installed;
    r.acquire({workshopEntry(kItem, "Already There", false)}); // disabled entries are acquired too
    r.settle();
    CHECK(r.job(kItem).state == AcquisitionState::Installed);
    CHECK(r.steam.sentIds<providers::SubscribeCommand>().empty());
    CHECK(r.steam.sentIds<providers::DownloadCommand>().empty());
    CHECK(installedInInventory(r.w, kItem));
}

TEST_CASE("Offline failures retry automatically and recover", "[services][acquisition]") {
    Rig r;
    r.steam.items[kItem].subscribeResult = 3; // NoConnection
    r.acquire({workshopEntry(kItem, "Offline Example")});
    r.steam.pump(r.c());

    AcquisitionJob job = r.job(kItem);
    CHECK(job.state == AcquisitionState::Failed);
    CHECK(job.errorCode == "offline");
    CHECK_THAT(job.errorMessage, ContainsSubstring("Retrying automatically"));
    CHECK(r.c().hasActiveWork(kGame));     // automatic retry scheduled
    CHECK_FALSE(r.c().sessionOpen(kGame)); // but the helper is not kept running meanwhile
    CHECK(r.snapshotJob(r.c().snapshot(kGame), kItem).canRetry);

    r.steam.items[kItem].subscribeResult = 1; // back online before the retry fires
    r.settle();
    job = r.job(kItem);
    CHECK(job.state == AcquisitionState::Installed);
    CHECK(job.attempts == 2);
    CHECK(r.steam.opens == 2);
}

TEST_CASE("Exhausted retries wait for the user's Retry", "[services][acquisition]") {
    Rig r;
    r.steam.items[kItem].subscribeResult = 16; // Timeout, retryable
    r.acquire({workshopEntry(kItem, "Flaky Example")});
    r.settle();
    AcquisitionJob job = r.job(kItem);
    CHECK(job.state == AcquisitionState::Failed);
    CHECK(job.attempts == 3);
    CHECK_THAT(job.errorMessage, ContainsSubstring("Automatic retries are exhausted"));
    CHECK_FALSE(r.c().hasActiveWork(kGame));

    r.steam.items[kItem].subscribeResult = 1;
    const CoordinatorUpdate retried = r.c().retryAllFailed(kGame);
    CHECK(retried.changed);
    r.settle();
    CHECK(r.job(kItem).state == AcquisitionState::Installed);

    const auto again = r.c().retry(r.job(kItem).id);
    REQUIRE_FALSE(again);
    CHECK(again.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("A helper crash during a download is retried", "[services][acquisition]") {
    Rig r;
    r.steam.items[kItem].steps = 4;
    r.acquire({workshopEntry(kItem, "Crash Example")});
    r.steam.pump(r.c());
    REQUIRE(r.job(kItem).state == AcquisitionState::Downloading);
    r.steam.pending.emplace_back(FakeSteam::Exit{139});
    r.steam.pump(r.c());
    CHECK(r.job(kItem).state == AcquisitionState::Failed);
    CHECK(r.c().snapshot(kGame).providerStatus == ProviderStatus::Unavailable);
    r.settle();
    CHECK(r.job(kItem).state == AcquisitionState::Installed);
    CHECK(r.steam.opens == 2);
}

TEST_CASE("Deleted, private or wrong-game items become unavailable and stay in the playset",
          "[services][acquisition]") {
    Rig r;
    SECTION("Steam reports the item as not found") {
        r.steam.items[kItem].subscribeResult = 9; // FileNotFound
        const std::int64_t id = r.acquire({workshopEntry(kItem, "Deleted Example")});
        r.settle();
        const AcquisitionJob job = r.job(kItem);
        CHECK(job.state == AcquisitionState::Unavailable);
        CHECK(job.errorCode == "item_unavailable");
        const PlaysetView view = require(loadPlaysetView(r.w.ctx, id, r.c().liveJobs(kGame)));
        REQUIRE(view.rows.size() == 1);
        CHECK(view.rows[0].readiness.status == EntryStatus::Unavailable);
        CHECK(view.rows[0].blocking);
        CHECK_FALSE(view.launch.canLaunch);
    }
    SECTION("the Web API reports it missing or for another game") {
        constexpr const char* kOther = "2000000102";
        r.metadata.answering = false; // answered by hand below
        r.acquire({workshopEntry(kItem, "Gone"), workshopEntry(kOther, "Wrong game")});
        REQUIRE(r.metadata.requests.size() == 1);
        CHECK(r.metadata.requests[0].size() == 2);
        providers::WorkshopItemMetadata gone;
        gone.id = kItem;
        gone.resultCode = 9;
        providers::WorkshopItemMetadata other;
        other.id = kOther;
        other.found = true;
        other.resultCode = 1;
        other.title = "Some EU4 Mod";
        other.consumerAppId = "236850";
        r.c().onMetadata(kGame, {gone, other});
        CHECK(r.job(kItem).state == AcquisitionState::Unavailable);
        CHECK(r.job(kOther).state == AcquisitionState::Unavailable);
        CHECK(r.job(kOther).errorCode == "wrong_game");
        CHECK(r.snapshotJob(r.c().snapshot(kGame), kOther).name == "Some EU4 Mod");
        r.settle();
        CHECK(r.steam.sentIds<providers::SubscribeCommand>().empty());
    }
}

TEST_CASE("Steam not running leaves jobs waiting for the user with the manual fallback",
          "[services][acquisition]") {
    Rig r;
    SECTION("refused ready event") {
        r.steam.ready = providers::ReadyEvent{};
        r.steam.ready.ok = false;
        r.steam.ready.error = providers::HelperFailure::SteamNotRunning;
        r.steam.ready.message = "Steam client is not running";
    }
    SECTION("helper exit code before ready") {
        r.steam.exitBeforeReady = static_cast<int>(providers::HelperExitCode::SteamNotRunning);
    }
    r.acquire({workshopEntry(kItem, "Example")});
    r.settle();

    const AcquisitionJob job = r.job(kItem);
    CHECK(job.state == AcquisitionState::NeedsUserAction);
    CHECK_THAT(job.errorMessage, ContainsSubstring("Steam is not running"));
    const AcquisitionSnapshot snapshot = r.c().snapshot(kGame);
    CHECK(snapshot.providerStatus == ProviderStatus::SteamNotRunning);
    CHECK(std::string(toString(snapshot.providerStatus)) == "steam_not_running");
    CHECK_THAT(snapshot.providerMessage, ContainsSubstring("Start Steam"));
    CHECK(snapshot.summary == "1 needs attention");
    const JobSnapshot& item = r.snapshotJob(snapshot, kItem);
    CHECK(item.canRetry);
    CHECK(item.canOpenFallback);
    REQUIRE(item.fallback);
    CHECK(item.fallback->steamClientUrl == std::string("steam://url/CommunityFilePage/") + kItem);
    CHECK_FALSE(r.c().sessionOpen(kGame));

    const FallbackOutcome fallback = require(r.c().openFallback(r.job(kItem).id));
    CHECK(fallback.label == providers::kManualFallbackLabel);
    CHECK_THAT(fallback.links.webUrl, ContainsSubstring(kItem));
    CHECK_THAT(r.job(kItem).errorMessage, ContainsSubstring("Manual fallback"));

    // The user starts Steam and retries.
    r.steam.exitBeforeReady.reset();
    r.steam.ready = providers::ReadyEvent{};
    r.steam.ready.ok = true;
    r.steam.ready.loggedOn = true;
    r.steam.ready.ownsApp = true;
    require(r.c().retry(r.job(kItem).id));
    r.settle();
    CHECK(r.job(kItem).state == AcquisitionState::Installed);
}

TEST_CASE("Content installed through the manual fallback is picked up by a rescan",
          "[services][acquisition]") {
    Rig r;
    r.steam.ready = providers::ReadyEvent{};
    r.steam.ready.ok = false;
    r.steam.ready.error = providers::HelperFailure::SteamNotRunning;
    const std::int64_t id = r.acquire({workshopEntry(kItem, "Manual Example")});
    r.settle();
    require(r.c().openFallback(r.job(kItem).id));
    REQUIRE(r.c().snapshot(kGame).jobs.size() == 1);
    CHECK(r.steam.sentIds<providers::SubscribeCommand>().empty());

    // The user subscribes on the Workshop page; Steam downloads; the user rescans.
    r.w.addWorkshopMod(kItem, "Manual Example");
    require(r.games.rescanMods(kGame));

    const PlaysetView view = require(loadPlaysetView(r.w.ctx, id, r.c().liveJobs(kGame)));
    CHECK(view.rows[0].readiness.status == EntryStatus::Ready);
    CHECK(view.launch.canLaunch);
    // The job no longer asks for attention, although it never saw the download.
    const AcquisitionSnapshot snapshot = r.c().snapshot(kGame);
    CHECK(snapshot.jobs.empty());
    CHECK(snapshot.summary.empty());
    CHECK(r.job(kItem).state == AcquisitionState::NeedsUserAction);
}

TEST_CASE("Unavailable providers are reported without touching Steam", "[services][acquisition]") {
    Rig r;
    SECTION("the game's Steam API library is missing") {
        fs::remove(r.w.install / platform::steam::steamApiLibraryName());
        r.acquire({workshopEntry(kItem, "Example")});
        r.settle();
        CHECK(r.job(kItem).state == AcquisitionState::NeedsUserAction);
        CHECK_THAT(r.job(kItem).errorMessage, ContainsSubstring("Steam API library"));
        CHECK(r.c().snapshot(kGame).providerStatus == ProviderStatus::Unavailable);
    }
    SECTION("the game is not located") {
        r.w.adapter.discovered.clear();
        r.w.refresh();
        r.acquire({workshopEntry(kItem, "Example")});
        r.settle();
        CHECK(r.job(kItem).state == AcquisitionState::NeedsUserAction);
        CHECK_THAT(r.job(kItem).errorMessage, ContainsSubstring("has not been found"));
    }
    SECTION("the helper executable is missing") {
        r.options.helperExecutable = r.w.root / u8("missing helper.exe");
        r.restart();
        r.acquire({workshopEntry(kItem, "Example")});
        r.settle();
        CHECK(r.job(kItem).state == AcquisitionState::NeedsUserAction);
        CHECK_THAT(r.job(kItem).errorMessage, ContainsSubstring("pmm-steam-helper"));
    }
    SECTION("the helper cannot be started") {
        r.steam.openSucceeds = false;
        r.acquire({workshopEntry(kItem, "Example")});
        r.settle();
        CHECK(r.job(kItem).state == AcquisitionState::NeedsUserAction);
        CHECK(r.steam.opens == 1);
    }
    CHECK(r.steam.sent.empty());
    CHECK(r.snapshotJob(r.c().snapshot(kGame), kItem).canOpenFallback);
}

TEST_CASE("Steam offline mode and missing ownership need the user", "[services][acquisition]") {
    Rig r;
    SECTION("not signed in") {
        r.steam.ready.loggedOn = false;
        r.acquire({workshopEntry(kItem, "Example")});
        r.settle();
        CHECK(r.c().snapshot(kGame).providerStatus == ProviderStatus::NotLoggedOn);
        CHECK_THAT(r.job(kItem).errorMessage, ContainsSubstring("offline or not signed in"));
    }
    SECTION("game not owned") {
        r.steam.ready.ownsApp = false;
        r.acquire({workshopEntry(kItem, "Example")});
        r.settle();
        CHECK(r.c().snapshot(kGame).providerStatus == ProviderStatus::NotOwned);
        CHECK(r.job(kItem).errorCode == "access_denied");
    }
    CHECK(r.job(kItem).state == AcquisitionState::NeedsUserAction);
    CHECK(r.steam.sentIds<providers::SubscribeCommand>().empty());
}

TEST_CASE("Persisted jobs are reconciled with Steam after a restart", "[services][acquisition]") {
    Rig r;
    constexpr const char* kSubscribed = "2000000201";
    constexpr const char* kFinished = "2000000202";
    const std::int64_t id = r.playset({workshopEntry(kSubscribed, "Was downloading"),
                                       workshopEntry(kFinished, "Finished while closed", false)});
    persistence::AcquisitionJobRepository repo(*r.w.db);
    AcquisitionJob first =
        require(repo.createOrReuse({kGame, source::SteamWorkshop, kSubscribed}, id));
    first.state = AcquisitionState::Downloading;
    first.attempts = 1;
    first.bytesDownloaded = 1000;
    first.bytesTotal = 4000;
    requireOk(repo.update(first));
    AcquisitionJob second =
        require(repo.createOrReuse({kGame, source::SteamWorkshop, kFinished}, id));
    second.state = AcquisitionState::Subscribed;
    second.attempts = 1;
    requireOk(repo.update(second));

    // Meanwhile Steam kept working: one download is still running, one finished.
    using namespace providers::item_flag;
    r.steam.items[kSubscribed].flags = Subscribed | Downloading;
    r.steam.items[kSubscribed].downloaded = 1000;
    r.steam.items[kFinished].flags = Subscribed | Installed;
    r.w.addWorkshopMod(kFinished, "Finished while closed");

    AcquisitionCoordinator& c = r.restart();
    c.start(kGame);
    r.steam.pump(c);
    // Both jobs asked for their state in one batched command.
    REQUIRE_FALSE(r.steam.sent.empty());
    CHECK(std::get<providers::StateCommand>(r.steam.sent[0]).ids.size() == 2);
    r.settle();

    CHECK(r.job(kSubscribed).state == AcquisitionState::Installed);
    CHECK(r.job(kFinished).state == AcquisitionState::Installed);
    CHECK(r.job(kSubscribed).id == first.id); // resumed, not recreated
    CHECK(r.steam.sentIds<providers::SubscribeCommand>().empty());
    CHECK(r.steam.sentIds<providers::DownloadCommand>().empty());
    CHECK(installedInInventory(r.w, kFinished));
    CHECK(require(loadPlaysetView(r.w.ctx, id, c.liveJobs(kGame))).launch.canLaunch);
}

TEST_CASE("Cancelling stops application work, explains what remains, never claims readiness",
          "[services][acquisition]") {
    Rig r;
    r.steam.items[kItem].steps = 50;
    const std::int64_t id = r.acquire({workshopEntry(kItem, "Big Example")});
    r.steam.pump(r.c());
    REQUIRE(r.job(kItem).state == AcquisitionState::Downloading);
    const std::size_t sentBefore = r.steam.sent.size();

    const CancelOutcome outcome = r.c().cancelAll(kGame);
    CHECK(outcome.cancelled == 1);
    CHECK_THAT(outcome.message, ContainsSubstring("remain in Steam"));
    CHECK_THAT(outcome.message, ContainsSubstring("Nothing was unsubscribed"));
    const AcquisitionJob job = r.job(kItem);
    CHECK(job.state == AcquisitionState::Cancelled);
    CHECK_THAT(job.errorMessage, ContainsSubstring("keeps the subscription"));
    CHECK_FALSE(r.c().sessionOpen(kGame));
    CHECK_FALSE(r.c().hasActiveWork(kGame));

    r.w.clockMs += 60'000;
    r.c().tick(r.w.clockMs);
    CHECK(r.steam.sent.size() == sentBefore);
    const PlaysetView view = require(loadPlaysetView(r.w.ctx, id, r.c().liveJobs(kGame)));
    CHECK(view.rows[0].readiness.status == EntryStatus::Missing);
    CHECK_FALSE(view.launch.canLaunch);
    CHECK(r.c().cancelAll(kGame).message == "There was nothing to cancel.");
}

TEST_CASE("Launch preparation parks jobs and closes the helper session",
          "[services][acquisition]") {
    Rig r;
    r.steam.items[kItem].steps = 4;
    r.acquire({workshopEntry(kItem, "Background Example", false)});
    r.steam.pump(r.c());
    REQUIRE(r.c().sessionOpen(kGame));

    r.c().closeSessionsForLaunch(kGame);
    CHECK_FALSE(r.c().sessionOpen(kGame));
    CHECK(r.steam.closes == 1);
    CHECK(r.c().liveJobs(kGame).empty());
    CHECK(r.job(kItem).state == AcquisitionState::Downloading); // persisted for later
    CHECK(r.snapshotJob(r.c().snapshot(kGame), kItem).stateText == "Waiting to resume");

    r.c().start(kGame);
    r.settle();
    CHECK(r.job(kItem).state == AcquisitionState::Installed);
}

TEST_CASE("A failed job waiting for its automatic retry resumes after a launch or a restart",
          "[services][acquisition]") {
    Rig r;
    r.steam.items[kItem].subscribeResult = 3; // NoConnection: retried automatically
    r.acquire({workshopEntry(kItem, "Retry Example")});
    r.steam.pump(r.c());
    REQUIRE(r.job(kItem).state == AcquisitionState::Failed);
    REQUIRE(r.c().hasActiveWork(kGame));
    const int attempts = r.job(kItem).attempts;

    SECTION("across a game launch") {
        r.c().closeSessionsForLaunch(kGame);
        CHECK(r.c().liveJobs(kGame).empty());
    }
    SECTION("across an application restart") {
        r.c().shutdown();
        r.restart();
    }
    // Saved as the queued run the retry would have started, so start(game)
    // resumes it; the text no longer promises a retry that nothing schedules.
    const AcquisitionJob parked = r.job(kItem);
    CHECK(parked.state == AcquisitionState::Pending);
    CHECK(parked.attempts == attempts + 1);
    CHECK_THAT(parked.errorMessage, !ContainsSubstring("Retrying automatically"));
    CHECK(r.snapshotJob(r.c().snapshot(kGame), kItem).stateText == "Waiting to resume");

    r.steam.items[kItem].subscribeResult = 1;
    r.c().start(kGame);
    r.settle();
    CHECK(r.job(kItem).state == AcquisitionState::Installed);
    CHECK(r.job(kItem).id == parked.id);
    CHECK(r.steam.sentIds<providers::SubscribeCommand>().size() == 2);
}

TEST_CASE("A newer job for the same mod supersedes an older failed one",
          "[services][acquisition]") {
    Rig r;
    LogCapture logs;
    SECTION("while the older job waits for its automatic retry") {
        r.steam.items[kItem].subscribeResult = 3; // NoConnection: retried in 5 s
        r.acquire({workshopEntry(kItem, "Example")});
        r.steam.pump(r.c());
        REQUIRE(r.job(kItem).state == AcquisitionState::Failed);
        REQUIRE(r.c().hasActiveWork(kGame));
    }
    SECTION("after the older job's automatic retries are exhausted") {
        r.steam.items[kItem].subscribeResult = 16; // Timeout
        r.acquire({workshopEntry(kItem, "Example")});
        r.settle();
        REQUIRE(r.job(kItem).state == AcquisitionState::Failed);
        REQUIRE(r.job(kItem).attempts == 3);
    }
    const std::int64_t older = r.job(kItem).id;
    r.steam.items[kItem].subscribeResult = 1;
    r.steam.items[kItem].steps = 4;

    // The user imports a code with the same mod: a new job, since Failed is terminal.
    ShareService share(r.w.ctx);
    const AppliedImport applied = require(share.applyImport(
        share.previewImport(codeFor({workshopEntry(kItem, "Example")})), ImportMode::New));
    REQUIRE(applied.jobs.size() == 1);
    const std::int64_t newer = applied.jobs[0].id;
    REQUIRE(newer != older);
    const std::size_t subscribesBefore = r.steam.sentIds<providers::SubscribeCommand>().size();

    r.c().start(kGame);
    bool settled = false;
    for (int i = 0; i < 100 && !settled; ++i) {
        r.steam.pump(r.c());
        CHECK(unfinishedPerMod(r.c()) <= 1);
        settled = !r.c().hasActiveWork(kGame) && r.steam.pending.empty();
        r.w.clockMs += 1000;
        r.c().tick(r.w.clockMs);
        CHECK(unfinishedPerMod(r.c()) <= 1);
    }
    REQUIRE(settled);

    CHECK(r.job(kItem).id == newer);
    CHECK(r.job(kItem).state == AcquisitionState::Installed);
    persistence::AcquisitionJobRepository repo(*r.w.db);
    CHECK(require(repo.get(older)).state == AcquisitionState::Failed);
    CHECK(r.steam.sentIds<providers::SubscribeCommand>().size() == subscribesBefore + 1);
    CHECK(logs.count("saving an acquisition job failed") == 0);
    for (const AcquisitionJob& job : r.c().liveJobs(kGame))
        CHECK(job.id != older);
}

TEST_CASE("Retrying an older job is refused while a newer one waits for its automatic retry",
          "[services][acquisition]") {
    Rig r;
    LogCapture logs;
    r.steam.items[kItem].subscribeResult = 16; // Timeout: retries exhaust
    r.acquire({workshopEntry(kItem, "Example")});
    r.settle();
    const std::int64_t older = r.job(kItem).id;
    REQUIRE(r.job(kItem).state == AcquisitionState::Failed);

    r.steam.items[kItem].subscribeResult = 3; // NoConnection: retried automatically
    ShareService share(r.w.ctx);
    const AppliedImport applied = require(share.applyImport(
        share.previewImport(codeFor({workshopEntry(kItem, "Example")})), ImportMode::New));
    REQUIRE(applied.jobs.size() == 1);
    r.c().start(kGame);
    r.steam.pump(r.c());
    REQUIRE(r.job(kItem).id == applied.jobs[0].id);
    REQUIRE(r.job(kItem).state == AcquisitionState::Failed);
    REQUIRE(r.c().hasActiveWork(kGame));

    const auto retried = r.c().retry(older);
    REQUIRE_FALSE(retried);
    CHECK(retried.error().code == ErrorCode::AlreadyExists);
    CHECK(unfinishedPerMod(r.c()) <= 1);

    r.steam.items[kItem].subscribeResult = 1;
    r.settle();
    CHECK(r.job(kItem).state == AcquisitionState::Installed);
    persistence::AcquisitionJobRepository repo(*r.w.db);
    CHECK(require(repo.get(older)).state == AcquisitionState::Failed);
    CHECK(logs.count("saving an acquisition job failed") == 0);
}

TEST_CASE("An older job revived next to a running one stops at its first save",
          "[services][acquisition]") {
    Rig r;
    LogCapture logs;
    r.steam.items[kItem].subscribeResult = 16; // Timeout: retries exhaust
    r.acquire({workshopEntry(kItem, "Example")});
    r.settle();
    const std::int64_t older = r.job(kItem).id;

    r.steam.items[kItem].subscribeResult = 1;
    r.steam.items[kItem].steps = 6;
    ShareService share(r.w.ctx);
    require(share.applyImport(share.previewImport(codeFor({workshopEntry(kItem, "Example")})),
                              ImportMode::New));
    r.c().start(kGame);
    r.steam.pump(r.c());
    REQUIRE(r.job(kItem).state == AcquisitionState::Downloading);

    // The manual fallback of the older job brings it back into memory, where
    // the running download's state reports reach it too and would revive it.
    require(r.c().openFallback(older));
    r.settle();

    CHECK(r.job(kItem).state == AcquisitionState::Installed);
    persistence::AcquisitionJobRepository repo(*r.w.db);
    CHECK(require(repo.get(older)).state == AcquisitionState::NeedsUserAction);
    CHECK(logs.count("saving an acquisition job failed") == 0);
    CHECK(logs.count("stopped a duplicate acquisition job") == 1);
    for (const AcquisitionJob& job : r.c().liveJobs(kGame))
        CHECK(job.id != older);
}

TEST_CASE("Nothing is subscribed until the Web API confirms the item belongs to the game",
          "[services][acquisition]") {
    Rig r;
    r.metadata.answering = false; // the helper answers before the Web API

    SECTION("confirmed late: subscribed once, then installed") {
        r.acquire({workshopEntry(kItem, "Late Example")});
        r.steam.pump(r.c());
        CHECK(r.steam.sentIds<providers::SubscribeCommand>().empty());
        CHECK(r.job(kItem).state == AcquisitionState::Pending);
        CHECK(r.c().hasActiveWork(kGame));
        CHECK(r.c().sessionOpen(kGame));
        CHECK(r.snapshotJob(r.c().snapshot(kGame), kItem).stateText ==
              "Checking the item with Steam");
        // Still held a few seconds later.
        for (int i = 0; i < 8; ++i) {
            r.w.clockMs += 1000;
            r.c().tick(r.w.clockMs);
            r.steam.pump(r.c());
        }
        CHECK(r.steam.sentIds<providers::SubscribeCommand>().empty());
        CHECK(r.job(kItem).state == AcquisitionState::Pending);

        r.metadata.answering = true;
        r.settle();
        CHECK(r.job(kItem).state == AcquisitionState::Installed);
        CHECK(r.steam.sentIds<providers::SubscribeCommand>() == std::vector<std::string>{kItem});
        CHECK(r.steam.sentIds<providers::DownloadCommand>() == std::vector<std::string>{kItem});
    }
    SECTION("held longer than Steam's response timeout") {
        r.options.identityTimeoutMs = 120'000;
        r.restart();
        r.acquire({workshopEntry(kItem, "Slow Web API")});
        r.steam.pump(r.c());
        for (int i = 0; i < 60; ++i) {
            r.w.clockMs += 1000;
            r.c().tick(r.w.clockMs);
            r.steam.pump(r.c());
        }
        CHECK(r.job(kItem).state == AcquisitionState::Pending); // no "did not answer" failure
        r.metadata.answering = true;
        r.settle();
        CHECK(r.job(kItem).state == AcquisitionState::Installed);
        CHECK(r.job(kItem).attempts == 1);
    }
    SECTION("rejected late: never subscribed, and Retry checks again") {
        r.metadata.items[kItem] = eu4Item(kItem);
        r.acquire({workshopEntry(kItem, "Wrong game")});
        r.steam.pump(r.c());
        CHECK(r.steam.sentIds<providers::SubscribeCommand>().empty());
        r.metadata.answering = true;
        r.settle();
        CHECK(r.job(kItem).state == AcquisitionState::Unavailable);
        CHECK(r.job(kItem).errorCode == "wrong_game");

        require(r.c().retry(r.job(kItem).id));
        CHECK(r.metadata.requests.size() == 2); // asked again, not subscribed blindly
        r.settle();
        CHECK(r.job(kItem).state == AcquisitionState::Unavailable);
        CHECK(r.job(kItem).errorCode == "wrong_game");
        CHECK(r.steam.sentIds<providers::SubscribeCommand>().empty());
        CHECK(r.steam.sentIds<providers::DownloadCommand>().empty());
    }
    SECTION("already subscribed elsewhere: no download is requested either") {
        r.metadata.items[kItem] = eu4Item(kItem);
        r.steam.items[kItem].flags = providers::item_flag::Subscribed;
        r.acquire({workshopEntry(kItem, "Wrong game")});
        r.steam.pump(r.c());
        r.metadata.answering = true;
        r.settle();
        CHECK(r.job(kItem).state == AcquisitionState::Unavailable);
        CHECK(r.steam.sentIds<providers::DownloadCommand>().empty());
    }
    SECTION("never answered: the job waits for the user and Retry asks again") {
        const std::int64_t id = r.acquire({workshopEntry(kItem, "Unconfirmed")});
        r.settle();
        const AcquisitionJob job = r.job(kItem);
        CHECK(job.state == AcquisitionState::NeedsUserAction);
        CHECK(job.errorCode == "timeout");
        CHECK_THAT(job.errorMessage, ContainsSubstring("could not confirm"));
        CHECK_THAT(job.errorMessage, ContainsSubstring("Stellaris"));
        CHECK(r.steam.sentIds<providers::SubscribeCommand>().empty());
        CHECK_FALSE(r.c().sessionOpen(kGame));
        const AcquisitionSnapshot snapshot = r.c().snapshot(kGame); // outlives `item`
        const JobSnapshot& item = r.snapshotJob(snapshot, kItem);
        CHECK(item.canRetry);
        CHECK(item.canOpenFallback);
        CHECK_FALSE(require(loadPlaysetView(r.w.ctx, id, r.c().liveJobs(kGame))).launch.canLaunch);

        r.metadata.answering = true;
        SECTION("Retry") {
            require(r.c().retry(job.id));
        }
        SECTION("Retry all") {
            CHECK(r.c().retryAllFailed(kGame).changed);
        }
        CHECK(r.metadata.requests.size() == 2);
        r.settle();
        CHECK(r.job(kItem).state == AcquisitionState::Installed);
        CHECK(r.steam.sentIds<providers::SubscribeCommand>() == std::vector<std::string>{kItem});
    }
    SECTION("without a Web API host nothing waits") {
        r.webApi = false;
        r.restart();
        r.acquire({workshopEntry(kItem, "No Web API")});
        r.steam.pump(r.c());
        CHECK(r.steam.sentIds<providers::SubscribeCommand>() == std::vector<std::string>{kItem});
        r.settle();
        CHECK(r.job(kItem).state == AcquisitionState::Installed);
        CHECK(r.metadata.requests.empty());
    }
}
