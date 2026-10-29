// QtWorkshopSessionHost end to end: the real pmm-steam-helper loads the fake
// Steam API library (tests/unit/providers/fake_steam_api.cpp, our own code)
// copied under the accepted file name. Through the ServiceRunner, the
// acquisition coordinator drives a missing Workshop item from subscribe to
// download to installed. Steam itself is never involved.

#if defined(PMM_STEAM_HELPER_PATH) && defined(PMM_FAKE_STEAM_API_PATH)

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "app/core/session_host.hpp"
#include "app/gui/backend.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "services/playset_view.hpp"
#include "sharing/share_code.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>

#include <atomic>
#include <optional>
#include <set>
#include <variant>
#include <vector>

using namespace pmm;
using namespace pmm::app;
using namespace pmm::app::test;

namespace {

const char* acceptedLibraryName() {
    switch (currentOsPlatform()) {
    case OsPlatform::Windows:
        return "steam_api64.dll";
    case OsPlatform::MacOS:
        return "libsteam_api.dylib";
    case OsPlatform::Linux:
        return "libsteam_api.so";
    }
    return "steam_api64.dll";
}

fs::path installFakeLibrary(const fs::path& folder) {
    fs::create_directories(folder);
    const fs::path library = folder / acceptedLibraryName();
    fs::copy_file(u8(PMM_FAKE_STEAM_API_PATH), library, fs::copy_options::overwrite_existing);
    return library;
}

struct ScopedEnv {
    ScopedEnv(const char* name, const QByteArray& value) : m_name(name) { qputenv(name, value); }
    ~ScopedEnv() { qunsetenv(m_name); }
    const char* m_name;
};

struct Collected {
    std::vector<providers::HelperEvent> events;
    std::optional<int> exitCode;

    template <class T> const T* last() const {
        for (auto it = events.rbegin(); it != events.rend(); ++it) {
            if (const T* event = std::get_if<T>(&*it))
                return event;
        }
        return nullptr;
    }
};

void collect(QtWorkshopSessionHost& host, Collected& collected) {
    host.setSinks(
        [&collected](const GameKey& game, const providers::HelperEvent& event) {
            CHECK(game == "stellaris");
            collected.events.push_back(event);
        },
        [&collected](const GameKey&, int exitCode) { collected.exitCode = exitCode; });
}

} // namespace

TEST_CASE("session host talks to the real helper and stops delivering after close",
          "[app][helper]") {
    TempDir temp;
    const fs::path library = installFakeLibrary(temp.path() / u8("game Ünïcødé"));
    QtWorkshopSessionHost host(qs(PMM_STEAM_HELPER_PATH));
    Collected collected;
    collect(host, collected);

    REQUIRE(host.open("stellaris", "281990", library));
    CHECK(collected.events.empty()); // never delivered from inside open()
    CHECK(host.isOpen("stellaris"));
    REQUIRE(waitUntil([&] { return collected.last<providers::ReadyEvent>() != nullptr; }));
    const providers::ReadyEvent ready = *collected.last<providers::ReadyEvent>();
    CHECK(ready.ok);
    CHECK(ready.loggedOn);
    CHECK(ready.ownsApp);
    CHECK(ready.appId == "281990");

    host.send("stellaris", providers::StateCommand{{"111111111"}});
    REQUIRE(waitUntil([&] { return collected.last<providers::ItemStateEvent>() != nullptr; }));
    const providers::ItemStatus& item = collected.last<providers::ItemStateEvent>()->status;
    CHECK(item.id == "111111111");
    CHECK((item.flags & 4u) != 0); // k_EItemStateInstalled

    host.send("stellaris", providers::PingCommand{});
    host.close("stellaris");
    CHECK_FALSE(host.isOpen("stellaris"));
    const std::size_t seen = collected.events.size();
    waitUntil([] { return false; }, std::chrono::milliseconds(700));
    CHECK(collected.events.size() == seen);
    CHECK_FALSE(collected.exitCode.has_value());
    host.closeAll();
}

TEST_CASE("session host reports a refused session and the helper's exit code", "[app][helper]") {
    TempDir temp;
    const fs::path library = installFakeLibrary(temp.path() / "game");
    ScopedEnv mode("PMM_FAKE_STEAM_MODE", "no_client");
    QtWorkshopSessionHost host(qs(PMM_STEAM_HELPER_PATH));
    Collected collected;
    collect(host, collected);
    REQUIRE(host.open("stellaris", "281990", library));
    REQUIRE(waitUntil([&] { return collected.exitCode.has_value(); }));
    const providers::ReadyEvent* ready = collected.last<providers::ReadyEvent>();
    REQUIRE(ready);
    CHECK_FALSE(ready->ok);
    CHECK(ready->error == providers::HelperFailure::SteamNotRunning);
    CHECK(*collected.exitCode == static_cast<int>(providers::HelperExitCode::SteamNotRunning));
    CHECK_FALSE(host.isOpen("stellaris"));
}

TEST_CASE("session host refuses to start a missing helper", "[app][helper]") {
    TempDir temp;
    const fs::path library = installFakeLibrary(temp.path() / "game");
    QtWorkshopSessionHost host(qpath(temp.path() / "no-such-helper.exe"));
    CHECK_FALSE(host.open("stellaris", "281990", library));
    CHECK_FALSE(host.isOpen("stellaris"));
}

TEST_CASE("acquisition runs subscribe, download and install through the runner (fake Steam)",
          "[app][helper][acquisition]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    installFakeLibrary(world.install);
    FakeAdapter adapter(world);

    RunnerOptions options;
    options.runtime = hermeticOptions(temp.path() / "profile", adapter);
    options.steam = true;
    options.metadata = false; // no network in tests
    options.helperExecutable = qs(PMM_STEAM_HELPER_PATH);
    options.tickInterval = std::chrono::milliseconds(100);
    auto verified = std::make_shared<std::atomic<int>>(0);
    const fs::path itemFolder = world.workshop / "222222222";
    options.configureCoordinator = [verified,
                                    itemFolder](services::CoordinatorOptions& coordinator) {
        coordinator.policy.pollIntervalMs = 100;
        coordinator.progressPersistIntervalMs = 0;
        // The fake reports an install folder that does not exist; stand in for
        // Steam having written the content where the adapter scans.
        coordinator.verifyContent = [verified, itemFolder](const fs::path&) -> Status {
            ++*verified;
            std::error_code ec;
            fs::create_directories(itemFolder, ec);
            return platform::writeFileAtomic(itemFolder / "name.txt", "Downloaded item");
        };
    };
    ServiceRunner runner(options);
    QSignalSpy changes(&runner, &ServiceRunner::acquisitionChanged);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    REQUIRE(runner.steamEnabled());

    const std::int64_t playsetId = runner.callBlocking([](AppRuntime& runtime) -> std::int64_t {
        runtime.games().refreshAll();
        Playset playset;
        playset.game = kGame;
        playset.name = "Downloads";
        playset.entries = {PlaysetEntry{ModIdentity{kGame, source::SteamWorkshop, "222222222"},
                                        true,
                                        "Fake Workshop Item",
                                        {},
                                        {}}};
        auto id = persistence::PlaysetRepository(runtime.database()).create(playset);
        return id ? id.value() : -1;
    });
    REQUIRE(playsetId > 0);

    const bool queued = runner.callBlocking([playsetId](AppRuntime& runtime) {
        auto update = runtime.acquisition()->acquireMissing(playsetId);
        return update.ok();
    });
    REQUIRE(queued);

    std::set<std::string> seen;
    const auto installed = [&] {
        const auto snapshot = runner.callBlocking(
            [](AppRuntime& runtime) { return runtime.acquisition()->snapshot(kGame); });
        bool done = false;
        for (const services::JobSnapshot& job : snapshot.jobs) {
            seen.insert(toString(job.job.state));
            done = done || job.job.state == AcquisitionState::Installed;
        }
        return done;
    };
    REQUIRE(waitUntil(installed, std::chrono::seconds(60)));
    CHECK(*verified >= 1);
    CHECK(changes.count() > 0);
    INFO("observed job states: " << [&] {
        std::string all;
        for (const std::string& s : seen)
            all += s + " ";
        return all;
    }());
    CHECK(seen.count("installed") == 1);

    // The inventory rescan saw the content: the entry is ready to launch.
    REQUIRE(waitUntil([&] {
        return runner.callBlocking([playsetId](AppRuntime& runtime) {
            auto view = services::loadPlaysetView(runtime.context(), playsetId,
                                                  runtime.acquisition()->liveJobs(kGame));
            return view && view.value().rows.size() == 1 &&
                   view.value().rows[0].readiness.status == EntryStatus::Ready;
        });
    }));
    // The persisted job says installed and the helper session was closed.
    const bool persisted = runner.callBlocking([](AppRuntime& runtime) {
        auto jobs = persistence::AcquisitionJobRepository(runtime.database()).listForGame(kGame);
        return jobs && jobs.value().size() == 1 &&
               jobs.value()[0].state == AcquisitionState::Installed;
    });
    CHECK(persisted);
    CHECK(waitUntil([&] {
        return !runner.callBlocking(
            [](AppRuntime& runtime) { return runtime.acquisition()->sessionOpen(kGame); });
    }));
    runner.shutdown();
}

TEST_CASE("an imported code with a missing Workshop mod becomes playable (fake Steam, Backend)",
          "[app][helper][acquisition][backend]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    installFakeLibrary(world.install);
    world.addWorkshopMod("111111111", "Installed Item");
    FakeAdapter adapter(world);

    RunnerOptions options;
    options.runtime = hermeticOptions(temp.path() / "profile", adapter);
    options.steam = true;
    options.metadata = false;
    options.helperExecutable = qs(PMM_STEAM_HELPER_PATH);
    options.tickInterval = std::chrono::milliseconds(100);
    const fs::path itemFolder = world.workshop / "222222222";
    options.configureCoordinator = [itemFolder](services::CoordinatorOptions& coordinator) {
        coordinator.policy.pollIntervalMs = 100;
        coordinator.verifyContent = [itemFolder](const fs::path&) -> Status {
            std::error_code ec;
            fs::create_directories(itemFolder, ec);
            return platform::writeFileAtomic(itemFolder / "name.txt", "Downloaded Item");
        };
    };
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
    CHECK(backend.acquisition()->downloadsEnabled());

    // A friend's code: one installed mod and one missing, enabled Workshop mod.
    sharing::Manifest manifest;
    manifest.game = kGame;
    manifest.playsetName = "From a friend";
    manifest.mods = {{source::SteamWorkshop, "111111111", "Installed Item", true, {}, {}},
                     {source::SteamWorkshop, "222222222", "Downloaded Item", true, {}, {}}};
    const QString code = qs(require(sharing::encodeShareCode(manifest)));

    backend.sharing()->previewImport(code);
    REQUIRE(
        waitUntil([&] { return backend.sharing()->hasPreview() && !backend.sharing()->busy(); }));
    const QVariantMap preview = backend.sharing()->preview();
    REQUIRE(preview.value("ok").toBool());
    CHECK(preview.value("missing").toInt() == 1);
    CHECK(preview.value("missingEnabled").toInt() == 1);

    REQUIRE(backend.sharing()->confirmImport("new", -1, preview.value("proposedName").toString()));
    REQUIRE(waitUntil([&] { return backend.mods()->count() == 2; }));
    // Missing enabled content blocks Play until it is installed.
    CHECK_FALSE(backend.launcher()->canPlay());

    // The coordinator works the queued job: subscribe, download, verify, rescan.
    REQUIRE(waitUntil(
        [&] {
            const ModRowData* row = backend.mods()->row(1);
            return row && row->status == QLatin1String("ready");
        },
        std::chrono::seconds(60)));
    CHECK(backend.mods()->rows()[1].name == "Downloaded Item");
    CHECK(backend.mods()->missingCount() == 0);
    CHECK(backend.acquisition()->providerStatus() == "ready");
    REQUIRE(waitUntil([&] { return backend.launcher()->blockers().isEmpty(); }));
    CHECK(backend.launcher()->canPlay());
    CHECK(backend.inventory()->totalCount() == 2);

    backend.shutdown();
    runner.shutdown();
}

#endif
