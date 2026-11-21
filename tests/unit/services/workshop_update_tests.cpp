// Updates and re-downloads of installed Workshop mods through the
// acquisition coordinator: the session's update check (Web API answers kept
// and compared), update jobs, re-download jobs, the identity gate, and what
// readiness, the Downloads snapshot and the launch check make of them.

#include "persistence/acquisition_job_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "services/acquisition_coordinator.hpp"
#include "services/launch_service.hpp"
#include "services/playset_view.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>

using namespace pmm;
using namespace pmm::services;
using namespace pmm::services::test;
using Catch::Matchers::ContainsSubstring;
using V = providers::WorkshopUpdateVerdict;

namespace {

constexpr const char* kItem = "2000000301";
constexpr const char* kOther = "2000000302";
constexpr std::uint32_t kOld = 1'700'000'000;
constexpr std::uint32_t kNew = 1'700'000'500;
constexpr std::uint32_t kSubInst =
    providers::item_flag::Subscribed | providers::item_flag::Installed;

// World + fake helper + fake Web API + coordinator, with one installed
// Workshop mod (kItem) whose installed version Steam recorded as kOld.
struct UpdateRig {
    World w;
    FakeSteam steam{w.workshop};
    FakeMetadata metadata;
    GameService games{w.ctx};
    CoordinatorOptions options;
    std::unique_ptr<AcquisitionCoordinator> coordinator;
    std::int64_t playsetId = 0;

    UpdateRig() {
        steam.acf = &w.adapter.acf;
        install(kItem, "Example Outdated");
        restart();
        playsetId = playset({workshopEntry(kItem, "Example Outdated")});
    }

    AcquisitionCoordinator& restart() {
        coordinator =
            std::make_unique<AcquisitionCoordinator>(w.ctx, games, steam, &metadata, options);
        return *coordinator;
    }
    AcquisitionCoordinator& c() { return *coordinator; }

    // On disk, in Steam's record (current as far as Steam knows), subscribed.
    void install(const std::string& id, const std::string& name) {
        w.addWorkshopMod(id, name);
        w.adapter.acf[id] = AcfRecord{kOld, "1100000000000000701", kOld, "1100000000000000701"};
        FakeSteam::Item& item = steam.items[id];
        item.flags = kSubInst;
        item.timestamp = kOld;
        item.latest = kNew;
        item.latestManifest = "1100000000000000702";
        item.name = name;
        w.refresh();
    }

    // Steam itself learnt of the newer version (latest_manifest differs).
    void flagInAcf(const std::string& id) {
        w.adapter.acf[id].latestTimeUpdated = kNew;
        w.adapter.acf[id].latestManifest = "1100000000000000702";
        steam.items[id].flags |= providers::item_flag::NeedsUpdate;
        w.refresh();
    }

    std::int64_t playset(std::vector<PlaysetEntry> entries) {
        persistence::PlaysetRepository repo(*w.db);
        Playset p;
        p.game = kGame;
        p.name = "Updates";
        p.entries = std::move(entries);
        return require(repo.create(p));
    }

    // The Web API's answer: the item exists for the game, last updated at `time`.
    providers::WorkshopItemMetadata answer(const std::string& id, std::int64_t time) {
        providers::WorkshopItemMetadata item = metadata.item(id);
        item.title = "Example Outdated";
        item.timeUpdated = time;
        item.contentManifest = "1100000000000000702";
        return item;
    }

    UpdateCheckReport check(std::vector<providers::WorkshopItemMetadata> answers) {
        const UpdateCheckPlan plan = require(c().prepareUpdateCheck(playsetId));
        return require(c().recordUpdateCheck(playsetId, answers, plan.ids.size()));
    }

    void settle(int maxSteps = 400) {
        for (int i = 0; i < maxSteps; ++i) {
            metadata.answer(c());
            steam.pump(c());
            if (!c().hasActiveWork(kGame) && steam.pending.empty())
                return;
            w.clockMs += 1000;
            c().tick(w.clockMs);
        }
        FAIL("acquisition did not settle");
    }

    AcquisitionJob job(const std::string& id) {
        persistence::AcquisitionJobRepository repo(*w.db);
        AcquisitionJob found;
        bool seen = false;
        for (const AcquisitionJob& j : require(repo.listForGame(kGame))) {
            if (j.identity.id == id && (!seen || j.id > found.id)) {
                found = j;
                seen = true;
            }
        }
        REQUIRE(seen);
        return found;
    }

    PlaysetView view() {
        const WorkshopFindings findings = c().workshopFindings(kGame);
        return require(loadPlaysetView(w.ctx, playsetId, c().liveJobs(kGame), &findings));
    }

    InstalledMod installed(const std::string& id) {
        persistence::InventoryRepository inventory(*w.db);
        auto mod = require(inventory.find({kGame, source::SteamWorkshop, id}));
        REQUIRE(mod.has_value());
        return *mod;
    }

    std::vector<std::string> sentKinds() const {
        std::vector<std::string> kinds;
        for (const providers::HelperCommand& command : steam.sent) {
            if (std::holds_alternative<providers::UnsubscribeCommand>(command))
                kinds.emplace_back("unsubscribe");
            else if (std::holds_alternative<providers::SubscribeCommand>(command))
                kinds.emplace_back("subscribe");
            else if (std::holds_alternative<providers::DownloadCommand>(command))
                kinds.emplace_back("download");
        }
        return kinds;
    }
};

const JobSnapshot* snapshotJob(const AcquisitionSnapshot& snapshot, const std::string& id) {
    for (const JobSnapshot& job : snapshot.jobs) {
        if (job.job.identity.id == id)
            return &job;
    }
    return nullptr;
}

} // namespace

TEST_CASE("The update check compares installed versions with the Workshop and keeps the answers",
          "[services][updates]") {
    UpdateRig r;
    r.install(kOther, "Example Current");
    r.w.addWorkshopMod("2000000303", "Example Folder Time"); // not in Steam's record
    r.w.refresh();
    persistence::PlaysetRepository playsets(*r.w.db);
    Playset p = require(playsets.get(r.playsetId));
    p.entries.push_back(workshopEntry(kOther, "Example Current", false)); // disabled count too
    p.entries.push_back(workshopEntry("2000000303", "Example Folder Time"));
    p.entries.push_back(workshopEntry("2000000304", "Example Missing")); // not installed
    p.entries.push_back(localEntry("example_local", "Local"));
    requireOk(playsets.saveEntries(p.id, p.entries));

    const UpdateCheckPlan plan = require(r.c().prepareUpdateCheck(r.playsetId));
    CHECK(plan.game == kGame);
    CHECK(plan.installed == 3);
    CHECK(plan.ids == std::vector<std::string>{kItem, kOther, "2000000303"}); // ids only

    const UpdateCheckReport report = require(r.c().recordUpdateCheck(
        r.playsetId, {r.answer(kItem, kNew), r.answer(kOther, kOld), r.answer("2000000303", kNew)},
        plan.ids.size()));
    CHECK(report.update.changed);
    REQUIRE(report.items.size() == 3);
    CHECK(report.items[0].verdict == V::UpdateAvailable);
    CHECK(report.items[0].updateAvailable);
    CHECK(report.items[0].installedTimeUpdated == kOld);
    CHECK(report.items[0].workshopTimeUpdated == kNew);
    CHECK(report.items[0].workshopManifest == "1100000000000000702");
    CHECK(report.items[0].installedManifest == "1100000000000000701");
    CHECK(report.items[1].verdict == V::UpToDate);
    CHECK_FALSE(report.items[1].enabled);
    CHECK(report.items[2].verdict == V::NotCompared); // folder time: never compared
    CHECK_FALSE(report.items[2].updateAvailable);
    CHECK(report.checked == 3);
    CHECK(report.updates == 1);
    CHECK(report.notCompared == 1);
    CHECK(report.reachable);
    CHECK(report.summary == "Checked 3 Workshop mods: 1 has an update. 1 could not be compared: "
                            "Steam's records do not say which version is installed.");
    CHECK(r.metadata.requests.empty()); // the caller asks; the coordinator only records

    // Rows: the outdated one is Ready with an update; nothing blocks.
    const PlaysetView view = r.view();
    CHECK(view.rows[0].readiness.status == EntryStatus::Ready);
    CHECK(view.rows[0].readiness.updateAvailable);
    CHECK_FALSE(view.rows[1].readiness.updateAvailable);
    CHECK(view.counts.updates == 1);
    CHECK(view.counts.updatesEnabled == 1);
    CHECK(view.counts.warning == 0);

    // Kept for the session: asked again only after updateCheckMaxAgeMs.
    CHECK(require(r.c().prepareUpdateCheck(r.playsetId)).ids.empty());
    CHECK(require(r.c().updateCheckReport(r.playsetId)).updates == 1);
    r.w.clockMs += r.options.updateCheckMaxAgeMs;
    CHECK(require(r.c().prepareUpdateCheck(r.playsetId)).ids.size() == 3);
}

TEST_CASE("The update check reports unreachable, partial and refused answers",
          "[services][updates]") {
    UpdateRig r;
    SECTION("nothing answered") {
        const UpdateCheckReport report = r.check({});
        CHECK_FALSE(report.reachable);
        CHECK(report.unanswered == 1);
        CHECK_THAT(report.summary, ContainsSubstring("could not be reached"));
        CHECK(require(r.c().prepareUpdateCheck(r.playsetId)).ids.size() == 1); // asks again
    }
    SECTION("all current") {
        const UpdateCheckReport report = r.check({r.answer(kItem, kOld)});
        CHECK(report.summary == "The Workshop mod is up to date.");
    }
    SECTION("removed from the Workshop: shown on the row, never updated") {
        providers::WorkshopItemMetadata gone;
        gone.id = kItem;
        gone.resultCode = 9;
        r.flagInAcf(kItem); // even when Steam's own record has a newer version
        const UpdateCheckReport report = r.check({gone});
        CHECK(report.refused == 1);
        CHECK(report.updates == 0);
        CHECK_THAT(report.summary, ContainsSubstring("no longer available on the Workshop"));
        const PlaysetView view = r.view();
        CHECK(view.rows[0].readiness.status == EntryStatus::Ready);
        CHECK_FALSE(view.rows[0].readiness.updateAvailable);
        REQUIRE(view.rows[0].readiness.warnings.size() == 1);
        CHECK_THAT(view.rows[0].readiness.warnings[0],
                   ContainsSubstring("installed copy stays as it is"));
        const UpdateOutcome outcome = require(r.c().acquireUpdates(r.playsetId, {kItem}));
        CHECK(outcome.jobIds.empty());
        REQUIRE(outcome.skipped.size() == 1);
        CHECK_THAT(outcome.skipped[0].reason, ContainsSubstring("not found"));
        const auto redo = r.c().reinstall(r.playsetId, kItem);
        REQUIRE_FALSE(redo);
        CHECK(redo.error().code == ErrorCode::ItemUnavailable);
        CHECK_THAT(redo.error().message, ContainsSubstring("It was not unsubscribed"));
        CHECK(r.steam.opens == 0);
    }
}

TEST_CASE("An update found by the check is downloaded, verified and rescanned",
          "[services][updates]") {
    UpdateRig r;
    r.check({r.answer(kItem, kNew)});
    REQUIRE(r.view().rows[0].readiness.updateAvailable);
    LaunchService launcher(r.w.ctx, &r.c());
    const LaunchCheck before = launcher.check(kGame, r.playsetId, {false});
    CHECK(before.canLaunch); // an update available never blocks
    CHECK(std::find(before.warnings.begin(), before.warnings.end(),
                    "1 enabled mod has an update on the Workshop.") != before.warnings.end());

    const UpdateOutcome outcome = require(r.c().acquireUpdates(r.playsetId));
    REQUIRE(outcome.jobIds.size() == 1);
    CHECK(outcome.skipped.empty());
    CHECK(outcome.message == "Updating \"Example Outdated\" through Steam.");
    const AcquisitionJob queued = r.job(kItem);
    CHECK(queued.kind == AcquisitionKind::Update);
    CHECK(queued.targetTimeUpdated == kNew);
    CHECK(queued.targetManifest == "1100000000000000702");

    // While it runs, the row waits and Play is blocked.
    r.steam.items[kItem].steps = 4;
    r.steam.pump(r.c());
    CHECK(r.view().rows[0].readiness.status != EntryStatus::Ready);
    CHECK_FALSE(launcher.check(kGame, r.playsetId, {false}).canLaunch);
    const AcquisitionSnapshot running = r.c().snapshot(kGame);
    REQUIRE(snapshotJob(running, kItem) != nullptr);
    CHECK(running.summary == "1 updating");

    r.settle();
    const AcquisitionJob done = r.job(kItem);
    CHECK(done.state == AcquisitionState::Installed);
    CHECK(r.sentKinds() == std::vector<std::string>{"download"}); // never subscribed
    CHECK(r.installed(kItem).contentUpdatedAt == kNew);           // rescanned
    const PlaysetView after = r.view();
    CHECK(after.rows[0].readiness.status == EntryStatus::Ready);
    CHECK_FALSE(after.rows[0].readiness.updateAvailable);
    CHECK(after.counts.updates == 0);
    CHECK(launcher.check(kGame, r.playsetId, {false}).canLaunch);
    CHECK(r.c().snapshot(kGame).jobs.size() == 1);
    CHECK(snapshotJob(r.c().snapshot(kGame), kItem)->stateText == "Updated");
}

TEST_CASE("An update Steam flagged works without the Web API", "[services][updates]") {
    UpdateRig r;
    r.metadata.answering = false; // the identity gate lets a flagged update pass
    r.flagInAcf(kItem);
    CHECK(r.view().rows[0].readiness.updateAvailable); // no network involved
    const UpdateOutcome outcome = require(r.c().acquireUpdates(r.playsetId, {kItem}));
    REQUIRE(outcome.jobIds.size() == 1);
    CHECK(r.job(kItem).targetTimeUpdated == kNew);
    r.settle();
    CHECK(r.job(kItem).state == AcquisitionState::Installed);
    CHECK(r.installed(kItem).state == InstallState::Ready);
    CHECK_FALSE(r.view().rows[0].readiness.updateAvailable);
}

TEST_CASE("A current item is not downloaded", "[services][updates]") {
    UpdateRig r;
    r.check({r.answer(kItem, kOld)});
    const UpdateOutcome all = require(r.c().acquireUpdates(r.playsetId));
    CHECK(all.jobIds.empty());
    CHECK(all.message == "No mod of this playset has a known update.");
    const UpdateOutcome one = require(r.c().acquireUpdates(r.playsetId, {kItem, "2000000399"}));
    CHECK(one.jobIds.empty());
    REQUIRE(one.skipped.size() == 2);
    CHECK_THAT(one.skipped[0].reason, ContainsSubstring("No newer version is known"));
    CHECK_THAT(one.skipped[1].reason, ContainsSubstring("not a Workshop mod of this playset"));
    CHECK(r.steam.opens == 0);
    persistence::AcquisitionJobRepository repo(*r.w.db);
    CHECK(require(repo.listForGame(kGame)).empty());
}

TEST_CASE("An update Steam does not pick up fails, stays visible and offers a re-download",
          "[services][updates]") {
    UpdateRig r;
    r.steam.items[kItem].seesUpdate = false; // DownloadItem "installs nothing"
    r.check({r.answer(kItem, kNew)});
    require(r.c().acquireUpdates(r.playsetId));
    r.settle();
    const AcquisitionJob job = r.job(kItem);
    CHECK(job.state == AcquisitionState::Failed);
    CHECK(job.errorCode == "update_not_seen");
    CHECK(job.errorMessage == providers::kUpdateNotSeenMessage);
    CHECK(r.sentKinds() == std::vector<std::string>{"download", "download"}); // asked twice
    CHECK(fs::exists(r.w.workshop / kItem / "descriptor.mod"));               // nothing deleted

    // Still listed in Downloads although the mod is installed and usable.
    const AcquisitionSnapshot snapshot = r.c().snapshot(kGame);
    const JobSnapshot* item = snapshotJob(snapshot, kItem);
    REQUIRE(item != nullptr);
    CHECK(item->canRetry);
    CHECK(item->canReinstall);
    CHECK(snapshot.summary == "1 failed");
    // The row stays Ready with a warning; Play is not blocked.
    const PlaysetView view = r.view();
    CHECK(view.rows[0].readiness.status == EntryStatus::Ready);
    CHECK(view.rows[0].readiness.updateAvailable);
    REQUIRE(view.rows[0].readiness.warnings.size() == 1);
    CHECK(view.rows[0].readiness.warnings[0] ==
          std::string("Update failed: ") + std::string(providers::kUpdateNotSeenMessage));
    LaunchService launcher(r.w.ctx, &r.c());
    CHECK(launcher.check(kGame, r.playsetId, {false}).canLaunch);

    // After a restart the failed update is still listed.
    r.c().shutdown();
    r.restart();
    REQUIRE(snapshotJob(r.c().snapshot(kGame), kItem) != nullptr);

    // Steam notices the update; Retry fetches it.
    r.steam.items[kItem].seesUpdate = true;
    r.check({r.answer(kItem, kNew)});
    require(r.c().retry(job.id));
    r.settle();
    CHECK(r.job(kItem).state == AcquisitionState::Installed);
}

TEST_CASE("Updates and re-downloads are refused while the game runs", "[services][updates]") {
    UpdateRig r;
    r.check({r.answer(kItem, kNew)});
    r.w.processes.push_back({1234, kExeName, r.w.executable});

    const auto update = r.c().acquireUpdates(r.playsetId);
    REQUIRE_FALSE(update);
    CHECK(update.error().code == ErrorCode::ConflictingProcess);
    CHECK_THAT(update.error().message, ContainsSubstring("Stellaris is running"));
    CHECK_THAT(update.error().message, ContainsSubstring("Close it before updating mods"));
    const auto redo = r.c().reinstall(r.playsetId, kItem);
    REQUIRE_FALSE(redo);
    CHECK(redo.error().code == ErrorCode::ConflictingProcess);
    persistence::AcquisitionJobRepository repo(*r.w.db);
    CHECK(require(repo.listForGame(kGame)).empty());

    // A stored update job waits until the game is closed.
    persistence::AcquisitionJobRepository::NewJob kind{AcquisitionKind::Update, kNew, {}};
    const AcquisitionJob stored =
        require(repo.createOrReuse({kGame, source::SteamWorkshop, kItem}, r.playsetId, kind));
    r.c().start(kGame);
    CHECK(r.c().liveJobs(kGame).empty());
    CHECK(r.steam.opens == 0);
    CHECK(r.c().updatesInProgress(kGame).size() == 1); // Play waits as well
    LaunchService launcher(r.w.ctx, &r.c());
    CHECK_FALSE(launcher.check(kGame, r.playsetId, {false}).canLaunch);

    // A failed update cannot be retried while the game runs either.
    r.w.processes.pop_back();
    r.steam.items[kItem].acceptDownload = false;
    r.c().start(kGame);
    r.settle();
    REQUIRE(r.job(kItem).state == AcquisitionState::Failed);
    r.w.processes.push_back({1234, kExeName, r.w.executable});
    const auto retried = r.c().retry(stored.id);
    REQUIRE_FALSE(retried);
    CHECK(retried.error().code == ErrorCode::ConflictingProcess);
}

TEST_CASE("Play waits for an update of a disabled mod", "[services][updates]") {
    UpdateRig r;
    r.install(kOther, "Example Disabled");
    persistence::PlaysetRepository playsets(*r.w.db);
    Playset p = require(playsets.get(r.playsetId));
    p.entries.push_back(workshopEntry(kOther, "Example Disabled", false));
    requireOk(playsets.saveEntries(p.id, p.entries));
    r.check({r.answer(kItem, kOld), r.answer(kOther, kNew)});
    r.steam.items[kOther].steps = 6;
    require(r.c().acquireUpdates(r.playsetId)); // disabled mods are updated too
    REQUIRE(r.job(kOther).kind == AcquisitionKind::Update);
    r.steam.pump(r.c());
    LaunchService launcher(r.w.ctx, &r.c());
    const LaunchCheck check = launcher.check(kGame, r.playsetId, {false});
    CHECK_FALSE(check.canLaunch);
    REQUIRE(check.blockers.size() == 1);
    CHECK(check.blockers[0] == "1 other Workshop mod of Stellaris is being updated or downloaded "
                               "again. Wait until that finishes, or cancel it in Downloads, then "
                               "play.");
    r.settle();
    CHECK(launcher.check(kGame, r.playsetId, {false}).canLaunch);
}

TEST_CASE("A re-download unsubscribes, subscribes again and downloads", "[services][updates]") {
    UpdateRig r;
    r.steam.items[kItem].removeOnUnsubscribe = true;
    const UpdateOutcome outcome = require(r.c().reinstall(r.playsetId, kItem));
    REQUIRE(outcome.jobIds.size() == 1);
    CHECK_THAT(outcome.message, ContainsSubstring("Steam unsubscribes it"));
    CHECK(r.job(kItem).kind == AcquisitionKind::Reinstall);
    CHECK(r.metadata.requests.size() == 1); // confirmed by the Web API first
    r.settle();
    const AcquisitionJob job = r.job(kItem);
    CHECK(job.state == AcquisitionState::Installed);
    CHECK(job.reinstallStep == ReinstallStep::Resubscribed);
    CHECK(r.sentKinds() == std::vector<std::string>{"unsubscribe", "subscribe", "download"});
    CHECK(fs::exists(r.w.workshop / kItem / "descriptor.mod")); // downloaded again
    CHECK(r.installed(kItem).contentUpdatedAt == kNew);
    CHECK(r.view().rows[0].readiness.status == EntryStatus::Ready);
    CHECK(snapshotJob(r.c().snapshot(kGame), kItem)->stateText == "Downloaded again");
}

TEST_CASE("A re-download that fails after the unsubscribe says the mod is unsubscribed",
          "[services][updates]") {
    UpdateRig r;
    r.steam.items[kItem].removeOnUnsubscribe = true;
    r.steam.items[kItem].subscribeResult = 16; // timeout, every time
    require(r.c().reinstall(r.playsetId, kItem));
    r.settle();
    const AcquisitionJob failed = r.job(kItem);
    CHECK(failed.state == AcquisitionState::Failed);
    CHECK(failed.reinstallStep == ReinstallStep::Unsubscribed);
    CHECK_THAT(failed.errorMessage,
               ContainsSubstring(std::string(kItem) + " is now unsubscribed in Steam: Retry "
                                                      "subscribes to it again"));
    // The files are gone: after a rescan the entry is not ready, with the reason.
    require(r.games.rescanMods(kGame));
    const PlaysetView view = r.view();
    CHECK(view.rows[0].readiness.status == EntryStatus::Failed);
    CHECK_THAT(view.rows[0].readiness.detail, ContainsSubstring("now unsubscribed"));
    const JobSnapshot* item = snapshotJob(r.c().snapshot(kGame), kItem);
    REQUIRE(item != nullptr);
    CHECK(item->canRetry);

    // Retry subscribes again; it never unsubscribes a second time.
    r.steam.items[kItem].subscribeResult = 1;
    require(r.c().retry(failed.id));
    r.settle();
    CHECK(r.job(kItem).state == AcquisitionState::Installed);
    const std::vector<std::string> kinds = r.sentKinds();
    CHECK(std::count(kinds.begin(), kinds.end(), "unsubscribe") == 1);
    CHECK(kinds.back() == "download");
}

TEST_CASE("A cancelled re-download names the mod it left unsubscribed", "[services][updates]") {
    UpdateRig r;
    r.steam.items[kItem].subscribeResult = 16;
    require(r.c().reinstall(r.playsetId, kItem));
    for (int i = 0; i < 20 && r.job(kItem).reinstallStep != ReinstallStep::Unsubscribed; ++i) {
        r.metadata.answer(r.c());
        r.steam.pump(r.c());
        r.w.clockMs += 1000;
        r.c().tick(r.w.clockMs);
    }
    REQUIRE(r.job(kItem).reinstallStep == ReinstallStep::Unsubscribed);
    const CancelOutcome outcome = r.c().cancelAll(kGame);
    CHECK(outcome.cancelled == 1);
    CHECK_THAT(outcome.message, ContainsSubstring("\"Example Outdated\" was unsubscribed for a "
                                                  "re-download and is not subscribed again"));
    CHECK_THAT(r.job(kItem).errorMessage,
               ContainsSubstring("was unsubscribed for the re-download"));
}

TEST_CASE("A re-download waits for the Web API before unsubscribing", "[services][updates]") {
    UpdateRig r;
    r.metadata.answering = false;
    require(r.c().reinstall(r.playsetId, kItem));
    r.settle();
    const AcquisitionJob job = r.job(kItem);
    CHECK(job.state == AcquisitionState::NeedsUserAction);
    CHECK(job.errorCode == "timeout");
    CHECK_THAT(job.errorMessage, ContainsSubstring("did not unsubscribe or download it again"));
    CHECK(r.sentKinds().empty());

    SECTION("without a Web API host it is refused") {
        r.coordinator =
            std::make_unique<AcquisitionCoordinator>(r.w.ctx, r.games, r.steam, nullptr, r.options);
        const auto refused = r.c().reinstall(r.playsetId, kItem);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().code == ErrorCode::ProviderUnavailable);
    }
    SECTION("an update nobody flagged waits for it too, with its own wording") {
        persistence::AcquisitionJobRepository repo(*r.w.db);
        persistence::AcquisitionJobRepository::NewJob kind{AcquisitionKind::Update, kNew, {}};
        require(repo.createOrReuse({kGame, source::SteamWorkshop, kOther}, r.playsetId, kind));
        r.install(kOther, "Example Unflagged");
        r.c().start(kGame);
        r.settle();
        CHECK(r.job(kOther).state == AcquisitionState::NeedsUserAction);
        CHECK_THAT(r.job(kOther).errorMessage,
                   ContainsSubstring("did not ask Steam to download the update"));
        CHECK(r.sentKinds().empty());
    }
}

TEST_CASE("A re-download is refused while another job for the mod runs", "[services][updates]") {
    UpdateRig r;
    r.check({r.answer(kItem, kNew)});
    r.steam.items[kItem].steps = 50;
    require(r.c().acquireUpdates(r.playsetId));
    r.steam.pump(r.c());
    const auto refused = r.c().reinstall(r.playsetId, kItem);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == ErrorCode::AlreadyExists);
    const auto notInstalled = r.c().reinstall(r.playsetId, "2000000399");
    REQUIRE_FALSE(notInstalled);
    CHECK(notInstalled.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("A failed update can be downloaded again from its job", "[services][updates]") {
    UpdateRig r;
    r.steam.items[kItem].seesUpdate = false;
    r.check({r.answer(kItem, kNew)});
    require(r.c().acquireUpdates(r.playsetId));
    r.settle();
    const AcquisitionJob failed = r.job(kItem);
    REQUIRE(failed.errorCode == "update_not_seen");

    // A fresh subscription makes Steam fetch the newest version.
    r.steam.items[kItem].seesUpdate = true;
    const UpdateOutcome outcome = require(r.c().reinstallJob(failed.id));
    REQUIRE(outcome.jobIds.size() == 1);
    CHECK(outcome.jobIds[0] != failed.id);
    const AcquisitionJob redo = r.job(kItem);
    CHECK(redo.kind == AcquisitionKind::Reinstall);
    CHECK(redo.targetTimeUpdated == kNew); // the version the update wanted
    r.settle();
    CHECK(r.job(kItem).state == AcquisitionState::Installed);
    CHECK(r.installed(kItem).contentUpdatedAt == kNew);
    // Only the newest job per mod is listed.
    const AcquisitionSnapshot snapshot = r.c().snapshot(kGame);
    REQUIRE(snapshot.jobs.size() == 1);
    CHECK(snapshot.jobs[0].job.id == redo.id);
}

TEST_CASE("A stored re-download resumes at its step after a restart", "[services][updates]") {
    UpdateRig r;
    persistence::AcquisitionJobRepository repo(*r.w.db);
    persistence::AcquisitionJobRepository::NewJob kind{AcquisitionKind::Reinstall, 0, {}};
    AcquisitionJob job =
        require(repo.createOrReuse({kGame, source::SteamWorkshop, kItem}, r.playsetId, kind));
    job.state = AcquisitionState::Unsubscribing;
    job.reinstallStep = ReinstallStep::Unsubscribed;
    job.attempts = 1;
    requireOk(repo.update(job));
    r.steam.items[kItem].flags = providers::item_flag::Installed; // unsubscribed meanwhile
    r.restart().start(kGame);
    r.settle();
    CHECK(r.job(kItem).state == AcquisitionState::Installed);
    CHECK(r.sentKinds() == std::vector<std::string>{"subscribe", "download"});
}
