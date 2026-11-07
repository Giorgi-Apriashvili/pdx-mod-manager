// ServiceRunner marshalling and shutdown, and the Backend driving the real
// services through it: edits are persisted and undoable, playset management,
// the share-code round trip, start-up failures, Play without a playset, Play
// following Steam, the game and the launcher on its own, and the downloads
// view after a launch (the real pmm-steam-helper with the fake Steam API
// library).

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/launch_history_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/settings_repository.hpp"
#include "platform/file_transaction.hpp"
#include "platform/steam.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QPointer>
#include <QSignalSpy>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace pmm;
using namespace pmm::app;
using namespace pmm::app::test;

namespace {

RunnerOptions runnerOptions(const fs::path& profile, const FakeAdapter& adapter) {
    RunnerOptions options;
    options.runtime = hermeticOptions(profile, adapter);
    options.steam = false;
    return options;
}

std::vector<PlaysetEntry> storedEntries(ServiceRunner& runner, std::int64_t playsetId) {
    return runner.callBlocking([playsetId](AppRuntime& runtime) {
        auto playset = runtime.playsets().playset(playsetId);
        return playset ? playset.value().entries : std::vector<PlaysetEntry>{};
    });
}

std::vector<std::string> ids(const std::vector<PlaysetEntry>& entries) {
    std::vector<std::string> out;
    for (const PlaysetEntry& entry : entries)
        out.push_back(entry.identity.id);
    return out;
}

std::vector<std::string> ids(const ModListModel& model) {
    std::vector<std::string> out;
    for (const ModRowData& row : model.rows())
        out.push_back(row.entry.identity.id);
    return out;
}

// Creates playsets of the fake game on the worker; the last one becomes active.
std::vector<std::int64_t> createPlaysets(ServiceRunner& runner,
                                         const std::vector<std::string>& names) {
    return runner.callBlocking([names](AppRuntime& runtime) {
        std::vector<std::int64_t> out;
        for (const std::string& name : names) {
            auto id = runtime.playsets().create(kGame, name);
            if (!id || !runtime.playsets().setActivePlayset(kGame, id.value()))
                return std::vector<std::int64_t>{};
            out.push_back(id.value());
        }
        return out;
    });
}

// A process list the test changes while the Backend runs; counts the listings.
// While it is held, a listing (and the worker with it) waits for release().
struct FakeProcesses {
    std::mutex mutex;
    std::condition_variable released;
    std::vector<platform::RunningProcess> list;
    bool held = false;
    std::atomic<int> listed{0};
    std::atomic<bool> waiting{false};

    void set(std::vector<platform::RunningProcess> processes) {
        const std::lock_guard lock(mutex);
        list = std::move(processes);
    }
    void hold() {
        const std::lock_guard lock(mutex);
        held = true;
    }
    void release() {
        {
            const std::lock_guard lock(mutex);
            held = false;
        }
        released.notify_all();
    }
    Result<std::vector<platform::RunningProcess>> read() {
        ++listed;
        std::unique_lock lock(mutex);
        waiting = held;
        released.wait(lock, [this] { return !held; });
        waiting = false;
        return list;
    }
};

// Releases held listings when a test leaves its scope, so that a failed
// check does not leave the worker waiting at shutdown.
struct ReleaseOnExit {
    std::shared_ptr<FakeProcesses> processes;
    ~ReleaseOnExit() { processes->release(); }
};

RunnerOptions runnerOptions(const fs::path& profile, const FakeAdapter& adapter,
                            const std::shared_ptr<FakeProcesses>& processes) {
    RunnerOptions options = runnerOptions(profile, adapter);
    options.runtime.configureContext = [hermetic = options.runtime.configureContext,
                                        processes](services::ServiceContext& context) {
        hermetic(context);
        context.listProcesses = [processes] { return processes->read(); };
    };
    return options;
}

// Leaves a launch of the fake game without mods "applying" in the profile's
// journal, as a crash right after writing `target` would.
platform::TransactionRecord interruptLaunch(const fs::path& profile, const fs::path& target) {
    writeText(target, "before");
    platform::FileTransactionManager journal(profile / "transactions");
    platform::FileTransactionFaults crash;
    crash.crashAfterWrites = std::size_t{1};
    journal.setFaultsForTesting(crash);
    REQUIRE_FALSE(journal.apply("launch stellaris without mods", {{target, "after"}}).ok());
    const auto incomplete = journal.listIncomplete();
    REQUIRE(incomplete.ok());
    REQUIRE(incomplete->size() == 1);
    return incomplete->front();
}

// A second supported game ("eu4"), not installed, to switch to.
class AbsentGameAdapter : public FakeAdapter {
public:
    using FakeAdapter::FakeAdapter;
    const GameCatalogEntry& catalog() const override { return *findGame("eu4"); }
    std::vector<GameInstallation> discover(const games::DiscoveryEnvironment&) const override {
        return {};
    }
};

std::optional<platform::TransactionRecord> journalRecord(ServiceRunner& runner,
                                                         const std::string& id) {
    return runner.callBlocking([id](AppRuntime& runtime) {
        auto record = runtime.context().transactions.load(id);
        return record ? std::optional<platform::TransactionRecord>(std::move(record).value())
                      : std::nullopt;
    });
}

std::int64_t storedActivePlayset(ServiceRunner& runner) {
    return runner.callBlocking([](AppRuntime& runtime) -> std::int64_t {
        auto active = runtime.playsets().activePlaysetId(kGame);
        return active && active.value() ? *active.value() : -1;
    });
}

} // namespace

TEST_CASE("ServiceRunner runs calls on its worker in order and answers on the caller",
          "[app][runner]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    ServiceRunner runner(runnerOptions(temp.path() / "profile", adapter));
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    CHECK(fs::is_regular_file(temp.path() / "profile" / "state.sqlite"));

    QObject receiver;
    QThread* const gui = QThread::currentThread();
    std::vector<int> order;
    std::vector<bool> onWorker;
    bool doneOnGui = true;
    for (int i = 0; i < 50; ++i) {
        runner.call(
            &receiver,
            [i, gui, &onWorker](AppRuntime&) {
                onWorker.push_back(QThread::currentThread() != gui);
                return i;
            },
            [&order, &doneOnGui, gui](int value) {
                doneOnGui = doneOnGui && QThread::currentThread() == gui;
                order.push_back(value);
            });
    }
    REQUIRE(waitUntil([&] { return order.size() == 50; }));
    CHECK(doneOnGui);
    CHECK(std::all_of(onWorker.begin(), onWorker.end(), [](bool b) { return b; }));
    CHECK(std::is_sorted(order.begin(), order.end()));

    // Results for a receiver that is gone are dropped.
    bool delivered = false;
    auto* shortLived = new QObject;
    runner.call(shortLived, [](AppRuntime&) { return 1; }, [&delivered](int) { delivered = true; });
    delete shortLived;
    const int marker = runner.callBlocking([](AppRuntime&) { return 7; });
    CHECK(marker == 7);
    QCoreApplication::processEvents();
    CHECK_FALSE(delivered);

    runner.shutdown();
    CHECK_FALSE(runner.isRunning());
    runner.shutdown(); // idempotent
    bool afterShutdown = false;
    runner.call(
        &receiver, [](AppRuntime&) { return 0; }, [&afterShutdown](int) { afterShutdown = true; });
    QCoreApplication::processEvents();
    CHECK_FALSE(afterShutdown);
}

TEST_CASE("ServiceRunner reports start-up failures with an actionable message", "[app][runner]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    const fs::path profile = temp.path() / "profile";
    writeText(profile / "state.sqlite", "this is not a database");
    ServiceRunner runner(runnerOptions(profile, adapter));
    const RunnerStart start = runner.start();
    CHECK_FALSE(start.ok);
    CHECK(start.errorMessage.contains(QStringLiteral("database")));
    CHECK(start.errorMessage.contains(QStringLiteral("state.sqlite")));
    runner.shutdown();
}

TEST_CASE("Backend edits are persisted, undoable and reflected in the models", "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("1001", "Alpha");
    world.addWorkshopMod("1002", "Beta");
    world.addWorkshopMod("1003", "Gamma");
    world.addLocalMod("delta mod", "Delta");
    world.addLocalMod("epsilon", "Epsilon");
    FakeAdapter adapter(world);
    ServiceRunner runner(runnerOptions(temp.path() / "profile", adapter));
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    CHECK(backend.currentGame() == "stellaris");
    CHECK(backend.games()->currentInstalled());
    CHECK(backend.inventory()->totalCount() == 5);
    CHECK(backend.playsets()->count() == 0);
    // No playset: Play starts the installed game without mods.
    CHECK(backend.launcher()->blockers().isEmpty());
    CHECK(backend.launcher()->canPlay());
    CHECK(backend.launcher()->warnings() ==
          QStringList{"No playset is selected, so the game starts without mods."});

    backend.playsets()->create(QStringLiteral("Campaign"));
    REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
    const std::int64_t id = backend.activePlaysetId();
    CHECK(backend.playsets()->activeName() == "Campaign");

    backend.mods()->addMods({"steam-workshop:1001", "steam-workshop:1002", "steam-workshop:1003",
                             "local:delta mod", "local:epsilon"});
    REQUIRE(waitUntil([&] { return backend.mods()->count() == 5 && !backend.mods()->busy(); }));
    CHECK(backend.mods()->enabledCount() == 5);
    CHECK(backend.launcher()->canPlay()); // everything installed and ready
    CHECK(ids(storedEntries(runner, id)) ==
          std::vector<std::string>{"1001", "1002", "1003", "delta mod", "epsilon"});

    SECTION("enable/disable") {
        backend.mods()->setEnabled(1, false);
        CHECK_FALSE(backend.mods()->rows()[1].entry.enabled); // optimistic
        REQUIRE(waitUntil([&] { return !backend.mods()->busy(); }));
        CHECK_FALSE(storedEntries(runner, id)[1].enabled);
        CHECK(backend.mods()->canUndo());
        backend.mods()->undo();
        REQUIRE(waitUntil([&] { return !backend.mods()->busy(); }));
        CHECK(storedEntries(runner, id)[1].enabled);
        CHECK(backend.mods()->rows()[1].entry.enabled);
        CHECK(backend.mods()->canRedo());
    }
    SECTION("multi-row moves are one undo step") {
        backend.mods()->moveRows({3, 4}, -1);
        CHECK(ids(*backend.mods()) ==
              std::vector<std::string>{"1001", "1002", "delta mod", "epsilon", "1003"});
        REQUIRE(waitUntil([&] { return !backend.mods()->busy(); }));
        CHECK(ids(storedEntries(runner, id)) == ids(*backend.mods()));
        backend.mods()->moveRows({0, 1}, 3); // drag: block lands at index 3
        CHECK(ids(*backend.mods()) ==
              std::vector<std::string>{"delta mod", "epsilon", "1003", "1001", "1002"});
        REQUIRE(waitUntil([&] { return !backend.mods()->busy(); }));
        CHECK(ids(storedEntries(runner, id)) == ids(*backend.mods()));
        backend.mods()->undo();
        REQUIRE(waitUntil([&] { return !backend.mods()->busy(); }));
        CHECK(ids(storedEntries(runner, id)) ==
              std::vector<std::string>{"1001", "1002", "delta mod", "epsilon", "1003"});
        CHECK(ids(*backend.mods()) == ids(storedEntries(runner, id)));
    }
    SECTION("reordering is refused while filtered") {
        backend.modView()->setSearch(QStringLiteral("a"));
        backend.mods()->moveRow(0, 2);
        CHECK(ids(*backend.mods())[0] == "1001");
        CHECK(backend.notifications()->latest().value("title").toString().contains("Reordering"));
        backend.modView()->setSearch({});
    }
    SECTION("removal is snapshotted and undoable") {
        backend.mods()->removeRows({0, 2});
        CHECK(backend.mods()->count() == 3);
        REQUIRE(waitUntil([&] { return !backend.mods()->busy(); }));
        CHECK(storedEntries(runner, id).size() == 3);
        CHECK(backend.inventory()->totalCount() == 5);
        backend.mods()->undo();
        REQUIRE(waitUntil([&] { return !backend.mods()->busy() && backend.mods()->count() == 5; }));
        CHECK(ids(storedEntries(runner, id)) ==
              std::vector<std::string>{"1001", "1002", "1003", "delta mod", "epsilon"});
    }
    SECTION("delete and restore a playset") {
        backend.playsets()->remove(static_cast<int>(id));
        REQUIRE(waitUntil([&] { return backend.playsets()->count() == 0; }));
        CHECK(backend.playsets()->canRestore());
        CHECK(backend.activePlaysetId() == -1);
        backend.playsets()->restoreLast();
        REQUIRE(waitUntil(
            [&] { return backend.playsets()->count() == 1 && backend.mods()->count() == 5; }));
        CHECK(backend.playsets()->activeName() == "Campaign");
        CHECK_FALSE(backend.playsets()->canRestore());
    }
    SECTION("share code round trip into a new playset") {
        backend.mods()->setEnabled(2, false);
        backend.mods()->moveRow(4, 0);
        REQUIRE(waitUntil([&] { return !backend.mods()->busy(); }));
        const std::vector<PlaysetEntry> exported = storedEntries(runner, id);

        backend.sharing()->exportCode();
        REQUIRE(waitUntil([&] { return !backend.sharing()->lastExportCode().isEmpty(); }));
        const QString code = backend.sharing()->lastExportCode();
        CHECK(code.startsWith("PMM1."));

        backend.sharing()->previewImport(code);
        REQUIRE(waitUntil(
            [&] { return backend.sharing()->hasPreview() && !backend.sharing()->busy(); }));
        const QVariantMap preview = backend.sharing()->preview();
        REQUIRE(preview.value("ok").toBool());
        CHECK(preview.value("total").toInt() == 5);
        CHECK(preview.value("enabled").toInt() == 4);
        CHECK(preview.value("missing").toInt() == 0);
        CHECK(preview.value("entries").toList().size() == 5);
        CHECK(backend.playsets()->count() == 1); // previewing changes nothing

        REQUIRE(
            backend.sharing()->confirmImport("new", -1, preview.value("proposedName").toString()));
        REQUIRE(waitUntil([&] {
            return backend.playsets()->count() == 2 && backend.activePlaysetId() != id &&
                   backend.mods()->playsetId() == backend.activePlaysetId();
        }));
        const std::vector<PlaysetEntry> imported = storedEntries(runner, backend.activePlaysetId());
        REQUIRE(imported.size() == exported.size());
        for (std::size_t i = 0; i < imported.size(); ++i) {
            CHECK(imported[i].identity.source == exported[i].identity.source);
            CHECK(imported[i].identity.id == exported[i].identity.id);
            CHECK(imported[i].enabled == exported[i].enabled);
        }
    }
    SECTION("replacing a playset with a share code keeps its name") {
        backend.sharing()->exportCode();
        REQUIRE(waitUntil([&] { return !backend.sharing()->lastExportCode().isEmpty(); }));
        const QString code = backend.sharing()->lastExportCode();
        const std::vector<PlaysetEntry> exported = storedEntries(runner, id);
        backend.mods()->setEnabled(0, false);
        REQUIRE(waitUntil([&] { return !backend.mods()->busy(); }));

        backend.sharing()->previewImport(code);
        REQUIRE(waitUntil(
            [&] { return backend.sharing()->hasPreview() && !backend.sharing()->busy(); }));
        // The dialog's name field still holds the name proposed for a new
        // playset ("Campaign (2)"), and the dialog passed it along.
        const QString proposed = backend.sharing()->preview().value("proposedName").toString();
        CHECK(proposed == "Campaign (2)");
        REQUIRE(backend.sharing()->confirmImport("replace", static_cast<int>(id), proposed));
        REQUIRE(waitUntil([&] {
            return !backend.sharing()->busy() && storedEntries(runner, id).front().enabled;
        }));
        const std::string stored = runner.callBlocking([id](AppRuntime& runtime) {
            auto playset = runtime.playsets().playset(id);
            return playset ? playset.value().name : std::string();
        });
        CHECK(stored == "Campaign");
        CHECK(storedEntries(runner, id).size() == exported.size());
        CHECK(backend.playsets()->count() == 1);
        CHECK(backend.activePlaysetId() == id);
        CHECK(backend.playsets()->activeName() == "Campaign");
    }
    SECTION("bad share codes are explained") {
        backend.sharing()->previewImport(QStringLiteral("PMM9.abc.def"));
        REQUIRE(waitUntil(
            [&] { return backend.sharing()->hasPreview() && !backend.sharing()->busy(); }));
        CHECK_FALSE(backend.sharing()->preview().value("ok").toBool());
        CHECK_FALSE(backend.sharing()->preview().value("errorMessage").toString().isEmpty());
    }

    backend.shutdown();
    runner.shutdown();
}

TEST_CASE("Launch blockers explain a running game, with or without a playset", "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("3001", "Only");
    FakeAdapter adapter(world);
    RunnerOptions options = runnerOptions(temp.path() / "profile", adapter);
    const auto hermetic = options.runtime.configureContext;
    options.runtime.configureContext = [hermetic](services::ServiceContext& context) {
        hermetic(context);
        context.listProcesses = [] {
            platform::RunningProcess game;
            game.pid = 4242;
            game.executableName = kExeName;
            return Result<std::vector<platform::RunningProcess>>(
                std::vector<platform::RunningProcess>{steamClient(), game});
        };
    };
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    // Without a playset the game would start without mods, but it is running.
    const QStringList blockers = backend.launcher()->blockers();
    REQUIRE(blockers.size() == 1);
    CHECK(blockers.front().contains("already running"));
    CHECK(backend.launcher()->warnings().join('\n').contains("No playset is selected"));
    CHECK_FALSE(backend.launcher()->canPlay());

    backend.playsets()->create(QStringLiteral("Running"));
    REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
    REQUIRE(waitUntil(
        [&] { return !backend.launcher()->warnings().join('\n').contains("No playset"); }));
    CHECK(backend.launcher()->blockers().join('\n').contains("already running"));
    CHECK_FALSE(backend.launcher()->canPlay());
    runner.shutdown();
}

TEST_CASE("Play follows Steam, the game and the Paradox launcher without a click",
          "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("3001", "Only");
    FakeAdapter adapter(world);
    auto processes = std::make_shared<FakeProcesses>();
    const platform::RunningProcess game{4242, kExeName, {}};
    const platform::RunningProcess launcher{99, "Paradox Launcher.exe", {}};
    QString blocker;
    bool gameStays = false; // Steam starts first, the game exits later
    SECTION("Steam starts") {
        processes->set({{7, "explorer.exe", {}}});
        blocker = "Steam is not running. Start Steam and sign in, then launch Stellaris.";
    }
    SECTION("the game exits") {
        processes->set({steamClient(), game});
        blocker = "already running";
    }
    SECTION("the Paradox launcher is closed") {
        processes->set({steamClient(), launcher});
        blocker = "Paradox launcher";
    }
    SECTION("Steam starts while the game runs") {
        processes->set({game});
        blocker = "Steam is not running";
        gameStays = true;
    }
    ServiceRunner runner(runnerOptions(temp.path() / "profile", adapter, processes));
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    BackendOptions options;
    options.launchRecheckMs = 50;
    Backend backend(runner, options);
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    CHECK_FALSE(backend.launcher()->canPlay());
    CHECK(backend.launcher()->blockers().join('\n').contains(blocker));

    // Nothing changes: the processes are checked again and again, Play stays blocked.
    const int listed = processes->listed;
    REQUIRE(waitUntil([&] { return processes->listed >= listed + 3; }));
    CHECK_FALSE(backend.launcher()->canPlay());
    CHECK(backend.launcher()->blockers().join('\n').contains(blocker));

    if (gameStays) {
        processes->set({game, steamClient()});
        // Steam's blocker goes; the running game's stays, and is checked further.
        REQUIRE(waitUntil([&] { return !backend.launcher()->data().steamNotRunning; }));
        CHECK(backend.launcher()->blockers().join('\n').contains("already running"));
        CHECK_FALSE(backend.launcher()->blockers().join('\n').contains("Steam"));
        CHECK_FALSE(backend.launcher()->canPlay());
        const int running = processes->listed;
        REQUIRE(waitUntil([&] { return processes->listed >= running + 3; }));
    }
    processes->set({steamClient()});
    // Play becomes available on its own...
    REQUIRE(waitUntil([&] { return backend.launcher()->canPlay(); }));
    CHECK(backend.launcher()->blockers().isEmpty());
    // ... and nothing is checked any more.
    const int settled = processes->listed;
    CHECK_FALSE(
        waitUntil([&] { return processes->listed != settled; }, std::chrono::milliseconds(600)));
    backend.shutdown();
    runner.shutdown();
}

TEST_CASE("Re-checks follow a playset chosen while one is under way", "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("3001", "Only");
    FakeAdapter adapter(world);
    auto processes = std::make_shared<FakeProcesses>();
    processes->set({{7, "explorer.exe", {}}}); // Steam is closed
    bool steamStarts = false;
    SECTION("Steam stays closed") {}
    SECTION("Steam starts meanwhile") {
        steamStarts = true;
    }
    ServiceRunner runner(runnerOptions(temp.path() / "profile", adapter, processes));
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    const std::vector<std::int64_t> playsets = createPlaysets(runner, {"First", "Second"});
    REQUIRE(playsets.size() == 2);
    BackendOptions options;
    options.launchRecheckMs = 50;
    Backend backend(runner, options);
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    REQUIRE(backend.activePlaysetId() == playsets[1]);
    REQUIRE(backend.launcher()->data().steamNotRunning);

    // A re-check waits on the worker while the other playset is chosen, so
    // its result is for the playset before.
    processes->hold();
    const ReleaseOnExit releaseOnExit{processes};
    REQUIRE(waitUntil([&] { return processes->waiting.load(); }));
    backend.activatePlayset(playsets[0]);
    if (steamStarts)
        processes->set({{7, "explorer.exe", {}}, steamClient()});
    processes->release();
    REQUIRE(waitUntil([&] { return backend.mods()->playsetId() == playsets[0]; }));

    if (!steamStarts) {
        // Still blocked for the playset now chosen, and checked further.
        CHECK(backend.launcher()->data().steamNotRunning);
        const int listed = processes->listed;
        REQUIRE(waitUntil([&] { return processes->listed >= listed + 3; }));
        CHECK_FALSE(backend.launcher()->canPlay());
        processes->set({steamClient()});
    }
    REQUIRE(waitUntil([&] { return backend.launcher()->canPlay(); }));
    CHECK(backend.activePlaysetId() == playsets[0]);
    // Nothing is checked any more.
    const int settled = processes->listed;
    CHECK_FALSE(
        waitUntil([&] { return processes->listed != settled; }, std::chrono::milliseconds(600)));
    backend.shutdown();
    runner.shutdown();
}

TEST_CASE("The last launch outcome clears once its cause has gone", "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    auto processes = std::make_shared<FakeProcesses>();
    processes->set({steamClient()});
    const platform::RunningProcess game{4242, kExeName, {}};
    auto failStart = std::make_shared<std::atomic<bool>>(false);
    RunnerOptions runnerOpts = runnerOptions(temp.path() / "profile", adapter, processes);
    runnerOpts.runtime.configureContext = [base = runnerOpts.runtime.configureContext, processes,
                                           game, failStart](services::ServiceContext& context) {
        base(context);
        // Nothing is executed; a started game shows up in the process list.
        context.startProcess =
            [processes, game,
             failStart](const platform::ProcessSpec&) -> Result<platform::StartedProcess> {
            if (*failStart)
                return makeError(ErrorCode::ProcessStartFailed, "The fake start failed.");
            processes->set({steamClient(), game});
            return platform::StartedProcess{game.pid};
        };
    };
    ServiceRunner runner(runnerOpts);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    BackendOptions options;
    options.launchRecheckMs = 50;
    Backend backend(runner, options);
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    REQUIRE(backend.launcher()->canPlay());
    LaunchController& launcher = *backend.launcher();
    const auto rechecked = [&] {
        const int listed = processes->listed;
        return waitUntil([&] { return processes->listed >= listed + 3; });
    };

    SECTION("the started game exits") {
        launcher.play();
        REQUIRE(waitUntil([&] { return launcher.state() == QLatin1String("started"); }));
        // While the game runs, the state line says so.
        REQUIRE(waitUntil([&] { return launcher.data().runningInstance; }));
        REQUIRE(rechecked());
        CHECK(launcher.state() == QLatin1String("started"));
        processes->set({steamClient()});
        REQUIRE(waitUntil([&] { return launcher.canPlay(); }));
        CHECK(launcher.state() == QLatin1String("idle"));
    }
    SECTION("the refusal is resolved") {
        // Steam closes, and nothing has noticed yet: Play is refused when
        // pressed.
        processes->set({{7, "explorer.exe", {}}});
        launcher.play();
        REQUIRE(waitUntil([&] { return launcher.state() == QLatin1String("failed"); }));
        CHECK(launcher.lastError().contains("Steam is not running"));
        CHECK_FALSE(launcher.canPlay());
        REQUIRE(rechecked());
        CHECK(launcher.state() == QLatin1String("failed"));
        processes->set({steamClient()});
        REQUIRE(waitUntil([&] { return launcher.canPlay(); }));
        CHECK(launcher.state() == QLatin1String("idle"));
    }
    SECTION("a failure that leaves Play available stays") {
        *failStart = true;
        launcher.play();
        REQUIRE(waitUntil([&] { return launcher.state() == QLatin1String("failed"); }));
        CHECK(launcher.canPlay());
        QSignalSpy refreshed(&launcher, &LaunchController::changed);
        backend.refreshLaunch();
        REQUIRE(waitUntil([&] { return refreshed.count() >= 1; }));
        CHECK(launcher.state() == QLatin1String("failed"));
        CHECK(launcher.lastError().contains("fake start failed"));
    }
    backend.shutdown();
    runner.shutdown();
}

TEST_CASE("Read-only sessions do not re-check the processes", "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    auto processes = std::make_shared<FakeProcesses>();
    ServiceRunner runner(runnerOptions(temp.path() / "profile", adapter, processes));
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    BackendOptions options;
    options.readOnly = true;
    options.launchRecheckMs = 50;
    Backend backend(runner, options);
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    // Steam is not running, and Play is blocked anyway.
    CHECK(backend.launcher()->data().steamNotRunning);
    CHECK(backend.launcher()->blockers().join('\n').contains("Read-only verification mode"));
    const int listed = processes->listed;
    CHECK_FALSE(
        waitUntil([&] { return processes->listed != listed; }, std::chrono::milliseconds(600)));
    backend.shutdown();
    runner.shutdown();
}

TEST_CASE("Play without a playset starts the game without mods", "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("3001", "Only");
    FakeAdapter adapter(world);
    auto started = std::make_shared<std::atomic<int>>(0);
    RunnerOptions options = runnerOptions(temp.path() / "profile", adapter);
    const auto hermetic = options.runtime.configureContext;
    options.runtime.configureContext = [hermetic, started](services::ServiceContext& context) {
        hermetic(context);
        // Records the start; nothing is executed.
        context.startProcess =
            [started](const platform::ProcessSpec&) -> Result<platform::StartedProcess> {
            ++*started;
            return platform::StartedProcess{4242};
        };
    };
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    REQUIRE(backend.activePlaysetId() == -1);
    REQUIRE(backend.launcher()->canPlay());
    CHECK_FALSE(backend.launcher()->canRestoreBackup());

    backend.launcher()->play();
    REQUIRE(waitUntil([&] { return backend.launcher()->state() == QLatin1String("started"); }));
    CHECK(*started == 1);
    CHECK_FALSE(backend.launcher()->startedWithPlayset());
    const QVariantMap notification = backend.notifications()->latest();
    CHECK(notification.value("title").toString() == "Stellaris started");
    // The check's warning is what the notification says.
    CHECK(notification.value("text").toString() ==
          "No playset is selected, so the game starts without mods.");
    CHECK(backend.launcher()->canRestoreBackup());

    // One journaled change with the label of a launch without mods, and a
    // history row without a playset.
    const auto recorded = runner.callBlocking([](AppRuntime& runtime) {
        auto restorable = runtime.launcher().lastRestorableTransaction(kGame);
        auto history = persistence::LaunchHistoryRepository(runtime.database()).latest(kGame, 5);
        std::string label = restorable && restorable.value() ? restorable.value()->label : "";
        const bool noPlayset = history && history.value().size() == 1 &&
                               history.value()[0].outcome == "started" &&
                               !history.value()[0].playsetId;
        return std::make_pair(label, noPlayset);
    });
    CHECK(recorded.first == "launch stellaris without mods");
    CHECK(recorded.second);
    backend.shutdown();
    runner.shutdown();
}

TEST_CASE("Play stays blocked when the active playset cannot be loaded", "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("3001", "Only");
    FakeAdapter adapter(world);
    ServiceRunner runner(runnerOptions(temp.path() / "profile", adapter));
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    backend.playsets()->create(QStringLiteral("Campaign"));
    REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
    const std::int64_t id = backend.activePlaysetId();
    REQUIRE(waitUntil([&] { return backend.launcher()->canPlay(); }));

    // A damaged inventory row: the playset's mods cannot be loaded.
    REQUIRE(runner.callBlocking([](AppRuntime& runtime) {
        auto access = runtime.database().access();
        return access.connection().exec("UPDATE installed_mods SET state = 'bogus'").ok();
    }));
    bool reloaded = false;
    backend.reloadContext(std::nullopt, [&] { reloaded = true; });
    REQUIRE(waitUntil([&] { return reloaded; }));
    // Still the selected playset, so Play checks it rather than starting the
    // game without mods.
    CHECK(backend.playsets()->activeId() == id);
    CHECK(backend.activePlaysetId() == id);
    CHECK(backend.mods()->playsetId() == -1);
    CHECK_FALSE(backend.launcher()->canPlay());
    CHECK(backend.launcher()->blockers().join('\n').contains("database could not be read"));
    CHECK(backend.launcher()->warnings().filter("No playset").isEmpty());

    // Later refreshes keep it blocked, and Play refuses.
    QSignalSpy refreshed(backend.launcher(), &LaunchController::changed);
    backend.refreshAcquisition(true);
    backend.refreshLaunch();
    REQUIRE(waitUntil([&] { return refreshed.count() >= 2; }));
    CHECK_FALSE(backend.launcher()->canPlay());
    backend.launcher()->play();
    CHECK(backend.launcher()->state() == QLatin1String("idle"));
    CHECK(backend.notifications()->latest().value("title").toString() ==
          "The game cannot be started yet");
    const bool launched = runner.callBlocking([](AppRuntime& runtime) {
        auto history = persistence::LaunchHistoryRepository(runtime.database()).latest(kGame, 5);
        return !history || !history.value().empty();
    });
    CHECK_FALSE(launched);
    backend.shutdown();
    runner.shutdown();
}

TEST_CASE("A change Recover cannot undo can be discarded, and Play is available again",
          "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    const fs::path profile = temp.path() / "profile";
    const fs::path target = world.userData / "load_order.txt";
    const platform::TransactionRecord record = interruptLaunch(profile, target);
    const fs::path backup = record.directory / record.entries.at(0).backupFile;
    SECTION("its backup is missing") {
        fs::remove(backup);
    }
    SECTION("its backup is damaged") {
        writeText(backup, "damaged");
    }
    auto started = std::make_shared<std::atomic<int>>(0);
    RunnerOptions options = runnerOptions(profile, adapter);
    options.runtime.configureContext = [hermetic = options.runtime.configureContext,
                                        started](services::ServiceContext& context) {
        hermetic(context);
        // Records the start; nothing is executed. Steam stays running.
        context.startProcess =
            [started](const platform::ProcessSpec&) -> Result<platform::StartedProcess> {
            ++*started;
            return platform::StartedProcess{4242};
        };
    };
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    LaunchController& launcher = *backend.launcher();
    REQUIRE(launcher.hasInterruptedTransaction());
    CHECK_FALSE(launcher.canPlay());
    CHECK(launcher.blockers().join('\n').contains("interrupted"));
    // Offered only after Recover has failed.
    CHECK_FALSE(launcher.canDiscardInterrupted());
    CHECK_FALSE(launcher.discardInterrupted());

    QSignalSpy changed(&launcher, &LaunchController::changed);
    REQUIRE(launcher.recoverInterrupted());
    REQUIRE(waitUntil([&] { return launcher.canDiscardInterrupted(); }));
    CHECK(launcher.discardableFiles() == QStringList{qpath(target)});
    CHECK(launcher.discardFolders() == QStringList{qpath(record.directory)});
    const QVariantMap failed = backend.notifications()->latest();
    CHECK(failed.value("title").toString() == "Recovery incomplete");
    CHECK(failed.value("text").toString().contains("load_order.txt"));
    CHECK(failed.value("text").toString().contains("discard the record next to Play"));
    // The refresh after Recover still shows the change, and keeps the offer.
    const int recovered = static_cast<int>(changed.count());
    REQUIRE(waitUntil([&] { return changed.count() > recovered; }));
    CHECK(launcher.hasInterruptedTransaction());
    CHECK(launcher.canDiscardInterrupted());
    CHECK_FALSE(launcher.canPlay());

    REQUIRE(launcher.discardInterrupted());
    CHECK_FALSE(launcher.canDiscardInterrupted()); // taken at once: a second click does nothing
    CHECK_FALSE(launcher.discardInterrupted());
    REQUIRE(waitUntil([&] { return !launcher.hasInterruptedTransaction(); }));
    CHECK(launcher.canPlay());
    CHECK(launcher.blockers().isEmpty());
    const QVariantMap discarded = backend.notifications()->latest();
    CHECK(discarded.value("title").toString() == "Interrupted change discarded");
    CHECK(discarded.value("text").toString().contains(qpath(record.directory)));
    // The file stays as the interrupted launch left it; the journal says why.
    CHECK(require(platform::readFile(target)) == "after");
    const auto journal = journalRecord(runner, record.id);
    REQUIRE(journal);
    CHECK(journal->state == platform::TransactionState::Abandoned);
    CHECK(journal->lastError.find("recovery failed") != std::string::npos);

    // The next Play writes the configuration again.
    launcher.play();
    REQUIRE(waitUntil([&] { return launcher.state() == QLatin1String("started"); }));
    CHECK(*started == 1);
    CHECK(require(platform::readFile(target)).empty()); // no mods
    backend.shutdown();
    runner.shutdown();
}

TEST_CASE("A Recover that fails for another reason offers only Recover again", "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    const fs::path profile = temp.path() / "profile";
    const fs::path target = world.userData / "load_order.txt";
    const platform::TransactionRecord record = interruptLaunch(profile, target);
    // A folder in place of the file: it cannot be put back, but the backup is fine.
    fs::remove(target);
    fs::create_directory(target);
    ServiceRunner runner(runnerOptions(profile, adapter));
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    LaunchController& launcher = *backend.launcher();
    REQUIRE(launcher.hasInterruptedTransaction());

    QSignalSpy changed(&launcher, &LaunchController::changed);
    REQUIRE(launcher.recoverInterrupted());
    REQUIRE(waitUntil([&] {
        return backend.notifications()->latest().value("title").toString() == "Recovery incomplete";
    }));
    // ... and the refresh after it.
    const int recovered = static_cast<int>(changed.count());
    REQUIRE(waitUntil([&] { return changed.count() > recovered; }));
    CHECK_FALSE(backend.notifications()->latest().value("text").toString().contains("discard"));
    CHECK(launcher.hasInterruptedTransaction());
    CHECK_FALSE(launcher.canDiscardInterrupted());
    CHECK(launcher.discardableFiles().isEmpty());
    CHECK_FALSE(launcher.discardInterrupted());
    REQUIRE(journalRecord(runner, record.id));
    CHECK(journalRecord(runner, record.id)->state == platform::TransactionState::Applying);

    // Once the folder is gone, Recover succeeds.
    fs::remove(target);
    REQUIRE(launcher.recoverInterrupted());
    REQUIRE(waitUntil([&] { return !launcher.hasInterruptedTransaction(); }));
    CHECK(require(platform::readFile(target)) == "before");
    CHECK(launcher.canPlay());
    CHECK(backend.notifications()->latest().value("title").toString() == "Configuration recovered");
    backend.shutdown();
    runner.shutdown();
}

TEST_CASE("The offer to discard an interrupted change ends when the game changes",
          "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    AbsentGameAdapter other(world);
    const fs::path profile = temp.path() / "profile";
    const platform::TransactionRecord record =
        interruptLaunch(profile, world.userData / "load_order.txt");
    fs::remove(record.directory / record.entries.at(0).backupFile);
    RunnerOptions options = runnerOptions(profile, adapter);
    options.runtime.adapters = std::vector<const games::GameAdapter*>{&adapter, &other};
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    BackendOptions backendOptions;
    backendOptions.initialGame = kGame;
    Backend backend(runner, backendOptions);
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    REQUIRE(backend.currentGame() == kGame);
    LaunchController& launcher = *backend.launcher();
    REQUIRE(launcher.recoverInterrupted());
    REQUIRE(waitUntil([&] { return launcher.canDiscardInterrupted(); }));

    // The change still shows in the other game (as a warning), without the offer.
    backend.selectGame("eu4");
    CHECK_FALSE(launcher.canDiscardInterrupted());
    REQUIRE(waitUntil([&] { return launcher.hasInterruptedTransaction(); }));
    CHECK_FALSE(launcher.canDiscardInterrupted());
    CHECK(launcher.discardableFiles().isEmpty());
    CHECK_FALSE(launcher.discardInterrupted());

    // A Recover answered after the game changed offers nothing either.
    backend.selectGame(kGame);
    REQUIRE(waitUntil([&] { return launcher.hasInterruptedTransaction(); }));
    QSignalSpy posted(backend.notifications(), &NotificationCenter::posted);
    REQUIRE(launcher.recoverInterrupted());
    backend.selectGame("eu4");
    const auto recoveryText = [&]() -> std::optional<QString> {
        for (const QList<QVariant>& notification : posted) {
            if (notification.at(1).toString() == "Recovery incomplete")
                return notification.at(2).toString();
        }
        return std::nullopt;
    };
    REQUIRE(waitUntil([&] { return recoveryText().has_value(); }));
    CHECK(recoveryText()->contains("load_order.txt"));
    CHECK_FALSE(recoveryText()->contains("discard"));
    CHECK_FALSE(launcher.canDiscardInterrupted());
    CHECK(journalRecord(runner, record.id)->state == platform::TransactionState::Applying);
    backend.shutdown();
    runner.shutdown();
}

TEST_CASE("Read-only sessions neither recover nor discard interrupted changes", "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    const fs::path profile = temp.path() / "profile";
    const platform::TransactionRecord record =
        interruptLaunch(profile, world.userData / "load_order.txt");
    fs::remove(record.directory / record.entries.at(0).backupFile);
    ServiceRunner runner(runnerOptions(profile, adapter));
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    BackendOptions options;
    options.readOnly = true;
    Backend backend(runner, options);
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    LaunchController& launcher = *backend.launcher();
    REQUIRE(launcher.hasInterruptedTransaction());

    CHECK_FALSE(launcher.recoverInterrupted());
    CHECK(backend.notifications()->latest().value("title").toString() == "Recovery is disabled");
    CHECK_FALSE(launcher.canDiscardInterrupted());
    CHECK_FALSE(launcher.discardInterrupted());
    CHECK(backend.notifications()->latest().value("title").toString() == "Discarding is disabled");
    REQUIRE(journalRecord(runner, record.id));
    CHECK(journalRecord(runner, record.id)->state == platform::TransactionState::Applying);
    backend.shutdown();
    runner.shutdown();
}

TEST_CASE("Backend restores settings and creates playsets from the game configuration",
          "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("2001", "One");
    world.addWorkshopMod("2002", "Two");
    FakeAdapter adapter(world);
    adapter.currentConfiguration = {
        PlaysetEntry{ModIdentity{"stellaris", source::SteamWorkshop, "2002"}, true, "Two", {}, {}},
        PlaysetEntry{ModIdentity{"stellaris", source::SteamWorkshop, "2001"}, true, "One", {}, {}}};
    const fs::path profile = temp.path() / "profile";
    {
        ServiceRunner runner(runnerOptions(profile, adapter));
        const RunnerStart start = runner.start();
        REQUIRE(start.ok);
        Backend backend(runner, {});
        backend.initialize(start);
        REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
        REQUIRE(backend.importCurrentConfiguration());
        REQUIRE(waitUntil([&] { return backend.mods()->count() == 2; }));
        CHECK(backend.mods()->rows()[0].entry.identity.id == "2002");
        backend.settings()->setTheme(QStringLiteral("dark"));
        backend.settings()->setColumnWidths({40, 30, 300, 90, 140});
        backend.settings()->setWindowWidth(1234);
        backend.shutdown();
        runner.shutdown();
    }
    ServiceRunner runner(runnerOptions(profile, adapter));
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    CHECK(backend.settings()->theme() == "dark");
    CHECK(backend.settings()->effectiveDark());
    CHECK(backend.settings()->columnWidths() == QList<int>{40, 30, 300, 90, 140});
    CHECK(backend.settings()->windowWidth() == 1234);
    CHECK(backend.settings()->lastGameKey() == "stellaris");
    CHECK(backend.mods()->count() == 2); // last active playset restored at start-up
    runner.shutdown();
}

TEST_CASE("Backend follows the stored active playset when a reply was superseded",
          "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("1001", "Alpha");
    FakeAdapter adapter(world);
    ServiceRunner runner(runnerOptions(temp.path() / "profile", adapter));
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);

    SECTION("a playset created during the initial rescan stays active") {
        const std::vector<std::int64_t> playsets = createPlaysets(runner, {"P1"});
        REQUIRE(playsets.size() == 1);
        Backend backend(runner, {});
        backend.initialize(start); // P1 shown, the rescan is queued
        REQUIRE(backend.activePlaysetId() == playsets[0]);
        backend.playsets()->create(QStringLiteral("Fresh"));
        REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));

        const std::int64_t stored = storedActivePlayset(runner);
        CHECK(stored != playsets[0]);
        CHECK(backend.activePlaysetId() == stored);
        CHECK(backend.playsets()->activeId() == stored);
        CHECK(backend.playsets()->activeName() == "Fresh");
        CHECK(backend.mods()->playsetId() == stored);
    }
    SECTION("a failed creation brings back the activation it superseded") {
        const std::vector<std::int64_t> playsets = createPlaysets(runner, {"P1", "P2"});
        REQUIRE(playsets.size() == 2);
        Backend backend(runner, {});
        backend.initialize(start);
        REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
        REQUIRE(backend.mods()->playsetId() == playsets[1]);

        backend.playsets()->activate(static_cast<int>(playsets[0]));
        backend.playsets()->create(QString(300, QLatin1Char('x'))); // over the name limit
        REQUIRE(waitUntil([&] {
            return backend.notifications()->latest().value("title").toString() ==
                   "Could not create the playset";
        }));
        CHECK(storedActivePlayset(runner) == playsets[0]);
        CHECK(backend.activePlaysetId() == playsets[0]);
        CHECK(backend.playsets()->activeId() == playsets[0]);
        CHECK(backend.mods()->playsetId() == playsets[0]);
    }
    runner.shutdown();
}

TEST_CASE("Row edits made while an undo is in flight hit the entries the user saw",
          "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    for (const char* mod : {"1001", "1002", "1003", "1004", "1005", "1006"})
        world.addWorkshopMod(mod, std::string("Mod ") + mod);
    FakeAdapter adapter(world);
    ServiceRunner runner(runnerOptions(temp.path() / "profile", adapter));
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    backend.playsets()->create(QStringLiteral("Undo race"));
    REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
    const std::int64_t id = backend.activePlaysetId();
    ModListModel& mods = *backend.mods();
    mods.addMods({"steam-workshop:1001", "steam-workshop:1002", "steam-workshop:1003",
                  "steam-workshop:1004", "steam-workshop:1005"});
    REQUIRE(waitUntil([&] { return mods.count() == 5 && !mods.busy(); }));
    const auto settled = [&] { return waitUntil([&] { return !mods.busy(); }); };
    using Ids = std::vector<std::string>;

    // Each section saves an edit, then undoes it and edits a row by index in
    // the same tick: the index refers to the row the list showed.
    SECTION("enable/disable") {
        mods.removeRows({2}); // 1003
        REQUIRE(settled());
        mods.undo();
        REQUIRE(mods.rows()[2].entry.identity.id == "1004");
        mods.setEnabled(2, false);
        REQUIRE(settled());
        const std::vector<PlaysetEntry> stored = storedEntries(runner, id);
        CHECK(ids(stored) == Ids{"1001", "1002", "1003", "1004", "1005"});
        REQUIRE(stored.size() == 5);
        CHECK(stored[2].enabled);       // the restored entry is untouched
        CHECK_FALSE(stored[3].enabled); // the row the user disabled
        CHECK(ids(mods) == ids(stored));
        CHECK(mods.enabledCount() == 4);
    }
    SECTION("keyboard move") {
        mods.moveRows({1}, 1); // 1002 down
        REQUIRE(settled());
        mods.undo();
        REQUIRE(mods.rows()[2].entry.identity.id == "1002");
        mods.moveRows({2}, 1);
        REQUIRE(settled());
        CHECK(ids(storedEntries(runner, id)) == Ids{"1001", "1003", "1002", "1004", "1005"});
        CHECK(ids(mods) == ids(storedEntries(runner, id)));
    }
    SECTION("drag and drop") {
        mods.removeRows({1}); // 1002
        REQUIRE(settled());
        mods.undo();
        mods.moveRows({0}, 2); // 1001 dropped in front of 1005
        REQUIRE(settled());
        CHECK(ids(storedEntries(runner, id)) == Ids{"1002", "1003", "1004", "1001", "1005"});
        CHECK(ids(mods) == ids(storedEntries(runner, id)));
    }
    SECTION("single move") {
        mods.removeRows({2}); // 1003
        REQUIRE(settled());
        mods.undo();
        mods.moveRow(0, 2); // 1001 in front of 1005
        REQUIRE(settled());
        CHECK(ids(storedEntries(runner, id)) == Ids{"1002", "1003", "1004", "1001", "1005"});
        CHECK(ids(mods) == ids(storedEntries(runner, id)));
    }
    SECTION("removal") {
        mods.removeRows({1}); // 1002
        REQUIRE(settled());
        mods.undo();
        REQUIRE(mods.rows()[1].entry.identity.id == "1003");
        mods.removeRows({1});
        REQUIRE(settled());
        CHECK(ids(storedEntries(runner, id)) == Ids{"1001", "1002", "1004", "1005"});
        CHECK(ids(mods) == ids(storedEntries(runner, id)));
    }
    SECTION("an entry that the undo takes away is refused") {
        mods.addMods({"steam-workshop:1006"});
        REQUIRE(waitUntil([&] { return mods.count() == 6 && !mods.busy(); }));
        mods.undo();
        mods.setEnabled(5, false); // 1006, which the undo removes
        REQUIRE(settled());
        const std::vector<PlaysetEntry> stored = storedEntries(runner, id);
        CHECK(ids(stored) == Ids{"1001", "1002", "1003", "1004", "1005"});
        CHECK(std::all_of(stored.begin(), stored.end(),
                          [](const PlaysetEntry& entry) { return entry.enabled; }));
        CHECK(ids(mods) == ids(stored));
        const QVariantMap latest = backend.notifications()->latest();
        CHECK(latest.value("level").toString() == "warning");
        CHECK(latest.value("text").toString().contains("undo or redo"));
    }
    SECTION("an edit of several rows, one of which the undo takes away, changes none") {
        mods.addMods({"steam-workshop:1006"});
        REQUIRE(waitUntil([&] { return mods.count() == 6 && !mods.busy(); }));
        mods.undo();
        // Rows 4 and 5 are 1005 and 1006; the undo removes 1006.
        SECTION("enable/disable") {
            mods.setEnabledRows({4, 5}, false);
        }
        SECTION("keyboard move") {
            mods.moveRows({4, 5}, -1);
        }
        SECTION("drag and drop") {
            mods.moveRows({4, 5}, -3);
        }
        SECTION("removal") {
            mods.removeRows({4, 5});
        }
        REQUIRE(settled());
        const std::vector<PlaysetEntry> stored = storedEntries(runner, id);
        CHECK(ids(stored) == Ids{"1001", "1002", "1003", "1004", "1005"});
        CHECK(std::all_of(stored.begin(), stored.end(),
                          [](const PlaysetEntry& entry) { return entry.enabled; }));
        CHECK(ids(mods) == ids(stored));
        CHECK(mods.enabledCount() == 5);
        const QVariantMap latest = backend.notifications()->latest();
        CHECK(latest.value("level").toString() == "warning");
        CHECK(latest.value("text").toString().contains("Nothing was changed"));
    }
    backend.shutdown();
    runner.shutdown();
}

TEST_CASE("Importing with downloads off leaves nothing for a later session to download",
          "[app][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("4001", "Kept");
    world.addWorkshopMod("4002", "Gone");
    // Lets a coordinator open a Steam session for the game (never loaded here).
    writeText(world.install / platform::steam::steamApiLibraryName(), "placeholder");
    FakeAdapter adapter(world);
    const fs::path profile = temp.path() / "profile";
    {
        ServiceRunner runner(runnerOptions(profile, adapter)); // --no-steam
        const RunnerStart start = runner.start();
        REQUIRE(start.ok);
        Backend backend(runner, {});
        backend.initialize(start);
        REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
        REQUIRE_FALSE(backend.steamEnabled());
        backend.playsets()->create(QStringLiteral("Source"));
        REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
        const std::int64_t source = backend.activePlaysetId();
        backend.mods()->addMods({"steam-workshop:4001", "steam-workshop:4002"});
        REQUIRE(waitUntil([&] { return backend.mods()->count() == 2 && !backend.mods()->busy(); }));
        backend.sharing()->exportCode();
        REQUIRE(waitUntil([&] { return !backend.sharing()->lastExportCode().isEmpty(); }));

        // 4002 is not on this computer any more when the code is imported.
        fs::remove_all(world.workshop / "4002");
        const bool rescanned = runner.callBlocking(
            [](AppRuntime& runtime) { return runtime.games().rescanMods(kGame).ok(); });
        REQUIRE(rescanned);
        backend.sharing()->previewImport(backend.sharing()->lastExportCode());
        REQUIRE(waitUntil(
            [&] { return backend.sharing()->hasPreview() && !backend.sharing()->busy(); }));
        REQUIRE(backend.sharing()->preview().value("missing").toInt() == 1);
        REQUIRE(backend.sharing()->confirmImport("new", -1, QStringLiteral("Imported")));
        REQUIRE(waitUntil([&] {
            return backend.activePlaysetId() != source &&
                   backend.mods()->playsetId() == backend.activePlaysetId() &&
                   backend.mods()->count() == 2;
        }));

        // No job record at all, not even a withdrawn (cancelled) one.
        const std::size_t recorded = runner.callBlocking([](AppRuntime& runtime) {
            auto jobs =
                persistence::AcquisitionJobRepository(runtime.database()).listForGame(kGame);
            return jobs ? jobs.value().size() : std::size_t{99};
        });
        CHECK(recorded == 0);
        const ModRowData* gone = backend.mods()->row(1);
        REQUIRE(gone);
        CHECK(gone->entry.identity.id == "4002");
        CHECK(gone->status == "missing");
        CHECK(gone->statusText == "Missing");
        // The details panel: not the text of a cancelled download.
        CHECK(gone->problems.join('\n').toStdString() ==
              "Not installed. Download it from Steam Workshop.");
        CHECK(backend.notifications()->latest().value("text").toString().contains(
            "1 missing Workshop mod not queued because downloads are off in this session; "
            "restart Paradox Mod Manager normally, then use Download missing."));
        backend.shutdown();
        runner.shutdown();
    }

    // A later session with Steam resumes unfinished jobs on its own: none.
    struct RecordingSessions : services::WorkshopSessionHost {
        bool open(const GameKey&, const std::string&, const fs::path&) override {
            ++opened;
            return true;
        }
        void send(const GameKey&, const providers::HelperCommand&) override { ++sent; }
        void close(const GameKey&) override {}
        int opened = 0;
        int sent = 0;
    } sessions;
    RuntimeHosts hosts;
    hosts.sessions = &sessions;
    auto runtime = require(AppRuntime::create(hermeticOptions(profile, adapter), hosts));
    REQUIRE(runtime->acquisition());
    runtime->acquisition()->start(kGame);
    CHECK(sessions.opened == 0);
    CHECK(sessions.sent == 0);
}

TEST_CASE("Importing with downloads off reports the downloads from before as they stand",
          "[app][backend]") {
    // The import leaves earlier jobs alone, so a missing mod reads as its
    // newest job says, and the notification must say the same.
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    for (const char* id : {"4001", "4002", "4003", "4004"})
        world.addWorkshopMod(id, std::string("Mod ") + id);
    FakeAdapter adapter(world);
    ServiceRunner runner(runnerOptions(temp.path() / "profile", adapter)); // --no-steam
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    backend.playsets()->create(QStringLiteral("Source"));
    REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
    const std::int64_t source = backend.activePlaysetId();
    backend.mods()->addMods({"steam-workshop:4001", "steam-workshop:4002", "steam-workshop:4003",
                             "steam-workshop:4004"});
    REQUIRE(waitUntil([&] { return backend.mods()->count() == 4 && !backend.mods()->busy(); }));
    backend.sharing()->exportCode();
    REQUIRE(waitUntil([&] { return !backend.sharing()->lastExportCode().isEmpty(); }));

    // Only 4001 is still installed. An earlier session with Steam left 4003
    // queued, and the download of 4004 failed.
    for (const char* id : {"4002", "4003", "4004"})
        fs::remove_all(world.workshop / id);
    const std::vector<AcquisitionJob> before =
        require(runner.callBlocking([](AppRuntime& runtime) -> Result<std::vector<AcquisitionJob>> {
            PMM_TRY(runtime.games().rescanMods(kGame));
            persistence::AcquisitionJobRepository jobs(runtime.database());
            PMM_TRY(jobs.createOrReuse({kGame, source::SteamWorkshop, "4003"}, std::nullopt));
            auto failed = jobs.createOrReuse({kGame, source::SteamWorkshop, "4004"}, std::nullopt);
            if (!failed)
                return std::move(failed).error();
            failed.value().state = AcquisitionState::Failed;
            failed.value().attempts = 3;
            failed.value().errorCode = "timeout";
            failed.value().errorMessage = "Steam did not finish the download.";
            PMM_TRY(jobs.update(failed.value()));
            return jobs.listForGame(kGame);
        }));
    REQUIRE(before.size() == 2);

    backend.sharing()->previewImport(backend.sharing()->lastExportCode());
    REQUIRE(
        waitUntil([&] { return backend.sharing()->hasPreview() && !backend.sharing()->busy(); }));
    const QVariantMap preview = backend.sharing()->preview();
    REQUIRE(preview.value("missing").toInt() == 3);
    // The confirm button's tooltip says that nothing will be queued.
    CHECK(preview.contains("downloads"));
    CHECK_FALSE(preview.value("downloads").toBool());
    REQUIRE(backend.sharing()->confirmImport("new", -1, QStringLiteral("Imported")));
    REQUIRE(waitUntil([&] {
        return backend.activePlaysetId() != source &&
               backend.mods()->playsetId() == backend.activePlaysetId() &&
               backend.mods()->count() == 4;
    }));

    const std::vector<AcquisitionJob> after = require(runner.callBlocking([](AppRuntime& runtime) {
        return persistence::AcquisitionJobRepository(runtime.database()).listForGame(kGame);
    }));
    REQUIRE(after.size() == 2);
    for (std::size_t i = 0; i < after.size(); ++i) {
        CHECK(after[i].id == before[i].id);
        CHECK(after[i].state == before[i].state);
        CHECK(after[i].updatedAt == before[i].updatedAt);
    }
    const ModRowData* missing = backend.mods()->row(1);
    const ModRowData* queued = backend.mods()->row(2);
    const ModRowData* failed = backend.mods()->row(3);
    REQUIRE((missing && queued && failed));
    CHECK(missing->statusText == "Missing");
    CHECK(missing->problems.join('\n').toStdString() ==
          "Not installed. Download it from Steam Workshop.");
    CHECK(queued->statusText == "Queued");
    CHECK(failed->statusText == "Failed");
    CHECK(failed->problems.join('\n').toStdString() == "Steam did not finish the download.");
    const QString text = backend.notifications()->latest().value("text").toString();
    CHECK(text.contains("1 missing Workshop mod not queued because downloads are off in this "
                        "session; restart Paradox Mod Manager normally, then use Download "
                        "missing."));
    CHECK(text.contains("1 missing Workshop mod still queued from before; downloading resumes "
                        "when Paradox Mod Manager is restarted normally."));
    CHECK(text.contains("1 missing Workshop mod could not be downloaded before; restart Paradox "
                        "Mod Manager normally, then use Retry in Downloads."));
    backend.shutdown();
    runner.shutdown();
}

#if defined(PMM_STEAM_HELPER_PATH) && defined(PMM_FAKE_STEAM_API_PATH)

TEST_CASE("The downloads view follows a launch at once (fake Steam, Backend)",
          "[app][backend][helper][acquisition]") {
    // A launch parks the game's downloads, and a launch that fails after that
    // resumes them. Neither raises a coordinator event, and the next one may
    // not come for a long time: the view must not wait for it.
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("111111111", "Installed Item");
    const fs::path library = world.install / platform::steam::steamApiLibraryName();
    fs::copy_file(u8(PMM_FAKE_STEAM_API_PATH), library);
    FakeAdapter adapter(world);

    auto startFails = std::make_shared<std::atomic<bool>>(false);
    RunnerOptions options = runnerOptions(temp.path() / "profile", adapter);
    const auto hermetic = options.runtime.configureContext;
    options.runtime.configureContext = [hermetic, startFails](services::ServiceContext& context) {
        hermetic(context);
        context.startProcess =
            [startFails](const platform::ProcessSpec&) -> Result<platform::StartedProcess> {
            if (*startFails)
                return makeError(ErrorCode::ProcessStartFailed, "fake start failure");
            return platform::StartedProcess{4242};
        };
    };
    options.steam = true;
    options.metadata = false; // no network in tests
    options.helperExecutable = qs(PMM_STEAM_HELPER_PATH);
    options.tickInterval = std::chrono::milliseconds(100);
    options.configureCoordinator = [](services::CoordinatorOptions& coordinator) {
        coordinator.policy.retryBaseDelayMs = 3'600'000; // not due during the test
    };
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    REQUIRE(runner.steamEnabled());

    // The installed mod is enabled. 444444444 (the fake's SubscribeItem fails)
    // is disabled, so its job, waiting for the automatic retry, is still in
    // progress without blocking Play.
    const bool prepared = runner.callBlocking([](AppRuntime& runtime) {
        runtime.games().refreshAll();
        Playset playset;
        playset.game = kGame;
        playset.name = "Parked";
        playset.entries = {PlaysetEntry{ModIdentity{kGame, source::SteamWorkshop, "111111111"},
                                        true,
                                        "Installed Item",
                                        {},
                                        {}},
                           PlaysetEntry{ModIdentity{kGame, source::SteamWorkshop, "444444444"},
                                        false,
                                        "Failing Item",
                                        {},
                                        {}}};
        auto id = persistence::PlaysetRepository(runtime.database()).create(playset);
        return id && runtime.playsets().setActivePlayset(kGame, id.value()) &&
               runtime.acquisition()->acquireMissing(id.value());
    });
    REQUIRE(prepared);

    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    const auto job = [&]() -> const JobRowData* {
        const std::vector<JobRowData>& rows = backend.acquisition()->jobs()->rows();
        return rows.size() == 1 ? &rows[0] : nullptr;
    };
    const auto shows = [&](const char* jobState, const char* entryStatus) {
        const ModRowData* entry = backend.mods()->row(1);
        return job() && job()->state == QLatin1String(jobState) && entry &&
               entry->status == QLatin1String(entryStatus);
    };
    REQUIRE(waitUntil([&] { return shows("failed", "failed"); }, std::chrono::seconds(30)));
    REQUIRE(job()->errorMessage.contains(QLatin1String("Retrying automatically")));
    REQUIRE(waitUntil([&] { return backend.launcher()->canPlay(); }));
    // The idle helper session is closed: nothing is left to report.
    REQUIRE(waitUntil([&] {
        return !runner.callBlocking(
            [](AppRuntime& runtime) { return runtime.acquisition()->sessionOpen(kGame); });
    }));

    SECTION("a started game leaves the job parked") {
        QSignalSpy coordinatorEvents(&runner, &ServiceRunner::acquisitionChanged);
        backend.launcher()->play();
        REQUIRE(waitUntil([&] { return backend.launcher()->state() == QLatin1String("started"); }));
        // Saved as the queued run that the automatic retry would have started.
        CHECK(waitUntil([&] { return shows("pending", "pending"); }, std::chrono::seconds(3)));
        CHECK(job()->stateText == QLatin1String("Waiting to resume"));
        CHECK(backend.mods()->row(1)->statusText == QLatin1String("Queued"));
        CHECK_FALSE(backend.acquisition()->active());
        CHECK(coordinatorEvents.isEmpty());
    }
    SECTION("a game that cannot be started resumes the job") {
        // Without the Steam API library the resumed run stops at once, before
        // any helper starts that could report it. The library is free once the
        // closed helper has exited.
        REQUIRE(waitUntil([&] {
            std::error_code ec;
            fs::remove(library, ec);
            return !fs::exists(library, ec);
        }));
        *startFails = true;
        QSignalSpy coordinatorEvents(&runner, &ServiceRunner::acquisitionChanged);
        backend.launcher()->play();
        REQUIRE(waitUntil([&] { return backend.launcher()->state() == QLatin1String("failed"); }));
        CHECK(waitUntil([&] { return shows("needs_user_action", "needs_user_action"); },
                        std::chrono::seconds(3)));
        CHECK(job()->errorMessage.contains(QLatin1String("Steam API library")));
        CHECK(backend.acquisition()->providerStatus() == QLatin1String("unavailable"));
        CHECK(coordinatorEvents.isEmpty());
    }
    backend.shutdown();
    runner.shutdown();
}

#endif
