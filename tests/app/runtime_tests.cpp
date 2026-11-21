// AppRuntime start-up: profile folders, the file log, database versions,
// retention and the startup notices; plus the rotating file log sink.

#include "app/core/app_runtime.hpp"
#include "app/core/file_log_sink.hpp"
#include "app/core/profile_lock.hpp"
#include "app/core/qt_util.hpp"
#include "app/core/view_data.hpp"
#include "app/gui/mod_details.hpp"
#include "app/gui/mod_list_model.hpp"
#include "app/gui/notification_center.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/sqlite.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QFile>
#include <QLockFile>

#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace pmm;
using namespace pmm::app;
using namespace pmm::app::test;

namespace {

std::size_t countLogs(const fs::path& folder) {
    std::size_t count = 0;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
        count += it->path().extension() == ".log" ? 1 : 0;
    return count;
}

} // namespace

TEST_CASE("AppRuntime creates the profile, logs and services", "[app][runtime]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    RuntimeOptions options = hermeticOptions(temp.path() / u8("profile Ünïcødé"), adapter);
    options.installLogSink = true;
    {
        auto runtime = require(AppRuntime::create(options));
        const services::AppPaths& paths = runtime->paths();
        CHECK(fs::is_directory(paths.logsDir));
        CHECK(fs::is_directory(paths.cacheDir));
        CHECK(fs::is_directory(paths.transactionsDir));
        CHECK(fs::is_regular_file(paths.databaseFile));
        CHECK(runtime->acquisition() == nullptr); // no session host given
        CHECK(runtime->notices().empty());
        const auto states = runtime->games().refreshAll();
        CHECK(std::any_of(states.begin(), states.end(), [](const services::GameState& s) {
            return s.key == kGame && s.installation.has_value();
        }));
        logInfo("test", "a line for the log file");
        REQUIRE_FALSE(runtime->logFile().empty());
        CHECK(fs::file_size(runtime->logFile()) > 0);
    }
    CHECK(countLogs(temp.path() / u8("profile Ünïcødé") / "logs") == 1);
}

TEST_CASE("AppRuntime refuses databases it cannot use with an actionable message",
          "[app][runtime]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    const fs::path profile = temp.path() / "profile";

    SECTION("not a database") {
        writeText(profile / "state.sqlite", "garbage");
        auto runtime = AppRuntime::create(hermeticOptions(profile, adapter));
        REQUIRE_FALSE(runtime.ok());
        CHECK(runtime.error().message.find("state.sqlite") != std::string::npos);
    }
    SECTION("written by a newer version") {
        {
            auto runtime = require(AppRuntime::create(hermeticOptions(profile, adapter)));
        }
        {
            auto connection = require(persistence::Connection::open(profile / "state.sqlite"));
            requireOk(connection.exec("INSERT INTO schema_migrations (version, name, applied_at) "
                                      "VALUES (999, 'from the future', 0)"));
        }
        auto runtime = AppRuntime::create(hermeticOptions(profile, adapter));
        REQUIRE_FALSE(runtime.ok());
        CHECK(runtime.error().code == ErrorCode::UnsupportedVersion);
        CHECK(runtime.error().message.find("newer version") != std::string::npos);
    }
}

TEST_CASE("AppRuntime applies retention at start-up", "[app][runtime]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    const fs::path profile = temp.path() / "profile";
    std::int64_t now = 1'800'000'000'000;
    RuntimeOptions options = hermeticOptions(profile, adapter);
    const auto configure = options.configureContext;
    options.configureContext = [&now, configure](services::ServiceContext& context) {
        configure(context);
        context.clock = [&now] { return now; };
    };
    {
        auto runtime = require(AppRuntime::create(options));
        persistence::AcquisitionJobRepository jobs(runtime->database());
        auto job = require(
            jobs.createOrReuse(ModIdentity{kGame, source::SteamWorkshop, "123"}, std::nullopt));
        job.state = AcquisitionState::Cancelled;
        job.errorCode = "cancelled"; // by the user, so only the retention removes it
        requireOk(jobs.update(job));
        CHECK(require(jobs.listForGame(kGame)).size() == 1);
    }
    // Finished jobs are kept for 7 days. The repository stamps rows with the
    // wall clock, so "now" moves far ahead for the next start.
    now = nowUnixMs() + AppRuntime::kJobRetentionMs + 60'000;
    auto runtime = require(AppRuntime::create(options));
    persistence::AcquisitionJobRepository jobs(runtime->database());
    CHECK(require(jobs.listForGame(kGame)).empty());
}

TEST_CASE("AppRuntime removes the jobs that imports with downloads off withdrew at start-up",
          "[app][runtime]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    const fs::path profile = temp.path() / "profile";
    std::int64_t cancelled = 0;
    {
        auto runtime = require(AppRuntime::create(hermeticOptions(profile, adapter)));
        persistence::AcquisitionJobRepository jobs(runtime->database());
        // What an import with downloads off left in 0.1.0 and 0.1.1.
        auto withdrawn = require(
            jobs.createOrReuse(ModIdentity{kGame, source::SteamWorkshop, "123"}, std::nullopt));
        withdrawn.state = AcquisitionState::Cancelled;
        requireOk(jobs.update(withdrawn));
        // A download the user cancelled.
        auto user = require(
            jobs.createOrReuse(ModIdentity{kGame, source::SteamWorkshop, "456"}, std::nullopt));
        user.state = AcquisitionState::Cancelled;
        user.errorCode = "cancelled";
        user.errorMessage = "Cancelled before anything was requested from Steam.";
        requireOk(jobs.update(user));
        cancelled = user.id;
        CHECK(require(jobs.listForGame(kGame)).size() == 2);
    }
    // Long before the 7-day retention.
    auto runtime = require(AppRuntime::create(hermeticOptions(profile, adapter)));
    persistence::AcquisitionJobRepository jobs(runtime->database());
    const std::vector<AcquisitionJob> remaining = require(jobs.listForGame(kGame));
    REQUIRE(remaining.size() == 1);
    CHECK(remaining[0].id == cancelled);
}

TEST_CASE("AppRuntime reports interrupted configuration changes without recovering them",
          "[app][runtime]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    const fs::path profile = temp.path() / "profile";
    const fs::path target = world.userData / "load_order.txt";
    writeText(target, "before");
    {
        platform::FileTransactionManager manager(profile / "transactions");
        platform::FileTransactionFaults faults;
        faults.crashAfterWrites = 1;
        manager.setFaultsForTesting(faults);
        auto applied = manager.apply("launch stellaris playset 1 (x)", {{target, "after"}});
        REQUIRE_FALSE(applied.ok());
    }
    auto runtime = require(AppRuntime::create(hermeticOptions(profile, adapter)));
    REQUIRE(runtime->notices().size() == 1);
    CHECK(runtime->notices()[0].level == LogLevel::Warning);
    CHECK(require(runtime->launcher().interruptedTransactions()).size() == 1);
    std::ifstream in(target);
    std::string contents;
    std::getline(in, contents);
    CHECK(contents == "after"); // left for the user's explicit Recover
}

TEST_CASE("Read-only launch data keeps every blocker except the write probes", "[app][runtime]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("2000000001", "UI Overhaul");
    FakeAdapter adapter(world);
    RuntimeOptions options = hermeticOptions(temp.path() / "profile", adapter);
    auto running = std::make_shared<bool>(false);
    options.configureContext = [base = options.configureContext,
                                running](services::ServiceContext& context) {
        base(context);
        context.listProcesses = [running] {
            std::vector<platform::RunningProcess> processes{steamClient()};
            if (*running)
                processes.push_back({1234, kExeName, {}});
            return Result<std::vector<platform::RunningProcess>>(std::move(processes));
        };
    };
    auto runtime = require(AppRuntime::create(options));
    runtime->games().refreshAll();
    persistence::PlaysetRepository repo(runtime->database());
    Playset playset;
    playset.game = kGame;
    playset.name = "Main";
    PlaysetEntry entry;
    entry.identity = ModIdentity{kGame, source::SteamWorkshop, "2000000001"};
    playset.entries = {entry};
    const std::int64_t id = require(repo.create(playset));

    const QString game = QString::fromLatin1(kGame);
    QueryOptions normal;
    QueryOptions readOnly;
    readOnly.readOnly = true;
    const auto joined = [](const LaunchData& data) { return data.blockers.join(QLatin1Char('|')); };

    CHECK(queries::launchData(*runtime, game, id, normal).canPlay);
    // Without a playset the same check runs, and the game starts without mods.
    constexpr std::int64_t kNoPlayset = -1;
    const LaunchData vanilla = queries::launchData(*runtime, game, kNoPlayset, normal);
    CHECK(vanilla.canPlay);
    CHECK(vanilla.blockers.isEmpty());
    CHECK(vanilla.warnings ==
          QStringList{QStringLiteral("No playset is selected, so the game starts without mods.")});
    CHECK_FALSE(queries::launchData(*runtime, game, kNoPlayset, readOnly).canPlay);
    const LaunchData verifying = queries::launchData(*runtime, game, id, readOnly);
    CHECK_FALSE(verifying.canPlay);
    CHECK_FALSE(verifying.canRestoreBackup);
    REQUIRE(verifying.blockers.size() == 1);
    CHECK(verifying.blockers.front().startsWith(QStringLiteral("Read-only verification mode")));

    // The same blockers as a normal session (here: the game is running), plus
    // the read-only note.
    *running = true;
    const LaunchData blocked = queries::launchData(*runtime, game, id, normal);
    const LaunchData blockedReadOnly = queries::launchData(*runtime, game, id, readOnly);
    CHECK_FALSE(blocked.canPlay);
    CHECK(blocked.runningInstance);
    CHECK_FALSE(blocked.steamNotRunning);
    CHECK(joined(blocked).contains(QStringLiteral("already running")));
    REQUIRE(blockedReadOnly.blockers.size() == blocked.blockers.size() + 1);
    for (qsizetype i = 0; i < blocked.blockers.size(); ++i)
        CHECK(blockedReadOnly.blockers[i] == blocked.blockers[i]);
    CHECK(joined(queries::launchData(*runtime, game, kNoPlayset, normal))
              .contains(QStringLiteral("already running")));

    // Write probes run in normal sessions only: a file where the mod folder belongs.
    *running = false;
    fs::remove_all(world.userData / "mod");
    writeText(world.userData / "mod", "not a folder");
    CHECK(joined(queries::launchData(*runtime, game, id, normal))
              .contains(QStringLiteral("mod folder")));
    CHECK_FALSE(joined(queries::launchData(*runtime, game, id, readOnly))
                    .contains(QStringLiteral("mod folder")));
    CHECK(joined(queries::launchData(*runtime, game, kNoPlayset, normal))
              .contains(QStringLiteral("mod folder")));
    CHECK_FALSE(joined(queries::launchData(*runtime, game, kNoPlayset, readOnly))
                    .contains(QStringLiteral("mod folder")));
}

TEST_CASE("Launch data says when Play waits for the Steam client", "[app][runtime]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    RuntimeOptions options = hermeticOptions(temp.path() / "profile", adapter);
    auto steam = std::make_shared<bool>(false);
    options.configureContext = [base = options.configureContext,
                                steam](services::ServiceContext& context) {
        base(context);
        context.listProcesses = [steam] {
            std::vector<platform::RunningProcess> processes{{7, "explorer.exe", {}}};
            if (*steam)
                processes.push_back(steamClient());
            return Result<std::vector<platform::RunningProcess>>(std::move(processes));
        };
    };
    auto runtime = require(AppRuntime::create(options));
    runtime->games().refreshAll();
    const QString game = QString::fromLatin1(kGame);
    constexpr std::int64_t kNoPlayset = -1;
    const QueryOptions normal;
    QueryOptions readOnly;
    readOnly.readOnly = true;

    const LaunchData closed = queries::launchData(*runtime, game, kNoPlayset, normal);
    CHECK_FALSE(closed.canPlay);
    CHECK(closed.steamNotRunning);
    CHECK_FALSE(closed.runningInstance);
    CHECK(closed.blockers ==
          QStringList{QStringLiteral(
              "Steam is not running. Start Steam and sign in, then launch Stellaris.")});
    // A read-only session reports it as well, before its own note.
    const LaunchData verifying = queries::launchData(*runtime, game, kNoPlayset, readOnly);
    CHECK(verifying.steamNotRunning);
    REQUIRE(verifying.blockers.size() == 2);
    CHECK(verifying.blockers.front() == closed.blockers.front());

    *steam = true;
    const LaunchData open = queries::launchData(*runtime, game, kNoPlayset, normal);
    CHECK(open.canPlay);
    CHECK_FALSE(open.steamNotRunning);
    CHECK(open.blockers.isEmpty());
}

TEST_CASE("Mod details list the mods a mod names as each game treats them", "[app][runtime]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    auto runtime = require(AppRuntime::create(hermeticOptions(temp.path() / "profile", adapter)));
    // In each game the overhaul (#1) names a mod that comes after it (#2), a
    // disabled one (#3), one installed elsewhere, one that is not installed
    // and #2 again in other letter case; #2 names #1, and the disabled #3
    // names #2 and #4, which comes after it.
    const auto playsetOf = [&runtime](const GameKey& game) {
        const auto mod = [&game](const std::string& id, const std::string& name,
                                 std::vector<std::string> dependencies) {
            InstalledMod m;
            m.identity = ModIdentity{game, source::SteamWorkshop, id};
            m.descriptor.name = name;
            m.descriptor.dependencies = std::move(dependencies);
            return m;
        };
        persistence::InventoryRepository inventory(runtime->database());
        requireOk(inventory.replaceForGame(
            game,
            {mod("6000000001", "Example Overhaul",
                 {"Example Map", "Example Conversion", "Example Elsewhere", "Example Missing",
                  "example MAP"}),
             mod("6000000002", "Example Map", {"Example Overhaul"}),
             mod("6000000003", "Example Conversion", {"Example Map", "Example Late"}),
             mod("6000000004", "Example Elsewhere", {}), mod("6000000005", "Example Late", {})}));
        Playset playset;
        playset.game = game;
        playset.name = "Main";
        for (const char* id : {"6000000001", "6000000002", "6000000003", "6000000005"}) {
            PlaysetEntry entry;
            entry.identity = ModIdentity{game, source::SteamWorkshop, id};
            entry.enabled = std::string_view(id) != "6000000003";
            playset.entries.push_back(entry);
        }
        persistence::PlaysetRepository repo(runtime->database());
        return queries::playsetState(*runtime, require(repo.create(playset)));
    };
    const auto shown = [](const ModDetails& details) {
        QStringList out;
        for (const QVariant& item : details.dependencies()) {
            const QVariantMap map = item.toMap();
            out.push_back(map.value(QStringLiteral("name")).toString() + " | " +
                          map.value(QStringLiteral("state")).toString() + " | " +
                          map.value(QStringLiteral("text")).toString());
        }
        return out;
    };
    ModListModel model(nullptr);
    NotificationCenter notifications;
    ModDetails details(&model, &notifications);
    const QStringList notNeeded{
        "Example Conversion | load_after | Disabled in this playset; not needed",
        "Example Elsewhere | load_after | Installed, not in this playset; not needed",
        "Example Missing | load_after | Not installed; not needed"};

    // Neither EU4 nor Hearts of Iron IV needs them or uses their place in the
    // list: no warnings, neutral states, each mod once, whatever the spelling.
    // Hearts of Iron IV's wiki documents what they do; for EU4 the note says
    // only what is established.
    const std::map<std::string, QString> notes{
        {"eu4", "Europa Universalis IV does not need these mods. This mod names them to load "
                "before it when they are enabled."},
        {"hoi4", "Hearts of Iron IV loads this mod after these mods when they are enabled. It "
                 "does not need them."}};
    for (const auto& [game, note] : notes) {
        INFO(game);
        const PlaysetStateData loadAfter = playsetOf(game);
        REQUIRE(loadAfter.rows.size() == 4);
        for (const ModRowData& row : loadAfter.rows)
            CHECK(row.warnings.isEmpty());
        CHECK(loadAfter.counts.warning == 0);
        model.resetTo(loadAfter);
        details.setRow(0);
        CHECK(details.dependenciesLoadAfter());
        CHECK(details.dependenciesNote() == note);
        CHECK(shown(details) ==
              QStringList{"Example Map | load_after | Enabled in this playset"} + notNeeded);
        details.setRow(1);
        CHECK(shown(details) ==
              QStringList{"Example Overhaul | load_after | Enabled in this playset"});
        details.setRow(2);
        CHECK(shown(details) == QStringList{"Example Map | load_after | Enabled in this playset",
                                            "Example Late | load_after | Enabled in this playset"});
    }

    // Stellaris and CK3 do not need them either, but an enabled one that comes
    // later in the list is an order warning, never "Requires".
    for (const char* game : {"ck3", "stellaris"}) {
        INFO(game);
        const PlaysetStateData orderHint = playsetOf(game);
        REQUIRE(orderHint.rows.size() == 4);
        const QString hint = "Lists \"Example Map\" (#2) to load before this mod (#1), but that "
                             "mod comes later in this playset.";
        CHECK(orderHint.rows[0].warnings == QStringList{hint});
        CHECK(orderHint.rows[1].warnings.isEmpty());
        CHECK(orderHint.rows[2].warnings.isEmpty());
        CHECK(orderHint.rows[3].warnings.isEmpty());
        CHECK(orderHint.counts.warning == 1);
        model.resetTo(orderHint);
        details.setRow(0);
        CHECK(details.dependenciesLoadAfter());
        CHECK(details.dependenciesNote() ==
              QString(game == std::string_view("ck3") ? "Crusader Kings III" : "Stellaris") +
                  " does not need these mods. This mod names them to load before it when they "
                  "are enabled.");
        CHECK(shown(details) == QStringList{"Example Map | order | " + hint} + notNeeded);
        details.setRow(1);
        CHECK(shown(details) ==
              QStringList{"Example Overhaul | load_after | Enabled in this playset"});
        // A disabled mod is not checked: the enabled mod it names after it
        // (#4) is no warning.
        details.setRow(2);
        CHECK(shown(details) == QStringList{"Example Map | load_after | Enabled in this playset",
                                            "Example Late | load_after | Enabled in this playset"});
    }

    // Victoria 3 requires them: the same states and texts as Stellaris had.
    const PlaysetStateData vic3 = playsetOf("vic3");
    REQUIRE(vic3.rows.size() == 4);
    CHECK(vic3.rows[0].warnings.size() == 5);
    CHECK(vic3.counts.warning == 1);
    model.resetTo(vic3);
    details.setRow(0);
    CHECK_FALSE(details.dependenciesLoadAfter());
    CHECK(details.dependenciesNote().isEmpty());
    const QStringList required = shown(details);
    REQUIRE(required.size() == 5);
    CHECK(required.mid(0, 4) ==
          QStringList{"Example Map | order | Requires \"Example Map\" (#2), which must load before "
                      "this mod (#1). Move it above this mod.",
                      "Example Conversion | disabled | Requires \"Example Conversion\" (#3), which "
                      "is disabled in this playset.",
                      "Example Elsewhere | not_in_playset | Requires \"Example Elsewhere\", which "
                      "is installed but not in this playset.",
                      "Example Missing | missing | Requires \"Example Missing\", which is not "
                      "installed."});
    // Every spelling, as before.
    CHECK(required[4] == "example MAP | order | Requires \"example MAP\" (#2), which must load "
                         "before this mod (#1). Move it above this mod.");
    details.setRow(1);
    CHECK(shown(details) ==
          QStringList{"Example Overhaul | ok | Enabled in this playset and loads earlier"});
    details.setRow(2);
    CHECK(shown(details) ==
          QStringList{"Example Map | unchecked | Checked when this mod is enabled",
                      "Example Late | unchecked | Checked when this mod is enabled"});
}

TEST_CASE("An active playset that cannot be read blocks Play instead of a launch without mods",
          "[app][runtime]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("2000000001", "UI Overhaul");
    FakeAdapter adapter(world);
    auto runtime = require(AppRuntime::create(hermeticOptions(temp.path() / "profile", adapter)));
    runtime->games().refreshAll();
    persistence::PlaysetRepository repo(runtime->database());
    Playset playset;
    playset.game = kGame;
    playset.name = "Main";
    PlaysetEntry entry;
    entry.identity = ModIdentity{kGame, source::SteamWorkshop, "2000000001"};
    playset.entries = {entry};
    const std::int64_t id = require(repo.create(playset));
    requireOk(runtime->playsets().setActivePlayset(kGame, id));
    const QString game = QString::fromLatin1(kGame);
    const QueryOptions normal;
    REQUIRE(queries::gameContext(*runtime, game, std::nullopt, normal).launch.canPlay);
    const auto damage = [&](const char* sql) {
        auto access = runtime->database().access();
        requireOk(access.connection().exec(sql));
    };

    SECTION("its mods cannot be loaded: the selected playset is checked") {
        damage("UPDATE installed_mods SET state = 'bogus'");
        const GameContextData data = queries::gameContext(*runtime, game, std::nullopt, normal);
        CHECK(data.list.activeId == id);
        CHECK(data.playset.playsetId == -1);
        CHECK_FALSE(data.error.isEmpty());
        CHECK_FALSE(data.launch.canPlay);
        CHECK(data.launch.blockers.join('|').contains("application database could not be read"));
        CHECK(data.launch.warnings.filter("No playset").isEmpty());
    }
    SECTION("the active playset itself cannot be read: -1 is not \"none\"") {
        damage("ALTER TABLE playsets RENAME TO playsets_unreadable");
        const GameContextData data = queries::gameContext(*runtime, game, std::nullopt, normal);
        CHECK(data.list.activeId == -1);
        CHECK_FALSE(data.launch.canPlay);
        REQUIRE(data.launch.blockers.size() == 1);
        CHECK(data.launch.blockers.front().startsWith("The active playset could not be read: "));
        CHECK(data.launch.warnings.isEmpty());
        CHECK_FALSE(queries::launchPlayset(*runtime, game, -1));
    }
}

TEST_CASE("FileLogSink writes daily files and keeps a bounded number", "[app][runtime]") {
    TempDir temp;
    FileLogSink sink(temp.path(), "pmm-test", 3, 200);
    LogRecord record;
    record.component = "test";
    record.message = std::string(60, 'x');
    record.timestampMs = 1'800'000'000'000; // 2027-01-15
    for (int i = 0; i < 10; ++i)
        sink.write(record);
    CHECK(sink.currentFile().filename().string().find("pmm-test-2027-01-15") == 0);
    CHECK(countLogs(temp.path()) <= 3);
    record.timestampMs += 24ll * 60 * 60 * 1000;
    sink.write(record);
    CHECK(sink.currentFile().filename().string() == "pmm-test-2027-01-16.log");
    CHECK(countLogs(temp.path()) <= 3);
}

TEST_CASE("ProfileLock admits one instance per profile and takes over stale locks",
          "[app][runtime]") {
    TempDir temp;
    const fs::path profile = temp.path() / u8("profile Ünïcødé");
    using std::chrono::milliseconds;
    {
        auto first = require(ProfileLock::acquire(profile, milliseconds(0)));
        CHECK(fs::is_regular_file(profile / ProfileLock::kFileName));
        auto second = ProfileLock::acquire(profile, milliseconds(100));
        REQUIRE_FALSE(second.ok());
        CHECK(second.error().code == ErrorCode::AlreadyExists);
        CHECK(second.error().message.find("already running") != std::string::npos);
        CHECK(second.error().message.find(platform::pathToUtf8(profile)) != std::string::npos);
        // Another profile is independent.
        CHECK(ProfileLock::acquire(temp.path() / "other", milliseconds(0)).ok());
    }
    CHECK_FALSE(fs::exists(profile / ProfileLock::kFileName));
    CHECK(ProfileLock::acquire(profile, milliseconds(0)).ok()); // released

    // A lock file left by a process that no longer runs (Qt's own format, with
    // a process id that cannot exist).
    QByteArray contents;
    {
        QLockFile probe(qs(platform::pathToUtf8(temp.path() / "probe.lock")));
        REQUIRE(probe.tryLock(0));
        QFile file(probe.fileName());
        REQUIRE(file.open(QIODevice::ReadOnly));
        contents = file.readAll();
    }
    const qsizetype firstLine = contents.indexOf('\n');
    REQUIRE(firstLine > 0);
    contents.replace(0, firstLine, QByteArray::number(2147483644));
    writeText(profile / ProfileLock::kFileName,
              std::string_view(contents.constData(), static_cast<std::size_t>(contents.size())));
    auto taken = ProfileLock::acquire(profile, milliseconds(0));
    REQUIRE(taken.ok());
    CHECK_FALSE(ProfileLock::acquire(profile, milliseconds(0)).ok());
}
