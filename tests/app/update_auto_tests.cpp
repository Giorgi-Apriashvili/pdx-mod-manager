// The daily update check (docs/updates.md) against the local stub
// (update_stub.hpp), on an injected calendar: once on the first start of a day
// and again on the next day; a network failure records nothing and the next
// start retries; any HTTP answer records the date; the setting, read-only and
// verification sessions keep it from asking at all; a newer version waits for
// the click. Sessions the installer's relaunch would not continue (--profile,
// --no-steam) offer the release page with the reason.

#include "app/core/service_runner.hpp"
#include "app/gui/app_settings.hpp"
#include "app/gui/backend.hpp"
#include "app/gui/update_controller.hpp"
#include "core/log.hpp"
#include "persistence/settings_repository.hpp"
#include "platform/process.hpp"
#include "test_support.hpp"
#include "update_stub.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QDate>
#include <QDir>
#include <QTcpServer>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::app;
using namespace pmm::app::test;

namespace {

using namespace std::chrono_literals;

const QByteArray kInstaller = QByteArray("pretend installer ").repeated(64);
const QString kSessionReason =
    QStringLiteral("Restart Paradox Mod Manager without --profile or --no-steam to update.");

void settle(std::chrono::milliseconds time = 300ms) {
    waitUntil([] { return false; }, time);
}

// Starts of the application on one computer. Every launch() is a new controller
// on settings loaded from what the previous start left in the settings table.
struct Days {
    TempDir temp;
    ReleaseStub stub;
    QDate date{2026, 11, 6};
    std::map<std::string, std::string> table;
    UpdateControllerOptions options;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<UpdateController> controller;
    std::vector<platform::ProcessSpec> starts;
    std::vector<QUrl> opened;

    Days() {
        options.currentVersion = QStringLiteral("0.1.0");
        options.installed = true;
        options.client.endpoints = stub.endpoints();
        options.client.checkTimeout = 10s;
        options.dailyCheck = true;
        options.dailyCheckDelay = 20ms;
        options.today = [this] { return date; };
        options.startProcess = [this](const platform::ProcessSpec& spec) {
            starts.push_back(spec);
            return Result<platform::StartedProcess>(platform::StartedProcess{4242});
        };
        options.requestQuit = [] {};
        options.openUrl = [this](const QUrl& url) { opened.push_back(url); };
    }

    QString updatesDir() const {
        return QDir(qpath(temp.path())).filePath(QStringLiteral("updates"));
    }

    // The start-up refresh has finished: the daily check is due 20 ms later.
    UpdateController& launch() {
        if (settings)
            table = settings->values();
        controller.reset();
        settings = std::make_unique<AppSettings>(nullptr);
        settings->load(table);
        controller = std::make_unique<UpdateController>(options);
        controller->setSettings(settings.get());
        controller->setUpdatesDir(updatesDir());
        controller->scheduleDailyCheck();
        return *controller;
    }

    int checks() const { return stub.requestsFor(ReleaseStub::apiPath()); }
    bool waitForChecks(int count) const {
        return waitUntil([&] { return checks() >= count; }, 10s);
    }
    bool waitForState(const char* state) const {
        return waitUntil([&] { return controller->state() == QLatin1String(state); }, 15s);
    }
    QStringList updateFiles() const {
        return QDir(updatesDir()).entryList(QDir::Files | QDir::Hidden);
    }
};

class WarningLog {
public:
    WarningLog() {
        setLogSink([this](const LogRecord& record) {
            if (record.component == "app.update" && record.level == LogLevel::Warning)
                ++m_count;
        });
    }
    ~WarningLog() { setLogSink({}); }
    WarningLog(const WarningLog&) = delete;
    WarningLog& operator=(const WarningLog&) = delete;
    int count() const { return m_count; }

private:
    std::atomic<int> m_count{0};
};

// The value stored under `key` in the profile's settings table.
std::string storedSetting(ServiceRunner& runner, const char* key) {
    return runner.callBlocking([key](AppRuntime& runtime) {
        persistence::SettingsRepository settings(runtime.database());
        auto value = settings.get(key);
        return value && value.value() ? *value.value() : std::string();
    });
}

} // namespace

TEST_CASE("The daily check asks once on the first start of a day", "[app][update][daily]") {
    Days days;
    days.stub.serveRelease("0.1.0", kInstaller);

    UpdateController& first = days.launch();
    REQUIRE(days.waitForChecks(1));
    REQUIRE(days.waitForState("upToDate"));
    CHECK(first.checkedAutomatically());
    CHECK(days.settings->lastUpdateCheck() == days.date);
    // The same request as the button: one GET of the API, nothing else.
    REQUIRE(days.stub.requests().size() == 1);
    CHECK(days.stub.requests()[0].toLower().contains("user-agent: paradoxmodmanager/0.1.0\r\n"));

    SECTION("a second start on the same day does not ask") {
        UpdateController& second = days.launch();
        settle();
        CHECK(days.checks() == 1);
        CHECK(days.stub.connections() == 1);
        CHECK(second.state() == "idle");
        CHECK_FALSE(second.checkedAutomatically());
        // The button still works.
        second.check();
        REQUIRE(days.waitForState("upToDate"));
        CHECK(days.checks() == 2);
    }
    SECTION("the next day asks again") {
        days.date = days.date.addDays(1);
        days.launch();
        REQUIRE(days.waitForChecks(2));
        REQUIRE(days.waitForState("upToDate"));
        CHECK(days.settings->lastUpdateCheck() == QDate(2026, 11, 7));
        days.launch();
        settle();
        CHECK(days.checks() == 2);
    }
    SECTION("a clock set back to an earlier day asks again") {
        days.date = days.date.addDays(-3);
        days.launch();
        REQUIRE(days.waitForChecks(2));
    }
}

TEST_CASE("A daily check without an answer records nothing and is retried",
          "[app][update][daily]") {
    Days days;
    const QString failure = GENERATE(as<QString>{}, "connection refused", "no answer");
    INFO(failure.toStdString());
    QTcpServer closed;
    if (failure == "connection refused") {
        // Nobody listens there.
        REQUIRE(closed.listen(QHostAddress::LocalHost, 0));
        days.options.client.endpoints.port = closed.serverPort();
        closed.close();
    } else {
        days.options.client.checkTimeout = 300ms;
        days.stub.routes[ReleaseStub::apiPath()].mode = ReleaseStub::Mode::NoAnswer;
    }
    UpdateController* controller = nullptr;
    {
        const WarningLog warnings;
        controller = &days.launch();
        REQUIRE(waitUntil([&] { return warnings.count() > 0; }, 10s)); // logged
        settle(100ms);
    }
    // Silent: nothing on screen, nothing recorded.
    CHECK(controller->state() == "idle");
    CHECK(controller->errorMessage().isEmpty());
    CHECK(controller->canCheck());
    CHECK(days.settings->lastUpdateCheck().isNull());

    // The next start on the same day tries again, and the answer counts.
    days.options.client.endpoints = days.stub.endpoints();
    days.options.client.checkTimeout = 10s;
    days.stub.serveRelease("0.1.0", kInstaller);
    days.stub.routes[ReleaseStub::apiPath()].mode = ReleaseStub::Mode::Complete;
    const int before = days.checks();
    days.launch();
    REQUIRE(days.waitForChecks(before + 1));
    REQUIRE(days.waitForState("upToDate"));
    CHECK(days.settings->lastUpdateCheck() == days.date);
}

TEST_CASE("Any HTTP answer to the daily check records the date", "[app][update][daily]") {
    Days days;
    auto& route = days.stub.routes[ReleaseStub::apiPath()];
    const QString answer = GENERATE(as<QString>{}, "newer", "same", "older", "none", "HTTP 500",
                                    "HTTP 403", "not JSON");
    INFO(answer.toStdString());
    QString expected = QStringLiteral("idle"); // errors are not shown
    if (answer == "newer") {
        days.stub.serveRelease("1.2.3", kInstaller);
        expected = QStringLiteral("available");
    } else if (answer == "same" || answer == "older") {
        days.stub.serveRelease(answer == "same" ? "0.1.0" : "0.0.9", kInstaller);
        expected = QStringLiteral("upToDate");
    } else if (answer == "none") {
        route.status = 404;
        route.body = R"({"message":"Not Found"})";
        expected = QStringLiteral("noRelease");
    } else if (answer == "HTTP 500") {
        route.status = 500;
    } else if (answer == "HTTP 403") {
        route.status = 403;
    } else {
        route.body = "<html>nope</html>";
    }
    UpdateController& controller = days.launch();
    REQUIRE(days.waitForChecks(1));
    REQUIRE(waitUntil([&] { return days.settings->lastUpdateCheck() == days.date; }, 10s));
    CHECK(controller.state() == expected);
    CHECK(controller.errorMessage().isEmpty());

    days.launch();
    settle();
    CHECK(days.checks() == 1);
}

TEST_CASE("With the setting off the daily check never connects", "[app][update][daily]") {
    Days days;
    days.stub.serveRelease("1.2.3", kInstaller);
    days.launch();
    days.settings->setCheckUpdatesAutomatically(false);
    settle();
    CHECK(days.stub.connections() == 0);
    CHECK(days.settings->lastUpdateCheck().isNull());

    // Off stays off on the next starts and days; the button still asks.
    days.date = days.date.addDays(1);
    UpdateController& next = days.launch();
    CHECK_FALSE(days.settings->checkUpdatesAutomatically());
    settle();
    CHECK(days.stub.connections() == 0);
    CHECK(next.state() == "idle");
    next.check();
    REQUIRE(days.waitForState("available"));
    CHECK_FALSE(next.checkedAutomatically());
    CHECK(days.settings->lastUpdateCheck().isNull()); // only the daily check counts
}

TEST_CASE("Sessions with updates off never run the daily check", "[app][update][daily]") {
    Days days;
    days.stub.serveRelease("1.2.3", kInstaller);
    const QString session =
        GENERATE(as<QString>{}, "verification", "development version", "not turned on");
    INFO(session.toStdString());
    if (session == "verification")
        days.options.disabledReason = QStringLiteral("Updates are off during verification runs.");
    else if (session == "development version")
        days.options.currentVersion = QStringLiteral("0.1.0-dev");
    else
        days.options.dailyCheck = false; // tests and anything but main()
    days.launch();
    settle();
    CHECK(days.stub.connections() == 0);
    CHECK(days.settings->lastUpdateCheck().isNull());
}

TEST_CASE("A check the user started first stands in for the daily one", "[app][update][daily]") {
    Days days;
    days.stub.serveRelease("1.2.3", kInstaller);
    days.options.dailyCheckDelay = 200ms;
    UpdateController& controller = days.launch();
    controller.check();
    REQUIRE(days.waitForState("available"));
    settle(500ms);
    CHECK(days.checks() == 1);
    CHECK_FALSE(controller.checkedAutomatically());
}

TEST_CASE("A newer version from the daily check waits for the click", "[app][update][daily]") {
    Days days;
    days.stub.serveRelease("1.2.3", kInstaller);
    UpdateController& controller = days.launch();
    REQUIRE(days.waitForState("available"));
    CHECK(controller.checkedAutomatically());
    CHECK(controller.latestVersion() == "1.2.3");
    CHECK(controller.mode() == "installer");
    CHECK(controller.installUnavailableReason().isEmpty());
    settle(500ms);
    // Only the API was asked: no checksum file, no installer, nothing started.
    CHECK(days.stub.requests().size() == 1);
    CHECK(days.stub.requestsFor(ReleaseStub::checksumPath("1.2.3")) == 0);
    CHECK(days.stub.requestsFor(ReleaseStub::installerPath("1.2.3")) == 0);
    CHECK(days.updateFiles().isEmpty());
    CHECK(days.starts.empty());
    CHECK(days.opened.empty());
    CHECK(controller.state() == "available");

    controller.startUpdate();
    REQUIRE(days.waitForState("installing"));
    CHECK(days.starts.size() == 1);
    CHECK(controller.checkedAutomatically()); // the banner follows the update it offered
}

TEST_CASE("Sessions the installer would not continue offer the release page",
          "[app][update][session]") {
    CHECK(UpdateController::sessionInstallBlocker(false, false).isEmpty());
    CHECK(UpdateController::sessionInstallBlocker(true, false) == kSessionReason);
    CHECK(UpdateController::sessionInstallBlocker(false, true) == kSessionReason);
    CHECK(UpdateController::sessionInstallBlocker(true, true) == kSessionReason);

    Days days;
    days.stub.serveRelease("1.2.3", kInstaller);
    days.options.installUnavailableReason = kSessionReason;
    SECTION("an installed copy says why") {
        UpdateController& controller = days.launch();
        CHECK(controller.mode() == "releasePage");
        CHECK(controller.installUnavailableReason() == kSessionReason);
        CHECK(controller.canCheck());
        REQUIRE(days.waitForState("available")); // the daily check still runs
        CHECK(controller.mode() == "releasePage");
        CHECK(controller.installUnavailableReason() == kSessionReason);

        controller.startUpdate();
        settle();
        CHECK(controller.state() == "available");
        REQUIRE(days.opened.size() == 1);
        CHECK(days.opened[0].scheme() == "https");
        CHECK(days.opened[0].host() == "github.com");
        CHECK(days.opened[0].path().endsWith("/releases/tag/v1.2.3"));
        CHECK(days.stub.requestsFor(ReleaseStub::checksumPath("1.2.3")) == 0);
        CHECK(days.stub.requestsFor(ReleaseStub::installerPath("1.2.3")) == 0);
        CHECK(days.starts.empty());
        controller.retry(); // not failed: nothing
        controller.openReleasePage();
        CHECK(days.opened.size() == 2);
        CHECK(days.starts.empty());
    }
    SECTION("a copy that cannot install anyway has nothing to explain") {
        days.options.installed = false;
        UpdateController& controller = days.launch();
        REQUIRE(days.waitForState("available"));
        CHECK(controller.mode() == "releasePage");
        CHECK(controller.installUnavailableReason().isEmpty());
    }
    SECTION("a release without the installer has nothing to explain") {
        auto json = days.stub.releaseJson("1.2.3");
        json["assets"].erase(0);
        days.stub.routes[ReleaseStub::apiPath()].body = QByteArray::fromStdString(json.dump());
        UpdateController& controller = days.launch();
        REQUIRE(days.waitForState("available"));
        CHECK(controller.installUnavailableReason().isEmpty());
    }
}

namespace {

// Starts of the real Backend on one hermetic profile, with the daily check on.
struct BackendDays {
    TempDir temp;
    FakeWorld world{temp.path() / "world"};
    FakeAdapter adapter{world};
    ReleaseStub stub;
    QDate date{2026, 11, 6};
    int starts = 0;
    BackendOptions options;
    std::unique_ptr<ServiceRunner> runner;
    std::unique_ptr<Backend> backend;

    BackendDays() {
        options.updates.currentVersion = QStringLiteral("0.1.0");
        options.updates.installed = true;
        options.updates.client.endpoints = stub.endpoints();
        options.updates.dailyCheck = true;
        options.updates.dailyCheckDelay = 300ms;
        options.updates.today = [this] { return date; };
        options.updates.startProcess = [this](const platform::ProcessSpec&) {
            ++starts;
            return Result<platform::StartedProcess>(platform::StartedProcess{1});
        };
        options.updates.requestQuit = [] {};
        options.updates.openUrl = [](const QUrl&) {};
    }

    void start() {
        RunnerOptions runnerOptions;
        runnerOptions.runtime = hermeticOptions(temp.path() / "profile", adapter);
        runnerOptions.steam = false;
        runner = std::make_unique<ServiceRunner>(runnerOptions);
        const RunnerStart started = runner->start();
        REQUIRE(started.ok);
        backend = std::make_unique<Backend>(*runner, options);
        backend->initialize(started);
    }

    // Ends the process without Backend::shutdown(), as a crash or a kill would:
    // what is in the table was written when it happened.
    void kill() {
        runner->callBlocking([](AppRuntime&) { return 0; }); // the writes queued so far
        runner->shutdown();
        backend.reset();
        runner.reset();
    }

    int checks() const { return stub.requestsFor(ReleaseStub::apiPath()); }
};

} // namespace

TEST_CASE("The Backend runs the daily check after the start-up refresh", "[app][update][daily]") {
    BackendDays days;
    days.stub.serveRelease("1.2.3", kInstaller);
    days.start();
    REQUIRE(waitUntil([&] { return days.backend->initialRefreshDone(); }));
    CHECK(days.stub.connections() == 0); // not before the refresh, and not at once
    REQUIRE(waitUntil([&] { return days.checks() == 1; }, 10s));
    UpdateController* updates = days.backend->updates();
    REQUIRE(waitUntil([&] { return updates->state() == "available"; }));
    CHECK(updates->checkedAutomatically());
    // Stored at once: a start that ends without saving keeps it.
    CHECK(waitUntil([&] {
        return storedSetting(*days.runner, "app.updates.lastAutomaticCheck") == "2026-11-06";
    }));
    days.kill();

    days.start();
    REQUIRE(waitUntil([&] { return days.backend->initialRefreshDone(); }));
    CHECK(days.backend->settings()->lastUpdateCheck() == days.date);
    CHECK(days.backend->settings()->checkUpdatesAutomatically());
    settle(800ms);
    CHECK(days.checks() == 1);

    // Turned off: stored at once as well, and the next day asks nothing.
    days.backend->settings()->setCheckUpdatesAutomatically(false);
    CHECK(storedSetting(*days.runner, "app.updates.checkAutomatically") == "false");
    days.kill();
    days.date = days.date.addDays(1);
    days.start();
    REQUIRE(waitUntil([&] { return days.backend->initialRefreshDone(); }));
    CHECK_FALSE(days.backend->settings()->checkUpdatesAutomatically());
    settle(800ms);
    CHECK(days.checks() == 1);
    CHECK(days.starts == 0);
    days.kill();
}

TEST_CASE("Read-only and verification sessions never run the daily check", "[app][update][daily]") {
    BackendDays days;
    days.stub.serveRelease("1.2.3", kInstaller);
    const bool readOnly = GENERATE(true, false);
    INFO((readOnly ? "read-only" : "verification"));
    if (readOnly)
        days.options.readOnly = true;
    else // what main() sets for every verification switch
        days.options.updates.disabledReason =
            QStringLiteral("Updates are off during verification runs.");
    days.options.updates.dailyCheckDelay = 20ms;
    days.start();
    REQUIRE(waitUntil([&] { return days.backend->initialRefreshDone(); }));
    settle(500ms);
    CHECK(days.stub.connections() == 0);
    CHECK_FALSE(days.backend->updates()->canCheck());
    CHECK(days.backend->settings()->lastUpdateCheck().isNull());
    days.kill();
}
