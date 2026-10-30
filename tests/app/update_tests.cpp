// UpdateClient and UpdateController against a local stub (update_stub.hpp): the
// check, redirects, limits, timeouts, cancelling and checksum verification.
// "Installing" starts pmm_platform_echo, a harmless test program, under the
// contract's file name. The real installer is never run, and only the hidden
// test at the end talks to GitHub.

#include "app/core/running_instance.hpp"
#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "app/gui/update_controller.hpp"
#include "platform/process.hpp"
#include "test_support.hpp"
#include "update_stub.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>
#include <windows.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::app;
using namespace pmm::app::test;

namespace {

using namespace std::chrono_literals;

const std::string kNew = "1.2.3";
const QByteArray kInstallerBytes = QByteArray("pretend installer ").repeated(200);
const std::vector<std::string> kContractArguments = {"/SILENT", "/SP-", "/NORESTART",
                                                     "/PMMUPDATE=1"};

// A controller on the stub with recording seams, as an installed copy.
struct Fixture {
    TempDir temp;
    ReleaseStub stub;
    UpdateControllerOptions options;
    QStringList states;
    int quits = 0;
    std::vector<platform::ProcessSpec> starts;
    std::vector<QUrl> opened;
    std::unique_ptr<UpdateController> controller;

    Fixture() {
        options.currentVersion = QStringLiteral("0.1.0");
        options.installed = true;
        options.client.endpoints = stub.endpoints();
        options.client.updatesDir = updatesDir();
        options.client.checkTimeout = 10s;
        options.client.stallTimeout = 10s;
        options.startProcess = [this](const platform::ProcessSpec& spec) {
            starts.push_back(spec);
            return Result<platform::StartedProcess>(platform::StartedProcess{4242});
        };
        options.requestQuit = [this] { ++quits; };
        options.openUrl = [this](const QUrl& url) { opened.push_back(url); };
    }

    QString updatesDir() const {
        return QDir(qpath(temp.path())).filePath(QStringLiteral("updates"));
    }

    UpdateController& make() {
        controller = std::make_unique<UpdateController>(options);
        QObject::connect(controller.get(), &UpdateController::stateChanged, controller.get(),
                         [this] { states.push_back(controller->state()); });
        return *controller;
    }

    bool waitForState(const QString& state, std::chrono::milliseconds timeout = 15s) const {
        return waitUntil([&] { return controller->state() == state; }, timeout);
    }

    // Checks and waits until the release is on offer.
    void reachAvailable() {
        if (!controller)
            make();
        controller->check();
        REQUIRE(waitForState(QStringLiteral("available")));
    }

    QStringList updateFiles() const {
        return QDir(updatesDir()).entryList(QDir::Files | QDir::Hidden);
    }
};

QString installerFile(const Fixture& f, const std::string& version = kNew) {
    return QDir(f.updatesDir()).filePath(qs(ReleaseStub::installerName(version)));
}

void settle(std::chrono::milliseconds time = 100ms) {
    waitUntil([] { return false; }, time);
}

} // namespace

TEST_CASE("Nothing touches the network until the user asks", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    UpdateController& controller = f.make();
    controller.setUpdatesDir(f.updatesDir());
    settle(200ms);
    CHECK(f.stub.connections() == 0);
    CHECK(controller.state() == "idle");
    CHECK(controller.canCheck());
    CHECK(controller.currentVersion() == "0.1.0");
    CHECK(controller.latestVersion().isEmpty());
    CHECK(controller.disabledReason().isEmpty());
}

TEST_CASE("A disabled session does nothing at all and says why", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    f.options.disabledReason = QStringLiteral("Updates are off in read-only mode.");
    QDir().mkpath(f.updatesDir());
    writeText(toPath(f.updatesDir()) / "left-behind.exe", "old");
    UpdateController& controller = f.make();
    controller.setUpdatesDir(f.updatesDir());
    CHECK(controller.disabledReason() == "Updates are off in read-only mode.");
    CHECK_FALSE(controller.canCheck());
    controller.check();
    controller.startUpdate();
    controller.openReleasePage();
    settle(200ms);
    CHECK(controller.state() == "idle");
    CHECK(f.stub.connections() == 0);
    CHECK(f.opened.empty());
    CHECK(f.updateFiles() ==
          QStringList{QStringLiteral("left-behind.exe")}); // read-only: untouched
}

TEST_CASE("A check finds a newer release", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    f.reachAvailable();
    UpdateController& controller = *f.controller;
    CHECK(f.states == QStringList{"checking", "available"});
    CHECK(controller.latestVersion() == "1.2.3");
    CHECK(controller.mode() == "installer");
    CHECK(controller.errorMessage().isEmpty());
    CHECK(controller.canCheck());

    REQUIRE(f.stub.requests().size() == 1);
    const QByteArray request = f.stub.requests()[0].toLower();
    CHECK(request.startsWith("get " + ReleaseStub::apiPath().toLower().toUtf8() + " http/1.1"));
    CHECK(request.contains("user-agent: paradoxmodmanager/0.1.0\r\n"));
    CHECK(request.contains("accept: application/vnd.github+json\r\n"));
    CHECK_FALSE(request.contains("authorization"));
    CHECK_FALSE(request.contains("cookie"));
    CHECK(f.updateFiles().isEmpty()); // a check downloads nothing
}

TEST_CASE("Only a strictly newer version is offered", "[app][update]") {
    const std::string version = GENERATE(as<std::string>{}, "0.1.0", "0.0.9");
    INFO(version);
    Fixture f;
    f.stub.serveRelease(version, kInstallerBytes);
    UpdateController& controller = f.make();
    controller.check();
    REQUIRE(f.waitForState(QStringLiteral("upToDate")));
    CHECK(controller.latestVersion() == qs(version));
    // Nothing to install, even if asked.
    controller.startUpdate();
    settle();
    CHECK(controller.state() == "upToDate");
    CHECK(f.stub.requests().size() == 1);
    CHECK(f.starts.empty());
    CHECK(f.quits == 0);
}

TEST_CASE("A 404 from the API means nothing was published yet", "[app][update]") {
    Fixture f;
    f.stub.routes[ReleaseStub::apiPath()].status = 404;
    f.stub.routes[ReleaseStub::apiPath()].body = R"({"message":"Not Found"})";
    UpdateController& controller = f.make();
    controller.check();
    REQUIRE(f.waitForState(QStringLiteral("noRelease")));
    CHECK(controller.errorMessage().isEmpty());
    CHECK(controller.latestVersion().isEmpty());
    CHECK(controller.canCheck());
    // The page of all releases, then.
    controller.openReleasePage();
    REQUIRE(f.opened.size() == 1);
    CHECK(f.opened[0] ==
          QUrl(QStringLiteral("https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases")));
}

TEST_CASE("Draft and prerelease replies are not offered", "[app][update]") {
    const char* flag = GENERATE("draft", "prerelease");
    Fixture f;
    auto json = f.stub.releaseJson(kNew);
    json[flag] = true;
    f.stub.routes[ReleaseStub::apiPath()].body = QByteArray::fromStdString(json.dump());
    UpdateController& controller = f.make();
    controller.check();
    REQUIRE(f.waitForState(QStringLiteral("noRelease")));
    CHECK(controller.latestVersion().isEmpty());
}

TEST_CASE("A failed check reports the problem and can be retried", "[app][update]") {
    Fixture f;
    auto& route = f.stub.routes[ReleaseStub::apiPath()];
    const QString expected =
        GENERATE(as<QString>{}, "not JSON", "wrong tag", "HTTP 500", "HTTP 403", "oversized",
                 "oversized without length", "no answer", "connection refused");
    INFO(expected.toStdString());
    if (expected == "no answer")
        f.options.client.checkTimeout = 400ms;
    if (expected == "not JSON") {
        route.body = "<html>nope</html>";
    } else if (expected == "wrong tag") {
        auto json = f.stub.releaseJson(kNew);
        json["tag_name"] = "v1.2.3-rc1";
        route.body = QByteArray::fromStdString(json.dump());
    } else if (expected == "HTTP 500") {
        route.status = 500;
    } else if (expected == "HTTP 403") {
        route.status = 403;
    } else if (expected == "oversized") {
        route.body = QByteArray(static_cast<qsizetype>(services::kMaxReleaseJsonBytes) + 1, ' ');
    } else if (expected == "oversized without length") {
        route.body = QByteArray(static_cast<qsizetype>(services::kMaxReleaseJsonBytes) + 1, ' ');
        route.mode = ReleaseStub::Mode::NoLength;
    } else if (expected == "no answer") {
        route.mode = ReleaseStub::Mode::NoAnswer;
    } else {
        // Nobody listens there: the stub's port is in use for nothing else.
        QTcpServer closed;
        REQUIRE(closed.listen(QHostAddress::LocalHost, 0));
        f.options.client.endpoints.port = closed.serverPort();
    }
    UpdateController& controller = f.make();
    controller.check();
    REQUIRE(f.waitForState(QStringLiteral("failed")));
    CHECK_FALSE(controller.errorMessage().isEmpty());
    CHECK(controller.canCheck());
    CHECK(f.starts.empty());

    // The retry checks again.
    f.stub.serveRelease(kNew, kInstallerBytes);
    f.stub.routes[ReleaseStub::apiPath()].mode = ReleaseStub::Mode::Complete;
    f.stub.routes[ReleaseStub::apiPath()].status = 200;
    if (expected != "connection refused" && expected != "no answer") {
        controller.retry();
        REQUIRE(f.waitForState(QStringLiteral("available")));
    }
}

TEST_CASE("Redirects are checked on every hop", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    const QString target = QStringLiteral("/forbidden");
    f.stub.routes[target].body = "should never be fetched";
    const QByteArray location =
        GENERATE(as<QByteArray>{}, "localhost", "evil", "scheme", "userinfo");
    QByteArray url;
    if (location == "localhost") // the same machine under a name that is not listed
        url = f.stub.absolute(target, QStringLiteral("localhost"));
    else if (location == "evil")
        url = f.stub.absolute(target, QStringLiteral("127.0.0.1.evil.example"));
    else if (location == "scheme")
        url = QStringLiteral("https://127.0.0.1:%1%2").arg(f.stub.port()).arg(target).toUtf8();
    else
        url = QStringLiteral("http://127.0.0.1@evil.example:%1%2")
                  .arg(f.stub.port())
                  .arg(target)
                  .toUtf8();
    INFO(url.toStdString());

    SECTION("from the API") {
        auto& route = f.stub.routes[ReleaseStub::apiPath()];
        route.status = 302;
        route.location = url;
        UpdateController& controller = f.make();
        controller.check();
        REQUIRE(f.waitForState(QStringLiteral("failed")));
        CHECK_THAT(controller.errorMessage().toStdString(),
                   Catch::Matchers::ContainsSubstring("does not trust"));
    }
    SECTION("from an asset, after allowed hops") {
        const QString first = QStringLiteral("/first");
        f.stub.routes[first].status = 301;
        f.stub.routes[first].location = url;
        auto& route = f.stub.routes[ReleaseStub::installerPath(kNew)];
        route.status = 302;
        route.body.clear();
        route.location = first.toUtf8(); // relative, resolved and allowed
        f.reachAvailable();
        f.controller->startUpdate();
        REQUIRE(f.waitForState(QStringLiteral("failed")));
        CHECK(f.stub.requestsFor(first) == 1);
        CHECK(f.starts.empty());
        CHECK(f.updateFiles().isEmpty());
    }
    CHECK(f.stub.requestsFor(target) == 0);
}

TEST_CASE("Redirect chains are bounded", "[app][update]") {
    Fixture f;
    auto& route = f.stub.routes[ReleaseStub::apiPath()];
    route.status = 302;
    route.location = ReleaseStub::apiPath().toUtf8();
    UpdateController& controller = f.make();
    controller.check();
    REQUIRE(f.waitForState(QStringLiteral("failed")));
    CHECK_THAT(controller.errorMessage().toStdString(),
               Catch::Matchers::ContainsSubstring("too many"));
    CHECK(f.stub.requestsFor(ReleaseStub::apiPath()) == services::kMaxUpdateRedirects + 1);
}

TEST_CASE("A verified download reaches ready with the right file", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    // As on GitHub: the asset URL redirects to the storage host.
    const QString storage = QStringLiteral("/storage/signed?sig=abc");
    f.stub.routes[storage].body = kInstallerBytes;
    auto& route = f.stub.routes[ReleaseStub::installerPath(kNew)];
    route.status = 302;
    route.body.clear();
    route.location = f.stub.absolute(storage);

    // The moment it is ready: the right file, under the contract's name.
    QByteArray seenAtStart;
    QString pathAtStart;
    f.options.startProcess = [&](const platform::ProcessSpec& spec) {
        f.starts.push_back(spec);
        pathAtStart = qpath(spec.executable);
        QFile file(pathAtStart);
        REQUIRE(file.open(QIODevice::ReadOnly));
        seenAtStart = file.readAll();
        return Result<platform::StartedProcess>(platform::StartedProcess{1});
    };
    f.reachAvailable();
    f.states.clear();
    f.controller->startUpdate();
    REQUIRE(f.waitForState(QStringLiteral("installing")));

    CHECK(f.states == QStringList{"downloading", "ready", "installing"});
    CHECK(f.controller->progress() == 1.0);
    CHECK(seenAtStart == kInstallerBytes);
    CHECK(QFileInfo(pathAtStart).fileName() == qs(ReleaseStub::installerName(kNew)));
    CHECK(QFileInfo(pathAtStart).absoluteFilePath() ==
          QFileInfo(installerFile(f)).absoluteFilePath());
    CHECK(f.updateFiles() == QStringList{qs(ReleaseStub::installerName(kNew))});
    REQUIRE(f.starts.size() == 1);
    CHECK(f.starts[0].arguments == kContractArguments);
    CHECK(f.quits == 1);
    CHECK_FALSE(f.controller->canCheck());
    CHECK(f.stub.requestsFor(ReleaseStub::checksumPath(kNew)) == 1);
    CHECK(f.stub.requestsFor(storage) == 1);
}

TEST_CASE("A checksum mismatch never starts anything and deletes the file", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    // One flipped byte in transit.
    QByteArray tampered = kInstallerBytes;
    tampered[10] = 'X';
    f.stub.routes[ReleaseStub::installerPath(kNew)].body = tampered;
    f.reachAvailable();
    f.controller->startUpdate();
    REQUIRE(f.waitForState(QStringLiteral("failed")));
    CHECK_THAT(f.controller->errorMessage().toStdString(),
               Catch::Matchers::ContainsSubstring("does not match its checksum"));
    CHECK(f.starts.empty());
    CHECK(f.quits == 0);
    CHECK(f.updateFiles().isEmpty());
    CHECK(f.stub.requestsFor(ReleaseStub::installerPath(kNew)) == 1);
}

TEST_CASE("A bad checksum file stops before the installer is fetched", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    auto& route = f.stub.routes[ReleaseStub::checksumPath(kNew)];
    const QString kind = GENERATE(as<QString>{}, "uppercase", "other file", "garbage", "oversized",
                                  "missing", "server error");
    INFO(kind.toStdString());
    if (kind == "uppercase")
        route.body = route.body.toUpper().replace(".EXE", ".exe");
    else if (kind == "other file")
        route.body = QByteArray(64, 'a') + "  other.exe\n";
    else if (kind == "garbage")
        route.body = "<html></html>";
    else if (kind == "oversized")
        route.body = QByteArray(static_cast<qsizetype>(services::kMaxChecksumFileBytes) + 1, 'a');
    else if (kind == "missing")
        f.stub.routes.erase(ReleaseStub::checksumPath(kNew));
    else
        route.status = 500;
    f.reachAvailable();
    f.controller->startUpdate();
    REQUIRE(f.waitForState(QStringLiteral("failed")));
    CHECK_FALSE(f.controller->errorMessage().isEmpty());
    CHECK(f.stub.requestsFor(ReleaseStub::installerPath(kNew)) == 0);
    CHECK(f.starts.empty());
    CHECK(f.updateFiles().isEmpty());
}

TEST_CASE("Cancelling stops the work and leaves nothing behind", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    SECTION("a check") {
        f.stub.routes[ReleaseStub::apiPath()].mode = ReleaseStub::Mode::NoAnswer;
        UpdateController& controller = f.make();
        controller.check();
        REQUIRE(waitUntil([&] { return !f.stub.requests().empty(); }));
        controller.cancel();
        CHECK(controller.state() == "idle");
        CHECK(controller.canCheck());
    }
    SECTION("a download") {
        f.stub.routes[ReleaseStub::installerPath(kNew)].mode = ReleaseStub::Mode::StallHalf;
        f.reachAvailable();
        f.controller->startUpdate();
        REQUIRE(f.waitForState(QStringLiteral("downloading")));
        REQUIRE(waitUntil([&] { return f.controller->progress() > 0.1; }));
        CHECK(f.controller->progress() < 0.6);
        f.controller->cancel();
        CHECK(f.controller->state() == "available");
        CHECK(f.controller->progress() == 0.0);
        settle();
        CHECK(f.updateFiles().isEmpty());
        CHECK(f.starts.empty());

        // The offer is still there.
        f.stub.routes[ReleaseStub::installerPath(kNew)].mode = ReleaseStub::Mode::Complete;
        f.controller->startUpdate();
        REQUIRE(f.waitForState(QStringLiteral("installing")));
        CHECK(f.starts.size() == 1);
    }
}

TEST_CASE("The size limit stops a download", "[app][update]") {
    Fixture f;
    f.options.client.maxInstallerBytes = 1024;
    f.stub.serveRelease(kNew, QByteArray(4096, 'x'));
    auto& route = f.stub.routes[ReleaseStub::installerPath(kNew)];
    SECTION("announced by Content-Length") {}
    SECTION("only seen while streaming") {
        route.mode = ReleaseStub::Mode::NoLength;
    }
    f.reachAvailable();
    f.controller->startUpdate();
    REQUIRE(f.waitForState(QStringLiteral("failed")));
    CHECK_THAT(f.controller->errorMessage().toStdString(),
               Catch::Matchers::ContainsSubstring("larger than"));
    CHECK(f.starts.empty());
    CHECK(f.updateFiles().isEmpty());
}

TEST_CASE("A stalled download times out", "[app][update]") {
    Fixture f;
    f.options.client.stallTimeout = 400ms;
    f.stub.serveRelease(kNew, kInstallerBytes);
    f.stub.routes[ReleaseStub::installerPath(kNew)].mode = ReleaseStub::Mode::StallHalf;
    f.reachAvailable();
    f.controller->startUpdate();
    REQUIRE(f.waitForState(QStringLiteral("failed")));
    CHECK_THAT(f.controller->errorMessage().toStdString(),
               Catch::Matchers::ContainsSubstring("stalled"));
    CHECK(f.starts.empty());
    CHECK(f.updateFiles().isEmpty());
}

TEST_CASE("Starting launches the program with the exact arguments, then quits", "[app][update]") {
    Fixture f;
    QFile echo(QStringLiteral(PMM_ECHO_EXE));
    REQUIRE(echo.open(QIODevice::ReadOnly));
    f.stub.serveRelease(kNew, echo.readAll());
    const fs::path report = f.temp.path() / "echo.json";
    // The real start; the test program reads where to report from its environment.
    f.options.startProcess = [&](const platform::ProcessSpec& spec) {
        CHECK(f.quits == 0);
        platform::ProcessSpec withReport = spec;
        withReport.environment.emplace_back("PMM_ECHO_OUTPUT", platform::pathToUtf8(report));
        return platform::startDetached(withReport);
    };
    f.reachAvailable();
    f.controller->startUpdate();
    REQUIRE(f.waitForState(QStringLiteral("installing")));
    CHECK(f.quits == 1);

    REQUIRE(waitUntil([&] { return fs::exists(report); }));
    const auto reported = nlohmann::json::parse(require(platform::readFile(report)));
    CHECK(reported.at("args").get<std::vector<std::string>>() == kContractArguments);
    // The program may still be running for a moment.
    CHECK(waitUntil([&] { return QFile::remove(installerFile(f)); }));
}

TEST_CASE("Quitting after a start takes the normal shutdown path", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    f.options.requestQuit = {}; // the real one
    f.reachAvailable();
    bool aboutToQuit = false;
    int startsAtQuit = -1;
    const QMetaObject::Connection connection = QObject::connect(
        QCoreApplication::instance(), &QCoreApplication::aboutToQuit, f.controller.get(), [&] {
            aboutToQuit = true;
            startsAtQuit = static_cast<int>(f.starts.size());
        });
    QTimer::singleShot(0, f.controller.get(), [&] { f.controller->startUpdate(); });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [] { QCoreApplication::exit(1); });
    watchdog.start(15s);
    const int code = QCoreApplication::exec();
    QObject::disconnect(connection);
    CHECK(code == 0);
    CHECK(aboutToQuit);
    CHECK(startsAtQuit == 1);
    CHECK(f.controller->state() == "installing");
}

TEST_CASE("A failed start keeps the application running and says so", "[app][update]") {
    Fixture f;
    // Not a program: platform::startDetached refuses it without trying.
    f.stub.serveRelease(kNew, "this is not a program");
    f.options.startProcess = {}; // the real one
    f.reachAvailable();
    f.controller->startUpdate();
    REQUIRE(f.waitForState(QStringLiteral("failed")));
    CHECK(f.quits == 0);
    CHECK_THAT(f.controller->errorMessage().toStdString(),
               Catch::Matchers::ContainsSubstring("could not be started"));
    CHECK(f.controller->canCheck());
    const int downloads = f.stub.requestsFor(ReleaseStub::installerPath(kNew));

    // "Try again" retries the start; the verified file is still there.
    f.controller->retry();
    REQUIRE(f.waitForState(QStringLiteral("failed")));
    settle();
    CHECK(f.quits == 0);
    CHECK(f.stub.requestsFor(ReleaseStub::installerPath(kNew)) == downloads);
}

TEST_CASE("A file changed after the check is not started", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    // Blocked once the download is done, so the file waits in its folder.
    bool blocked = false;
    f.options.installBlocker = [&](std::function<void(const QString&)> answer) {
        answer(blocked ? QStringLiteral("busy") : QString());
        blocked = true;
    };
    f.reachAvailable();
    f.controller->startUpdate();
    REQUIRE(f.waitForState(QStringLiteral("ready")));
    CHECK(f.starts.empty());
    CHECK(f.controller->errorMessage() == "busy");
    writeText(toPath(installerFile(f)), "replaced by something else");

    blocked = false;
    f.controller->startUpdate();
    REQUIRE(f.waitForState(QStringLiteral("failed")));
    CHECK(f.starts.empty());
    CHECK(f.quits == 0);
    CHECK(f.updateFiles().isEmpty());
}

TEST_CASE("Busy work holds the update back with the reason", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    QString reason = QStringLiteral("Workshop downloads are in progress.");
    f.options.installBlocker = [&](std::function<void(const QString&)> answer) { answer(reason); };
    f.reachAvailable();
    f.states.clear();

    f.controller->startUpdate();
    CHECK(f.controller->state() == "available");
    CHECK(f.controller->errorMessage() == reason);
    settle();
    CHECK(f.stub.requestsFor(ReleaseStub::checksumPath(kNew)) == 0);
    CHECK(f.starts.empty());

    // Once it has finished, the same button works and the reason is gone.
    reason.clear();
    f.controller->startUpdate();
    REQUIRE(f.waitForState(QStringLiteral("installing")));
    CHECK(f.controller->errorMessage().isEmpty());
    CHECK(f.starts.size() == 1);
}

TEST_CASE("An answer that comes too late changes nothing", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    std::function<void(const QString&)> pending;
    f.options.installBlocker = [&](std::function<void(const QString&)> answer) {
        pending = std::move(answer);
    };
    f.reachAvailable();
    f.controller->startUpdate();
    REQUIRE(pending);
    f.controller->startUpdate(); // already asked: not a second request
    // The user checks again meanwhile.
    f.stub.serveRelease("0.1.0", kInstallerBytes);
    f.controller->check();
    REQUIRE(f.waitForState(QStringLiteral("upToDate")));
    pending({});
    settle();
    CHECK(f.controller->state() == "upToDate");
    CHECK(f.stub.requestsFor(ReleaseStub::checksumPath(kNew)) == 0);
    CHECK(f.starts.empty());
}

TEST_CASE("Older downloads are cleaned up", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    const fs::path dir = toPath(f.updatesDir());
    writeText(dir / "ParadoxModManager-0.9.0-windows-x86_64-setup.exe", "old");
    writeText(dir / "ParadoxModManager-0.9.0-windows-x86_64-setup.exe.AbCdEf", "partial");
    SECTION("at start-up") {
        UpdateController& controller = f.make();
        controller.setUpdatesDir(f.updatesDir());
        CHECK(f.updateFiles().isEmpty());
    }
    SECTION("before a download") {
        f.reachAvailable();
        CHECK(f.updateFiles().size() == 2); // a check leaves them alone
        f.controller->startUpdate();
        REQUIRE(f.waitForState(QStringLiteral("installing")));
        CHECK(f.updateFiles() == QStringList{qs(ReleaseStub::installerName(kNew))});
    }
}

TEST_CASE("Other builds get the release page, built from the version", "[app][update]") {
    Fixture f;
    f.stub.serveRelease(kNew, kInstallerBytes);
    const QUrl page(QStringLiteral(
        "https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/tag/v1.2.3"));
    SECTION("not an installed copy") {
        f.options.installed = false;
        f.make();
        CHECK(f.controller->mode() == "releasePage");
        f.reachAvailable();
        CHECK(f.controller->mode() == "releasePage");
    }
    SECTION("a release without the installer") {
        auto json = f.stub.releaseJson(kNew);
        json["assets"].erase(0);
        f.stub.routes[ReleaseStub::apiPath()].body = QByteArray::fromStdString(json.dump());
        f.reachAvailable();
        CHECK(f.controller->mode() == "releasePage");
    }
    SECTION("a release without the checksum file") {
        auto json = f.stub.releaseJson(kNew);
        json["assets"].erase(1);
        f.stub.routes[ReleaseStub::apiPath()].body = QByteArray::fromStdString(json.dump());
        f.reachAvailable();
        CHECK(f.controller->mode() == "releasePage");
    }
    f.controller->startUpdate();
    settle();
    CHECK(f.controller->state() == "available");
    CHECK(f.stub.requestsFor(ReleaseStub::installerPath(kNew)) == 0);
    CHECK(f.starts.empty());
    CHECK(f.quits == 0);
    REQUIRE(f.opened.size() == 1);
    CHECK(f.opened[0] == page); // never the html_url of the reply
}

TEST_CASE("The mode follows the contract's layout", "[app][update]") {
    Fixture f;
    f.options.installed.reset();
    const fs::path appDir = f.temp.path() / "install" / "bin";
    fs::create_directories(appDir);
    f.options.appDir = qpath(appDir);
    SECTION("without the uninstaller") {
        CHECK(f.make().mode() == "releasePage");
    }
    SECTION("with the uninstaller") {
        writeText(appDir.parent_path() / "unins000.exe", "x");
        CHECK(f.make().mode() == "installer");
    }
}

TEST_CASE("A build without a release version has updates off", "[app][update]") {
    Fixture f;
    f.options.currentVersion = QStringLiteral("0.1.0-dev");
    UpdateController& controller = f.make();
    CHECK_FALSE(controller.disabledReason().isEmpty());
    CHECK_FALSE(controller.canCheck());
    controller.check();
    settle();
    CHECK(f.stub.connections() == 0);
}

TEST_CASE("The Backend wires updates to the session", "[app][update]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    RunnerOptions runnerOptions;
    runnerOptions.runtime = hermeticOptions(temp.path() / "profile", adapter);
    runnerOptions.steam = false;
    ServiceRunner runner(runnerOptions);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    ReleaseStub stub;
    stub.serveRelease(kNew, kInstallerBytes);

    int starts = 0;
    int quits = 0;
    BackendOptions options;
    options.updates.currentVersion = QStringLiteral("0.1.0");
    options.updates.installed = true;
    options.updates.client.endpoints = stub.endpoints();
    options.updates.startProcess = [&](const platform::ProcessSpec&) {
        ++starts;
        return Result<platform::StartedProcess>(platform::StartedProcess{1});
    };
    options.updates.requestQuit = [&] { ++quits; };

    SECTION("read-only sessions have it off") {
        options.readOnly = true;
        Backend backend(runner, options);
        backend.initialize(start);
        CHECK_FALSE(backend.updates()->canCheck());
        CHECK_THAT(backend.updates()->disabledReason().toStdString(),
                   Catch::Matchers::ContainsSubstring("read-only"));
        backend.updates()->check();
        settle();
        CHECK(stub.connections() == 0);
    }
    SECTION("downloads go to the profile's cache folder") {
        Backend backend(runner, options);
        backend.initialize(start);
        UpdateController* updates = backend.updates();
        settle();
        CHECK(stub.connections() == 0);
        updates->check();
        REQUIRE(waitUntil([&] { return updates->state() == "available"; }));

        // Held back while Workshop downloads of the current game are active...
        AcquisitionData active;
        active.active = true;
        backend.acquisition()->apply(active);
        updates->startUpdate();
        CHECK(updates->state() == "available");
        CHECK_THAT(updates->errorMessage().toStdString(),
                   Catch::Matchers::ContainsSubstring("Workshop downloads"));
        CHECK(starts == 0);

        // ... and goes ahead once they ended (the worker is asked about the rest).
        backend.acquisition()->apply({});
        updates->startUpdate();
        REQUIRE(waitUntil([&] { return updates->state() == "installing"; }));
        CHECK(starts == 1);
        CHECK(quits == 1);
        CHECK(fs::exists(temp.path() / "profile" / "cache" / "updates" /
                         u8(ReleaseStub::installerName(kNew))));
    }
    SECTION("work that starts while the worker is being asked holds it back too") {
        Backend backend(runner, options);
        backend.initialize(start);
        UpdateController* updates = backend.updates();
        settle();
        updates->check();
        REQUIRE(waitUntil([&] { return updates->state() == "available"; }));

        // The question is in the worker's queue; its answer has not arrived.
        updates->startUpdate();
        SECTION("a launch") {
            LaunchData ready;
            ready.canPlay = true;
            backend.launcher()->apply(ready);
            backend.launcher()->play(); // no active playset: the worker refuses it, no game
            REQUIRE(backend.launcher()->busy());
            REQUIRE(waitUntil([&] { return !updates->errorMessage().isEmpty(); }));
            CHECK_THAT(updates->errorMessage().toStdString(),
                       Catch::Matchers::ContainsSubstring("being launched"));
        }
        SECTION("Workshop downloads") {
            AcquisitionData active;
            active.active = true;
            backend.acquisition()->apply(active);
            REQUIRE(waitUntil([&] { return !updates->errorMessage().isEmpty(); }));
            CHECK_THAT(updates->errorMessage().toStdString(),
                       Catch::Matchers::ContainsSubstring("Workshop downloads"));
        }
        settle();
        CHECK(updates->state() == "available");
        CHECK(starts == 0);
        CHECK(quits == 0);
    }
    runner.shutdown();
}

TEST_CASE("The running-instance signal lives as long as its object", "[app][update]") {
    CHECK(QString::fromLatin1(kRunningInstanceMutexName) ==
          QStringLiteral("Local\\ParadoxModManagerRunning"));
    // A name of its own: a real pmm-app may be running while the tests do.
    const std::wstring name =
        L"Local\\ParadoxModManagerTest" + std::to_wstring(GetCurrentProcessId());
    const auto exists = [&] {
        HANDLE handle = OpenMutexW(SYNCHRONIZE, FALSE, name.c_str());
        if (!handle)
            return false;
        CloseHandle(handle);
        return true;
    };
    CHECK_FALSE(exists());
    {
        const RunningInstanceSignal first{QString::fromStdWString(name)};
        CHECK(first.held());
        CHECK(exists());
        // A second instance (another profile) is no error.
        const RunningInstanceSignal second{QString::fromStdWString(name)};
        CHECK(second.held());
    }
    CHECK_FALSE(exists());
}

// Hidden: run by hand ("pmm_app_tests [.live-update]"). One unauthenticated GET
// of the real API URL; CTest never runs it.
TEST_CASE("The real API answers through the client", "[.live-update]") {
    UpdateClientOptions options;
    options.userAgent = QStringLiteral("ParadoxModManager/") + QStringLiteral(PMM_APP_VERSION);
    UpdateClient client(options);
    std::optional<UpdateCheckResult> result;
    QObject::connect(&client, &UpdateClient::checkFinished,
                     [&](const UpdateCheckResult& finished) { result = finished; });
    client.check();
    REQUIRE(waitUntil([&] { return result.has_value(); }, 30s));
    if (!result->ok())
        FAIL("error: " << result->error().message << " | " << result->error().detail);
    if (result->value())
        WARN("latest release: " << result->value()->version.toString());
    else
        WARN("no release has been published yet (404)");
}
