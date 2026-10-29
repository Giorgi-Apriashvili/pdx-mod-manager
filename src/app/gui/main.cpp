// pmm-app: the desktop application. Composition (docs/app.md):
//   ServiceRunner (worker thread: AppRuntime, Steam helper and Web API hosts,
//   acquisition coordinator) <- Backend (models and controllers, GUI thread)
//   <- QML module Pmm.Ui (ui/), loaded with the Backend registered as the
//   singleton Pmm.Backend.

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "app/gui/verification.hpp"
#include "core/log.hpp"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlEngineExtensionPlugin>
#include <QQuickStyle>
#include <QSysInfo>

#include <cstdio>
#include <cstring>
#include <memory>

#if defined(_WIN32)
#include <windows.h>
#endif

Q_IMPORT_QML_PLUGIN(Pmm_UiPlugin)

namespace {

using namespace pmm;
using namespace pmm::app;

struct CommandLine {
    std::optional<std::filesystem::path> profile;
    bool verbose = false;
    bool noSteam = false;
    bool readOnly = false;
    QString game;
    VerificationOptions verification;
};

void forwardQtMessage(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const LogLevel level = type == QtDebugMsg || type == QtInfoMsg ? LogLevel::Debug
                           : type == QtWarningMsg                  ? LogLevel::Warning
                                                                   : LogLevel::Error;
    const std::string category = context.category ? context.category : "default";
    log(level, "qt", utf8(message), {{"category", category}});
}

CommandLine parseCommandLine(const QGuiApplication& app) {
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Compact mod manager for Paradox games: playsets, share codes, Workshop "
                       "downloads and direct launch."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption profile(
        QStringLiteral("profile"),
        QStringLiteral("Keep every application file (database, logs, cache) in <dir> instead of "
                       "the standard per-user locations; useful for a clean, isolated profile."),
        QStringLiteral("dir"));
    const QCommandLineOption verbose(QStringLiteral("verbose"),
                                     QStringLiteral("Write debug-level records to the log."));
    const QCommandLineOption noSteam(
        QStringLiteral("no-steam"),
        QStringLiteral(
            "Never start the Steam helper: Workshop downloads are off for this session."));
    const QCommandLineOption readOnly(
        QStringLiteral("read-only"),
        QStringLiteral("Never write to game folders or launch a game (implies --no-steam)."));
    const QCommandLineOption game(QStringLiteral("game"),
                                  QStringLiteral("Game to select at start-up."),
                                  QStringLiteral("key"));
    // Hidden verification switches (docs/app.md).
    QCommandLineOption screenshot(QStringLiteral("screenshot"), QString(), QStringLiteral("png"));
    QCommandLineOption delay(QStringLiteral("screenshot-delay-ms"), QString(),
                             QStringLiteral("ms"));
    QCommandLineOption theme(QStringLiteral("theme"), QString(), QStringLiteral("theme"));
    QCommandLineOption open(QStringLiteral("open"), QString(), QStringLiteral("what"));
    QCommandLineOption selectRow(QStringLiteral("select-row"), QString(), QStringLiteral("row"));
    QCommandLineOption filter(QStringLiteral("filter"), QString(), QStringLiteral("status"));
    QCommandLineOption search(QStringLiteral("search"), QString(), QStringLiteral("text"));
    QCommandLineOption selfTest(QStringLiteral("self-test"), QString(), QStringLiteral("steps"));
    for (QCommandLineOption* hidden :
         {&screenshot, &delay, &theme, &open, &selectRow, &filter, &search, &selfTest})
        hidden->setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOptions({profile, verbose, noSteam, readOnly, game, screenshot, delay, theme, open,
                       selectRow, filter, search, selfTest});
    parser.process(app);

    CommandLine line;
    if (parser.isSet(profile))
        line.profile =
            toPath(QDir::cleanPath(QDir::current().absoluteFilePath(parser.value(profile))));
    line.verbose = parser.isSet(verbose);
    line.noSteam = parser.isSet(noSteam);
    line.readOnly = parser.isSet(readOnly);
    line.game = parser.value(game);
    VerificationOptions& v = line.verification;
    v.screenshot = parser.value(screenshot);
    if (parser.isSet(delay))
        v.screenshotDelayMs = parser.value(delay).toInt();
    v.theme = parser.value(theme);
    v.open = parser.values(open);
    if (parser.isSet(selectRow))
        v.selectRow = parser.value(selectRow).toInt();
    v.statusFilter = parser.value(filter);
    v.search = parser.value(search);
    v.selfTest = parser.value(selfTest);
    if (v.active())
        line.readOnly = true; // verification never touches game folders or Steam
    if (line.readOnly)
        line.noSteam = true;
    return line;
}

bool argumentGiven(int argc, char* argv[], const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0)
            return true;
    }
    return false;
}

// Minimal window for errors that prevent start-up (no QtWidgets needed).
void showFatalError(QQmlApplicationEngine& engine, const QString& message) {
    engine.setInitialProperties({{QStringLiteral("message"), message}});
    engine.loadData(QByteArrayLiteral(R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
ApplicationWindow {
    property string message
    visible: true
    width: 560
    height: 240
    title: "Paradox Mod Manager"
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        Label {
            text: "Paradox Mod Manager cannot start"
            font.pixelSize: 16
            font.bold: true
        }
        Label {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: message
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        Button {
            Layout.alignment: Qt.AlignRight
            text: "Quit"
            onClicked: Qt.quit()
        }
    }
}
)"));
}

} // namespace

int main(int argc, char* argv[]) {
#if defined(_WIN32)
    // No modal system error boxes for this process or the processes it starts.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    if (argumentGiven(argc, argv, "--screenshot")) {
        // Reproducible captures: exactly the window's logical size.
        qputenv("QT_SCALE_FACTOR", "1");
        qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
    }
    QGuiApplication::setOrganizationName(QStringLiteral("ParadoxModManager"));
    QGuiApplication::setApplicationName(QStringLiteral("Paradox Mod Manager"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PMM_APP_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("paradox-mod-manager"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QGuiApplication app(argc, argv);

    const CommandLine line = parseCommandLine(app);

    RunnerOptions runnerOptions;
    runnerOptions.runtime.profileDir = line.profile;
    runnerOptions.runtime.verbose = line.verbose;
    runnerOptions.steam = !line.noSteam;
    ServiceRunner runner(runnerOptions);
    const RunnerStart start = runner.start();
    qInstallMessageHandler(forwardQtMessage);
    logInfo("app", "Paradox Mod Manager started",
            {{"version", PMM_APP_VERSION},
             {"qt", qVersion()},
             {"os", utf8(QSysInfo::prettyProductName())},
             {"steam", line.noSteam ? "off" : "on"},
             {"readOnly", line.readOnly ? "true" : "false"}});

    std::unique_ptr<Backend> backend;
    QQmlApplicationEngine engine;
    if (!start.ok) {
        logError("app", "start-up failed", {{"error", utf8(start.errorMessage)}});
        if (line.verification.active()) {
            std::fprintf(stderr, "pmm-app: %s\n", start.errorMessage.toUtf8().constData());
            return 2;
        }
        showFatalError(engine, start.errorMessage);
        return app.exec();
    }

    Backend::registerQmlTypes();
    BackendOptions backendOptions;
    backendOptions.readOnly = line.readOnly;
    backendOptions.initialGame = line.game;
    backend = std::make_unique<Backend>(runner, backendOptions);
    backend->initialize(start);
    qmlRegisterSingletonInstance("Pmm.Backend", 1, 0, "Backend", backend.get());
    engine.loadFromModule("Pmm.Ui", "Main");
    if (engine.rootObjects().isEmpty()) {
        logError("app", "the user interface could not be loaded");
        if (line.verification.active())
            return 3;
        showFatalError(engine, QStringLiteral("The user interface could not be loaded. See the log "
                                              "in %1.")
                                   .arg(start.logsDir));
        return app.exec();
    }

    std::unique_ptr<Verification> verification;
    if (line.verification.active()) {
        verification = std::make_unique<Verification>(engine, *backend, line.verification);
        verification->start();
    }
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
        if (!line.verification.active())
            backend->shutdown(); // settings; verification runs leave the profile as it was
        runner.shutdown();
        logInfo("app", "Paradox Mod Manager stopped");
    });
    const int code = app.exec();
    verification.reset();
    return code;
}
