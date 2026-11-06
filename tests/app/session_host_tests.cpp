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
#include "core/log.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "services/playset_view.hpp"
#include "sharing/share_code.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <windows.h>
// Needs <windows.h> first.
#include <tlhelp32.h>

#include <QElapsedTimer>
#include <QSignalSpy>

#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

using namespace pmm;
using namespace pmm::app;
using namespace pmm::app::test;

namespace {

fs::path installFakeLibrary(const fs::path& folder) {
    fs::create_directories(folder);
    const fs::path library = folder / "steam_api64.dll";
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

// Process ids of the running pmm-steam-helper processes this test program started.
std::set<DWORD> helperChildren() {
    std::set<DWORD> pids;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return pids;
    const std::wstring name = u8(PMM_STEAM_HELPER_PATH).filename().wstring();
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    for (BOOL more = Process32FirstW(snapshot, &entry); more;
         more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ParentProcessID == GetCurrentProcessId() &&
            _wcsicmp(entry.szExeFile, name.c_str()) == 0)
            pids.insert(entry.th32ProcessID);
    }
    CloseHandle(snapshot);
    return pids;
}

// One helper process, held open from the moment it runs, so that its exit
// can be checked without the process id being reused meanwhile.
class HelperProcess {
public:
    // The helper started since `before` was taken; invalid unless exactly one.
    explicit HelperProcess(const std::set<DWORD>& before) {
        std::vector<DWORD> started;
        for (const DWORD pid : helperChildren()) {
            if (before.count(pid) == 0)
                started.push_back(pid);
        }
        if (started.size() != 1)
            return;
        m_pid = started.front();
        m_handle = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_pid);
    }
    ~HelperProcess() {
        if (m_handle)
            CloseHandle(m_handle);
    }
    HelperProcess(const HelperProcess&) = delete;
    HelperProcess& operator=(const HelperProcess&) = delete;

    bool valid() const noexcept { return m_handle != nullptr; }
    bool running() const { return m_handle && WaitForSingleObject(m_handle, 0) == WAIT_TIMEOUT; }

    // Suspends every thread: the process stays alive, keeps its app id and
    // acts on nothing, not even Quit or the end of its input.
    bool suspend() const {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;
        int suspended = 0;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof entry;
        for (BOOL more = Thread32First(snapshot, &entry); more;
             more = Thread32Next(snapshot, &entry)) {
            if (entry.th32OwnerProcessID != m_pid)
                continue;
            const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
            if (!thread)
                continue;
            if (SuspendThread(thread) != static_cast<DWORD>(-1))
                ++suspended;
            CloseHandle(thread);
        }
        CloseHandle(snapshot);
        return suspended > 0;
    }

private:
    DWORD m_pid = 0;
    HANDLE m_handle = nullptr;
};

class LogCapture {
public:
    LogCapture() {
        setLogSink([this](const LogRecord& record) {
            std::lock_guard lock(m_mutex);
            m_records.push_back(record);
        });
    }
    ~LogCapture() { setLogSink({}); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    // Records with `message` whose "game" field is `game`.
    std::size_t count(const std::string& message, const std::string& game) {
        std::lock_guard lock(m_mutex);
        std::size_t n = 0;
        for (const LogRecord& record : m_records) {
            if (record.message != message)
                continue;
            for (const auto& [key, value] : record.fields)
                n += key == "game" && value == game ? 1 : 0;
        }
        return n;
    }

private:
    std::mutex m_mutex;
    std::vector<LogRecord> m_records;
};

constexpr const char* kKilledWarning = "steam helper did not exit; terminating it";

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

TEST_CASE("waiting after close returns once the helper has exited, and nothing is delivered",
          "[app][helper]") {
    TempDir temp;
    const fs::path library = installFakeLibrary(temp.path() / "game");
    QtWorkshopSessionHost host(qs(PMM_STEAM_HELPER_PATH));
    Collected collected;
    collect(host, collected);
    LogCapture logs;

    const std::set<DWORD> before = helperChildren();
    REQUIRE(host.open("stellaris", "281990", library));
    const HelperProcess helper(before);
    REQUIRE(helper.valid());
    REQUIRE(waitUntil([&] { return collected.last<providers::ReadyEvent>() != nullptr; }));

    // Answers the closed session would otherwise deliver.
    host.send("stellaris", providers::StateCommand{{"111111111"}});
    host.send("stellaris", providers::PingCommand{});
    const std::size_t seen = collected.events.size();
    host.close("stellaris");
    host.waitUntilClosed("stellaris");
    CHECK_FALSE(helper.running());
    CHECK(logs.count(kKilledWarning, "stellaris") == 0); // it honoured Quit
    CHECK(collected.events.size() == seen);

    waitUntil([] { return false; }, std::chrono::milliseconds(500));
    CHECK(collected.events.size() == seen);
    CHECK_FALSE(collected.exitCode.has_value());
    host.waitUntilClosed("stellaris"); // nothing left to wait for
}

TEST_CASE("waiting kills a closed helper that does not exit within the grace period, per game",
          "[app][helper]") {
    // The real helper always honours Quit. A stuck one (blocked in a Steam
    // call, say) is the real helper process with every thread suspended: it
    // keeps running with its app id and reads nothing, so only a kill ends it.
    TempDir temp;
    const fs::path library = installFakeLibrary(temp.path() / "game");
    QtWorkshopSessionHost host(qs(PMM_STEAM_HELPER_PATH));
    constexpr std::chrono::milliseconds grace(500);
    host.setQuitGracePeriod(grace);
    std::map<GameKey, std::size_t> events;
    std::vector<GameKey> exits;
    std::map<GameKey, bool> ready;
    host.setSinks(
        [&](const GameKey& game, const providers::HelperEvent& event) {
            ++events[game];
            if (std::holds_alternative<providers::ReadyEvent>(event))
                ready[game] = std::get<providers::ReadyEvent>(event).ok;
        },
        [&](const GameKey& game, int) { exits.push_back(game); });
    LogCapture logs;

    std::set<DWORD> before = helperChildren();
    REQUIRE(host.open("stellaris", "281990", library));
    const HelperProcess stellaris(before);
    before = helperChildren();
    REQUIRE(host.open("eu4", "236850", library));
    const HelperProcess eu4(before);
    REQUIRE(stellaris.valid());
    REQUIRE(eu4.valid());
    REQUIRE(waitUntil([&] { return ready["stellaris"] && ready["eu4"]; }));
    REQUIRE(eu4.suspend());

    const std::map<GameKey, std::size_t> seen = events;
    host.close("eu4");
    host.close("stellaris");
    // Only this game's helper is waited for; the stuck one of the other game
    // is left alone.
    host.waitUntilClosed("stellaris");
    CHECK_FALSE(stellaris.running());
    CHECK(eu4.running());

    QElapsedTimer timer;
    timer.start();
    host.waitUntilClosed("eu4");
    const qint64 waited = timer.elapsed();
    CHECK_FALSE(eu4.running());
    CHECK(logs.count(kKilledWarning, "eu4") == 1);
    INFO("waited " << waited << " ms");
    CHECK(waited >= grace.count() - 100); // the grace period first, then the kill
    CHECK(waited < grace.count() + 5000);

    // Nothing of either session is delivered, during the waits or later.
    CHECK(events == seen);
    waitUntil([] { return false; }, std::chrono::milliseconds(800));
    CHECK(events == seen);
    CHECK(exits.empty());
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

TEST_CASE("Play's blockers follow a download that ends unavailable (fake Steam, Backend)",
          "[app][helper][acquisition][backend]") {
    // Seen live on 2026-10-30: the job's last transition arrived within the
    // launch-refresh throttle after the import, and the blocker kept saying
    // "Queued for download" because no further acquisition event came.
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
    options.configureCoordinator = [](services::CoordinatorOptions& coordinator) {
        coordinator.policy.pollIntervalMs = 100;
    };
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));

    // 333333333: the fake's SubscribeItem fails with FileNotFound (deleted item).
    sharing::Manifest manifest;
    manifest.game = kGame;
    manifest.playsetName = "With a deleted item";
    manifest.mods = {{source::SteamWorkshop, "111111111", "Installed Item", true, {}, {}},
                     {source::SteamWorkshop, "333333333", "Deleted Item", true, {}, {}}};
    backend.sharing()->previewImport(qs(require(sharing::encodeShareCode(manifest))));
    REQUIRE(
        waitUntil([&] { return backend.sharing()->hasPreview() && !backend.sharing()->busy(); }));
    REQUIRE(backend.sharing()->confirmImport(
        "new", -1, backend.sharing()->preview().value("proposedName").toString()));
    REQUIRE(waitUntil([&] { return backend.mods()->count() == 2; }));

    REQUIRE(waitUntil(
        [&] {
            const auto& jobs = backend.acquisition()->jobs()->rows();
            return jobs.size() == 1 && jobs[0].state == QLatin1String("unavailable");
        },
        std::chrono::seconds(30)));
    // At most one throttle interval (5 s) later, Play explains the real state.
    const auto blockers = [&] { return backend.launcher()->blockers().join(QLatin1Char('\n')); };
    REQUIRE(waitUntil([&] { return blockers().contains(QLatin1String("unavailable")); },
                      std::chrono::seconds(8)));
    CHECK_FALSE(blockers().contains(QLatin1String("Queued")));
    CHECK_FALSE(backend.launcher()->canPlay());

    backend.shutdown();
    runner.shutdown();
}

#endif
