#include "persistence/launch_history_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "services/acquisition_coordinator.hpp"
#include "services/launch_service.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <utility>
#include <vector>

using namespace pmm;
using namespace pmm::services;
using namespace pmm::services::test;
using Catch::Matchers::ContainsSubstring;

namespace {

std::int64_t makePlayset(World& w, std::vector<PlaysetEntry> entries,
                         const std::string& name = "Main") {
    persistence::PlaysetRepository repo(*w.db);
    Playset p;
    p.game = kGame;
    p.name = name;
    p.entries = std::move(entries);
    return require(repo.create(p));
}

// Deliberately non-default order with a disabled entry in the middle.
std::vector<PlaysetEntry> mixedEntries() {
    return {workshopEntry("2000000002", "Patch"),
            localEntry("example_local_tweaks", "Tweaks", false),
            workshopEntry("2000000001", "UI Overhaul"), workshopEntry("2000000003", "Old Era")};
}

constexpr const char* kTranslatedOrder =
    "{\"enabled_mods\":[\"mod/ugc_2000000002.mod\",\"mod/ugc_2000000001.mod\","
    "\"mod/ugc_2000000003.mod\"],\"disabled_dlcs\":[]}";

fs::path configFile(const World& w) {
    return w.userData / "dlc_load.json";
}

// The configuration the official launcher left behind.
std::string installPreviousConfiguration(World& w) {
    const std::string previous = readText(fixture("dlc_load.json"));
    writeText(configFile(w), previous);
    return previous;
}

bool contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines) {
        if (line.find(text) != std::string::npos)
            return true;
    }
    return false;
}

std::vector<persistence::LaunchRecord> launches(World& w) {
    persistence::LaunchHistoryRepository history(*w.db);
    return require(history.latest(kGame, 10));
}

// Leaves a configuration change "applying", as a crash after the first write would.
void interruptChange(World& w, const std::string& label, const fs::path& target) {
    writeText(target, "original");
    platform::FileTransactionFaults crash;
    crash.crashAfterWrites = std::size_t{1};
    w.transactions.setFaultsForTesting(crash);
    REQUIRE_FALSE(w.transactions.apply(label, {platform::FileWrite{target, "changed"}}));
    w.transactions.setFaultsForTesting({});
    REQUIRE_FALSE(require(w.transactions.listIncomplete()).empty());
}

// A playset whose disabled Workshop entry is still downloading in the
// background (disabled entries never block a launch).
struct AcquiringRig {
    World w;
    FakeSteam steam{w.workshop};
    GameService games{w.ctx};
    AcquisitionCoordinator coordinator{w.ctx, games, steam, nullptr};
    std::int64_t playsetId = 0;

    AcquiringRig() {
        w.installFixtureMods();
        w.refresh();
        steam.items["2000000444"].steps = 4;
        playsetId = makePlayset(w, {workshopEntry("2000000001", "UI Overhaul"),
                                    workshopEntry("2000000444", "Downloading", false)});
        require(coordinator.acquireMissing(playsetId));
        steam.pump(coordinator);
        REQUIRE(coordinator.sessionOpen(kGame));
        REQUIRE(coordinator.hasActiveWork(kGame));
        REQUIRE(coordinator.liveJobs(kGame).size() == 1);
    }
};

} // namespace

TEST_CASE("Launching writes the load order through a transaction and starts the exact process",
          "[services][launch]") {
    World w;
    w.installFixtureMods();
    const std::string previous = installPreviousConfiguration(w);
    w.refresh();
    const std::int64_t id = makePlayset(w, mixedEntries());
    LaunchService launcher(w.ctx, nullptr);

    const LaunchCheck check = launcher.check(kGame, id);
    CHECK(check.canLaunch);
    CHECK(check.blockers.empty());
    CHECK(check.readiness.enabledCount == 3);
    CHECK_FALSE(check.interruptedTransaction);
    CHECK_FALSE(check.runningInstance);
    CHECK(w.started.empty()); // checking never starts or writes anything
    CHECK(readText(configFile(w)) == previous);

    const LaunchOutcome outcome = require(launcher.launch(kGame, id));
    CHECK(outcome.pid == 4242);

    // Exactly one process, started from the plan: argv vector, folder, environment.
    REQUIRE(w.started.size() == 1);
    const platform::ProcessSpec& spec = w.started[0];
    CHECK(spec.executable == w.executable);
    CHECK(spec.workingDirectory == w.install);
    CHECK(spec.arguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(spec.environment ==
          std::vector<std::pair<std::string, std::string>>{{"SteamAppId", kAppId}});

    // Enabled mods only, in playset order; the disabled local mod is left out.
    REQUIRE(w.adapter.launchRequests.size() == 1);
    CHECK(w.adapter.launchRequests[0] ==
          std::vector<ModIdentity>{{kGame, source::SteamWorkshop, "2000000002"},
                                   {kGame, source::SteamWorkshop, "2000000001"},
                                   {kGame, source::SteamWorkshop, "2000000003"}});
    CHECK(readText(configFile(w)) == kTranslatedOrder);

    // The write went through a committed transaction holding a verified backup.
    REQUIRE_FALSE(outcome.transactionId.empty());
    const platform::TransactionRecord record = require(w.transactions.load(outcome.transactionId));
    CHECK(record.state == platform::TransactionState::Committed);
    CHECK(record.label == LaunchService::transactionLabel(kGame, id, "Main"));
    CHECK_THAT(record.label, ContainsSubstring("stellaris") && ContainsSubstring("Main"));
    REQUIRE(record.entries.size() == 1);
    CHECK(record.entries[0].target == configFile(w));
    CHECK(record.entries[0].existed);
    CHECK(readText(record.directory / record.entries[0].backupFile) == previous);

    const std::vector<persistence::LaunchRecord> history = launches(w);
    REQUIRE(history.size() == 1);
    CHECK(history[0].outcome == "started");
    CHECK(history[0].pid == std::optional<std::int64_t>(4242));
    CHECK(history[0].playsetId == std::optional<std::int64_t>(id));
    CHECK(history[0].transactionId == outcome.transactionId);
    CHECK(outcome.historyId == history[0].id);

    // "Restore pre-launch configuration" puts the launcher's file back.
    const auto restorable = require(launcher.lastRestorableTransaction(kGame));
    REQUIRE(restorable);
    CHECK(restorable->id == outcome.transactionId);
    CHECK_FALSE(require(launcher.lastRestorableTransaction("eu4")));
    const platform::TransactionRecord restored =
        require(launcher.restoreTransaction(outcome.transactionId));
    CHECK(restored.state == platform::TransactionState::Restored);
    CHECK(readText(configFile(w)) == previous);
    CHECK_FALSE(require(launcher.lastRestorableTransaction(kGame)));
}

TEST_CASE("Only enabled entries block a launch", "[services][launch]") {
    World w;
    w.installFixtureMods();
    w.refresh();
    LaunchService launcher(w.ctx, nullptr);

    SECTION("a missing enabled entry blocks, and nothing is written or started") {
        const std::int64_t id = makePlayset(w, {workshopEntry("2000000001", "UI Overhaul"),
                                                workshopEntry("2000000999", "Gone Mod")});
        const LaunchCheck check = launcher.check(kGame, id);
        CHECK_FALSE(check.canLaunch);
        REQUIRE(check.readiness.blockers.size() == 1);
        CHECK(check.readiness.blockers[0].index == 1);
        CHECK(contains(check.blockers, "Gone Mod"));

        const auto refused = launcher.launch(kGame, id);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().code == ErrorCode::ValidationFailed);
        CHECK_THAT(refused.error().message, ContainsSubstring("Gone Mod"));
        CHECK(w.started.empty());
        CHECK(w.adapter.launchRequests.empty());
        CHECK_FALSE(fs::exists(configFile(w)));
        CHECK(require(w.transactions.list()).empty());
    }
    SECTION("missing disabled entries stay visible as a warning but do not block") {
        const std::int64_t id = makePlayset(w, {workshopEntry("2000000001", "UI Overhaul"),
                                                workshopEntry("2000000999", "Gone Mod", false),
                                                localEntry("theirs", "Theirs", false)});
        const LaunchCheck check = launcher.check(kGame, id);
        CHECK(check.canLaunch);
        CHECK(check.readiness.unresolvedDisabledCount == 2);
        CHECK(contains(check.warnings, "2 disabled entries are not available"));

        require(launcher.launch(kGame, id));
        CHECK(readText(configFile(w)) ==
              "{\"enabled_mods\":[\"mod/ugc_2000000001.mod\"],\"disabled_dlcs\":[]}");
    }
    SECTION("a playset without enabled mods launches with a warning") {
        const std::int64_t id = makePlayset(w, {workshopEntry("2000000001", "UI Overhaul", false)});
        const LaunchCheck check = launcher.check(kGame, id);
        CHECK(check.canLaunch);
        CHECK(contains(check.warnings, "No mods are enabled"));
        require(launcher.launch(kGame, id));
        CHECK(w.adapter.launchRequests.back().empty());
    }
    SECTION("a playset of another game or a deleted playset blocks") {
        persistence::PlaysetRepository repo(*w.db);
        Playset eu4;
        eu4.game = "eu4";
        eu4.name = "EU4";
        const std::int64_t other = require(repo.create(eu4));
        CHECK(contains(launcher.check(kGame, other).blockers, "belongs to"));
        const std::int64_t gone = makePlayset(w, {});
        require(repo.remove(gone));
        CHECK(contains(launcher.check(kGame, gone).blockers, "no longer exists"));
    }
}

TEST_CASE("A running game or Paradox launcher refuses the launch", "[services][launch]") {
    World w;
    w.installFixtureMods();
    w.refresh();
    const std::int64_t id = makePlayset(w, {workshopEntry("2000000001", "UI Overhaul")});
    LaunchService launcher(w.ctx, nullptr);

    SECTION("the game is already running") {
        w.processes = {{1234, kExeName, w.executable}, {7, "explorer.exe", {}}};
        const LaunchCheck check = launcher.check(kGame, id);
        CHECK_FALSE(check.canLaunch);
        CHECK(check.runningInstance);
        CHECK(contains(check.blockers, "already running"));
        CHECK(contains(check.blockers, "1234"));
    }
    SECTION("the Paradox launcher could rewrite the configuration") {
        w.processes = {{99, kLauncherName, {}}};
        const LaunchCheck check = launcher.check(kGame, id);
        CHECK_FALSE(check.canLaunch);
        CHECK(check.runningInstance);
        CHECK(contains(check.blockers, "Paradox launcher"));
    }
    const auto refused = launcher.launch(kGame, id);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == ErrorCode::ConflictingProcess);
    CHECK(w.started.empty());
    CHECK_FALSE(fs::exists(configFile(w)));
}

TEST_CASE("Read-only checks skip only the write probes", "[services][launch]") {
    World w;
    w.installFixtureMods();
    w.refresh();
    const std::int64_t id = makePlayset(w, {workshopEntry("2000000001", "UI Overhaul")});
    LaunchService launcher(w.ctx, nullptr);
    LaunchCheckOptions readOnly;
    readOnly.probeWriteAccess = false;
    CHECK(launcher.check(kGame, id, readOnly).blockers == launcher.check(kGame, id).blockers);

    // A file where the mod folder should be fails the write probe.
    fs::remove_all(w.userData / "mod");
    writeText(w.userData / "mod", "not a folder");
    const LaunchCheck probed = launcher.check(kGame, id);
    CHECK_FALSE(probed.canLaunch);
    CHECK(contains(probed.blockers, "mod folder"));

    const LaunchCheck unprobed = launcher.check(kGame, id, readOnly);
    CHECK(unprobed.canLaunch);
    CHECK_FALSE(contains(unprobed.blockers, "mod folder"));
    CHECK(unprobed.readiness.canLaunch);

    // Every other blocker is still reported.
    w.processes = {{1234, kExeName, w.executable}};
    const LaunchCheck running = launcher.check(kGame, id, readOnly);
    CHECK_FALSE(running.canLaunch);
    CHECK(running.runningInstance);
    CHECK(contains(running.blockers, "already running"));
    CHECK_FALSE(contains(running.blockers, "mod folder"));

    // launch() always probes.
    w.processes.clear();
    CHECK_FALSE(launcher.launch(kGame, id));
    CHECK(w.started.empty());
}

TEST_CASE("Process checks that cannot run only warn", "[services][launch]") {
    World w;
    w.installFixtureMods();
    w.refresh();
    const std::int64_t id = makePlayset(w, {workshopEntry("2000000001", "UI Overhaul")});
    std::string expected;
    SECTION("the lister fails") {
        w.ctx.listProcesses = []() -> Result<std::vector<platform::RunningProcess>> {
            return makeError(ErrorCode::PermissionDenied, "Process list denied.");
        };
        expected = "Process list denied";
    }
    SECTION("no lister is configured") {
        w.ctx.listProcesses = nullptr;
        expected = "could not be checked";
    }
    LaunchService launcher(w.ctx, nullptr);
    const LaunchCheck check = launcher.check(kGame, id);
    CHECK(check.canLaunch);
    CHECK(contains(check.warnings, expected));
}

TEST_CASE("A process start failure restores the previous configuration", "[services][launch]") {
    World w;
    w.installFixtureMods();
    std::optional<std::string> previous;
    SECTION("the launcher's configuration existed") {
        previous = installPreviousConfiguration(w);
    }
    SECTION("there was no configuration file yet") {}
    w.refresh();
    const std::int64_t id = makePlayset(w, mixedEntries());
    w.startFailure = makeError(ErrorCode::ProcessStartFailed, "The game could not be started.",
                               "CreateProcessW failed: Access is denied. (5)");
    LaunchService launcher(w.ctx, nullptr);

    const auto failed = launcher.launch(kGame, id);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code == ErrorCode::ProcessStartFailed);
    CHECK_THAT(failed.error().message,
               ContainsSubstring("previous game configuration was restored"));
    // Diagnostics name what was attempted and why it failed.
    const std::string& detail = failed.error().detail;
    CHECK_THAT(detail, ContainsSubstring(platform::pathToUtf8(w.executable)));
    CHECK_THAT(detail, ContainsSubstring(platform::pathToUtf8(w.install)));
    CHECK_THAT(detail, ContainsSubstring("\"-gdpr-compliant\""));
    CHECK_THAT(detail, ContainsSubstring("SteamAppId=281990"));
    CHECK_THAT(detail, ContainsSubstring("Access is denied"));
    REQUIRE(w.started.size() == 1);

    if (previous)
        CHECK(readText(configFile(w)) == *previous);
    else
        CHECK_FALSE(fs::exists(configFile(w)));
    const auto transactions = require(w.transactions.list());
    REQUIRE(transactions.size() == 1);
    CHECK(transactions[0].state == platform::TransactionState::Restored);
    CHECK_FALSE(require(launcher.lastRestorableTransaction(kGame)));

    const std::vector<persistence::LaunchRecord> history = launches(w);
    REQUIRE(history.size() == 1);
    CHECK(history[0].outcome == "process_start_failed");
    CHECK_FALSE(history[0].pid);
    CHECK(history[0].transactionId == transactions[0].id);
}

TEST_CASE("Configuration and plan failures never start the game", "[services][launch]") {
    World w;
    w.installFixtureMods();
    const std::string previous = installPreviousConfiguration(w);
    w.refresh();
    const std::int64_t id = makePlayset(w, mixedEntries());
    LaunchService launcher(w.ctx, nullptr);

    SECTION("the adapter cannot build a plan") {
        w.adapter.planError = makeError(ErrorCode::ValidationFailed, "dlc_load.json is damaged.");
        const auto failed = launcher.launch(kGame, id);
        REQUIRE_FALSE(failed);
        CHECK_THAT(failed.error().message, ContainsSubstring("dlc_load.json is damaged"));
        CHECK(require(w.transactions.list()).empty());
        CHECK(launches(w).at(0).outcome == "validation_failed");
    }
    SECTION("writing the configuration fails and is rolled back") {
        platform::FileTransactionFaults faults;
        faults.failWriteAt = std::size_t{0};
        w.transactions.setFaultsForTesting(faults);
        const auto failed = launcher.launch(kGame, id);
        REQUIRE_FALSE(failed);
        CHECK(failed.error().code == ErrorCode::IoError);
        CHECK_THAT(failed.error().message,
                   ContainsSubstring("previous configuration is unchanged"));
        CHECK(require(w.transactions.listIncomplete()).empty());
        CHECK(launches(w).at(0).outcome == "io_error");
    }
    CHECK(w.started.empty());
    CHECK(readText(configFile(w)) == previous);
}

TEST_CASE("An interrupted configuration change blocks launching until it is recovered",
          "[services][launch]") {
    World w;
    w.installFixtureMods();
    const std::string previous = installPreviousConfiguration(w);
    w.refresh();
    const std::int64_t id = makePlayset(w, mixedEntries());

    // The application dies right after replacing dlc_load.json.
    platform::FileTransactionFaults crash;
    crash.crashAfterWrites = std::size_t{1};
    w.transactions.setFaultsForTesting(crash);
    {
        LaunchService launcher(w.ctx, nullptr);
        const auto crashed = launcher.launch(kGame, id);
        REQUIRE_FALSE(crashed);
        CHECK_THAT(crashed.error().message, ContainsSubstring("Recover the interrupted"));
    }
    CHECK(w.started.empty());
    CHECK(readText(configFile(w)) == kTranslatedOrder);

    // Restart: a fresh journal manager and services over the same folders.
    platform::FileTransactionManager journal(w.ctx.paths.transactionsDir);
    ServiceContext ctx(*w.db, journal);
    ctx.adapters = w.ctx.adapters;
    ctx.clock = w.ctx.clock;
    ctx.startProcess = w.ctx.startProcess;
    ctx.listProcesses = w.ctx.listProcesses;
    LaunchService launcher(ctx, nullptr);

    const LaunchCheck check = launcher.check(kGame, id);
    CHECK_FALSE(check.canLaunch);
    CHECK(check.interruptedTransaction);
    CHECK(check.interruptedBlocks);
    CHECK(contains(check.blockers, "interrupted"));
    const auto refused = launcher.launch(kGame, id);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == ErrorCode::Interrupted);
    CHECK(w.started.empty());

    const auto interrupted = require(launcher.interruptedTransactions());
    REQUIRE(interrupted.size() == 1);
    CHECK(interrupted[0].label == LaunchService::transactionLabel(kGame, id, "Main"));
    const platform::TransactionRecord recovered =
        require(launcher.recoverInterrupted(interrupted[0].id));
    CHECK(recovered.state == platform::TransactionState::Recovered);
    CHECK(readText(configFile(w)) == previous);
    CHECK(require(launcher.interruptedTransactions()).empty());

    CHECK(launcher.check(kGame, id).canLaunch);
    require(launcher.launch(kGame, id));
    CHECK(w.started.size() == 1);
}

TEST_CASE("Only an interrupted change of this game's files blocks its launch",
          "[services][launch]") {
    World w;
    w.installFixtureMods();
    const std::string previous = installPreviousConfiguration(w);
    w.refresh();
    const std::int64_t id = makePlayset(w, mixedEntries());
    const fs::path eu4Data = w.documents / "Paradox Interactive" / u8("Europa Universalis IV");
    LaunchService launcher(w.ctx, nullptr);

    SECTION("another game's launch: a warning, and the launch goes ahead") {
        // Its backup may be lost so that recovery fails; Stellaris is unaffected.
        interruptChange(w, LaunchService::transactionLabel("eu4", 7, "EU4 Main"),
                        eu4Data / "dlc_load.json");
        const LaunchCheck check = launcher.check(kGame, id);
        CHECK(check.canLaunch);
        CHECK(check.interruptedTransaction); // still offered for recovery
        CHECK_FALSE(check.interruptedBlocks);
        CHECK_FALSE(contains(check.blockers, "interrupted"));
        CHECK(contains(check.warnings, "Europa Universalis IV"));
        CHECK(contains(check.warnings, "launch eu4 playset 7 (EU4 Main)"));

        const LaunchOutcome outcome = require(launcher.launch(kGame, id));
        CHECK(outcome.pid == 4242);
        CHECK(w.started.size() == 1);
        CHECK(readText(configFile(w)) == kTranslatedOrder);
        CHECK(require(launcher.interruptedTransactions()).size() == 1); // left for its game
        CHECK(readText(eu4Data / "dlc_load.json") == "changed");
    }
    SECTION("another game's launch: a rolled-back write still reports the old state") {
        interruptChange(w, LaunchService::transactionLabel("eu4", 7, "EU4 Main"),
                        eu4Data / "dlc_load.json");
        platform::FileTransactionFaults faults;
        faults.failWriteAt = std::size_t{0};
        w.transactions.setFaultsForTesting(faults);
        const auto failed = launcher.launch(kGame, id);
        REQUIRE_FALSE(failed);
        CHECK(failed.error().code == ErrorCode::IoError);
        CHECK_THAT(failed.error().message,
                   ContainsSubstring("previous configuration is unchanged"));
        CHECK(w.started.empty());
        CHECK(readText(configFile(w)) == previous);
    }
    SECTION("an unattributable change outside this game's folders: a warning") {
        interruptChange(w, "settings import", w.root / "elsewhere" / "settings.txt");
        const LaunchCheck check = launcher.check(kGame, id);
        CHECK(check.canLaunch);
        CHECK(check.interruptedTransaction);
        CHECK_FALSE(check.interruptedBlocks);
        CHECK(contains(check.warnings, "settings import"));
    }
    SECTION("any change that touched this game's user data blocks") {
        interruptChange(w, "settings import", w.userData / "mod" / "ugc_1.mod");
        const LaunchCheck check = launcher.check(kGame, id);
        CHECK_FALSE(check.canLaunch);
        CHECK(check.interruptedTransaction);
        CHECK(check.interruptedBlocks);
        CHECK(contains(check.blockers, "interrupted"));
        const auto refused = launcher.launch(kGame, id);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().code == ErrorCode::Interrupted);
        CHECK(w.started.empty());
    }
}

TEST_CASE("A missing installation or executable blocks with an explanation", "[services][launch]") {
    World w;
    w.installFixtureMods();
    w.refresh();
    const std::int64_t id = makePlayset(w, {workshopEntry("2000000001", "UI Overhaul")});
    LaunchService launcher(w.ctx, nullptr);

    SECTION("the executable is gone") {
        fs::remove(w.executable);
        CHECK(contains(launcher.check(kGame, id).blockers, "executable was not found"));
    }
    SECTION("the game is not installed") {
        w.adapter.discovered.clear();
        w.refresh();
        const LaunchCheck check = launcher.check(kGame, id);
        CHECK_FALSE(check.installation);
        CHECK(contains(check.blockers, "has not been found"));
    }
    SECTION("no adapter for the game") {
        w.ctx.adapters.clear();
        CHECK(contains(launcher.check(kGame, id).blockers, "not supported"));
    }
    CHECK_FALSE(launcher.check(kGame, id).canLaunch);
    CHECK_FALSE(launcher.launch(kGame, id));
    CHECK(w.started.empty());
}

TEST_CASE("Launching closes the game's Steam helper session before starting the game",
          "[services][launch]") {
    AcquiringRig r;
    bool sessionOpenAtStart = true;
    const auto starter = r.w.ctx.startProcess;
    r.w.ctx.startProcess = [&](const platform::ProcessSpec& spec) {
        sessionOpenAtStart = r.steam.isOpen;
        return starter(spec);
    };
    LaunchService launcher(r.w.ctx, &r.coordinator);
    CHECK(launcher.check(kGame, r.playsetId).canLaunch);
    const LaunchOutcome outcome = require(launcher.launch(kGame, r.playsetId));
    CHECK_FALSE(sessionOpenAtStart);
    CHECK(r.steam.closes == 1);
    CHECK_FALSE(r.coordinator.sessionOpen(kGame));
    // The background job is parked, not lost: the next start resumes it.
    CHECK(r.coordinator.liveJobs(kGame).empty());
    CHECK(r.coordinator.snapshot(kGame).jobs.size() == 1);
    CHECK(outcome.acquisition.changed); // so that the job views refresh
}

TEST_CASE("A launch whose plan fails leaves Workshop acquisition untouched", "[services][launch]") {
    AcquiringRig r;
    LaunchService launcher(r.w.ctx, &r.coordinator);
    REQUIRE(launcher.check(kGame, r.playsetId).canLaunch);
    // E.g. a Workshop folder removed since the last scan, or an unreadable dlc_load.json.
    r.w.adapter.planError = makeError(ErrorCode::ValidationFailed, "Files of 1 mod are missing.");

    const auto failed = launcher.launch(kGame, r.playsetId);
    REQUIRE_FALSE(failed);
    CHECK_THAT(failed.error().message, ContainsSubstring("Files of 1 mod are missing"));
    CHECK(r.w.started.empty());
    CHECK(r.steam.closes == 0);
    CHECK(r.steam.isOpen);
    CHECK(r.coordinator.sessionOpen(kGame));
    CHECK(r.coordinator.hasActiveWork(kGame));
    CHECK(r.coordinator.liveJobs(kGame).size() == 1);
}

TEST_CASE("A launch that fails after closing the helper session resumes acquisition",
          "[services][launch]") {
    AcquiringRig r;
    LaunchService launcher(r.w.ctx, &r.coordinator);
    SECTION("the game process cannot be started") {
        r.w.startFailure = makeError(ErrorCode::ProcessStartFailed, "Access is denied.");
        const auto failed = launcher.launch(kGame, r.playsetId);
        REQUIRE_FALSE(failed);
        CHECK(failed.error().code == ErrorCode::ProcessStartFailed);
        CHECK(r.w.started.size() == 1);
    }
    SECTION("the configuration cannot be written") {
        platform::FileTransactionFaults faults;
        faults.failWriteAt = std::size_t{0};
        r.w.transactions.setFaultsForTesting(faults);
        const auto failed = launcher.launch(kGame, r.playsetId);
        REQUIRE_FALSE(failed);
        CHECK(failed.error().code == ErrorCode::IoError);
        CHECK(r.w.started.empty());
    }
    // The session was closed for the launch, then reopened for the parked job.
    CHECK(r.steam.closes == 1);
    CHECK(r.steam.opens == 2);
    CHECK(r.coordinator.sessionOpen(kGame));
    CHECK(r.coordinator.liveJobs(kGame).size() == 1);
    CHECK(r.coordinator.hasActiveWork(kGame));
}
