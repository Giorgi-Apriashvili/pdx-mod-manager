// AppRuntime start-up: profile folders, the file log, database versions,
// retention and the startup notices; plus the rotating file log sink.

#include "app/core/app_runtime.hpp"
#include "app/core/file_log_sink.hpp"
#include "app/core/profile_lock.hpp"
#include "app/core/qt_util.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/sqlite.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QFile>
#include <QLockFile>

#include <chrono>
#include <fstream>
#include <string>

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
