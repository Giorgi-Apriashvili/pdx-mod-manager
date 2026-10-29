// ServiceRunner marshalling and shutdown, and the Backend driving the real
// services through it: edits are persisted and undoable, playset management,
// the share-code round trip and start-up failures.

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/settings_repository.hpp"
#include "platform/steam.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QPointer>
#include <QThread>

#include <algorithm>
#include <memory>
#include <string>
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
    CHECK(backend.launcher()->blockers().join(' ').contains("No active playset"));

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

TEST_CASE("Launch blockers explain a running game and a missing playset", "[app][backend]") {
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
                std::vector<platform::RunningProcess>{game});
        };
    };
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    const QString blockers = backend.launcher()->blockers().join('\n');
    CHECK(blockers.contains("already running"));
    CHECK(blockers.contains("No active playset"));
    CHECK_FALSE(backend.launcher()->canPlay());

    backend.playsets()->create(QStringLiteral("Running"));
    REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
    REQUIRE(waitUntil(
        [&] { return !backend.launcher()->blockers().join('\n').contains("No active"); }));
    CHECK(backend.launcher()->blockers().join('\n').contains("already running"));
    CHECK_FALSE(backend.launcher()->canPlay());
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

        const std::size_t unfinished = runner.callBlocking([](AppRuntime& runtime) {
            auto jobs = persistence::AcquisitionJobRepository(runtime.database()).listUnfinished();
            return jobs ? jobs.value().size() : std::size_t{99};
        });
        CHECK(unfinished == 0);
        const ModRowData* gone = backend.mods()->row(1);
        REQUIRE(gone);
        CHECK(gone->entry.identity.id == "4002");
        CHECK(gone->status == "missing");
        CHECK(gone->statusText == "Missing");
        CHECK(backend.notifications()->latest().value("text").toString().contains(
            "1 missing Workshop mod listed; downloads are off in this session."));
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
