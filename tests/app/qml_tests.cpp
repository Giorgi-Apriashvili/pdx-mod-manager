// The real QML module (Pmm.Ui) against the real Backend in a hermetic
// profile: the window and every dialog load and bind without QML warnings,
// the main flows work through the window's own functions, and the keyboard
// paths of docs/accessibility.md work with real key events. Names that look
// like rich text never make the window fetch anything, and the engine's
// network access refuses everything but local resources.

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "app/gui/network_guard.hpp"
#include "core/log.hpp"
#include "persistence/settings_repository.hpp"
#include "platform/file_transaction.hpp"
#include "test_support.hpp"
#include "update_stub.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QDate>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngineExtensionPlugin>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>
#include <QtQml/qqml.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

Q_IMPORT_QML_PLUGIN(Pmm_UiPlugin)

using namespace pmm;
using namespace pmm::app;
using namespace pmm::app::test;

namespace {

// Local HTTP endpoint: counts connections, records request paths and answers
// 404.
class Listener {
public:
    Listener() {
        REQUIRE(m_server.listen(QHostAddress::LocalHost));
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                ++m_connections;
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    if (!socket->canReadLine())
                        return;
                    m_paths.push_back(QString::fromLatin1(socket->readLine().split(' ').value(1)));
                    socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n"
                                  "Connection: close\r\n\r\n");
                    socket->disconnectFromHost();
                });
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QString url(const QString& file) const {
        return QStringLiteral("http://127.0.0.1:%1/%2").arg(m_server.serverPort()).arg(file);
    }
    int connections() const { return m_connections; }
    bool requested(const QString& file) const { return m_paths.contains(QLatin1Char('/') + file); }

private:
    QTcpServer m_server;
    int m_connections = 0;
    QStringList m_paths;
};

// Counts the requests the QML engine's network access refused.
class RefusalLog {
public:
    RefusalLog() {
        setLogSink([this](const LogRecord& record) {
            if (record.component == "app.qml" && record.message.find("refused") == 0)
                ++m_count;
        });
    }
    ~RefusalLog() { setLogSink({}); }
    int count() const { return m_count; }

private:
    std::atomic<int> m_count{0};
};

// QML warnings carry the source file of the binding.
class QmlWarnings {
public:
    QmlWarnings() {
        list().clear();
        m_previous = qInstallMessageHandler(&QmlWarnings::handler);
    }
    ~QmlWarnings() { qInstallMessageHandler(m_previous); }
    static QStringList& list() {
        static QStringList warnings;
        return warnings;
    }

private:
    static void handler(QtMsgType type, const QMessageLogContext&, const QString& text) {
        if (type != QtDebugMsg && type != QtInfoMsg && text.contains(QLatin1String(".qml")))
            list().push_back(text);
    }
    QtMessageHandler m_previous = nullptr;
};

// The singleton is registered once per process and resolves to the Backend of
// the running test, so several test cases can load the module.
Backend*& currentBackend() {
    static Backend* backend = nullptr;
    return backend;
}

// Clears the singleton's backend before the test's Backend goes away.
struct BackendRegistration {
    BackendRegistration() = default;
    BackendRegistration(const BackendRegistration&) = delete;
    BackendRegistration& operator=(const BackendRegistration&) = delete;
    ~BackendRegistration() { currentBackend() = nullptr; }
};

[[nodiscard]] std::unique_ptr<BackendRegistration> registerBackend(Backend& backend) {
    static const bool registered = [] {
        Backend::registerQmlTypes();
        qmlRegisterSingletonType<Backend>(
            "Pmm.Backend", 1, 0, "Backend", [](QQmlEngine*, QJSEngine*) -> QObject* {
                Backend* current = currentBackend();
                QJSEngine::setObjectOwnership(current, QJSEngine::CppOwnership);
                return current;
            });
        return true;
    }();
    (void)registered;
    currentBackend() = &backend;
    return std::make_unique<BackendRegistration>();
}

// The column ("gamesPane", "modsPane", "sidePane") holding the keyboard focus.
QString focusedPane(QQuickWindow* window) {
    for (QQuickItem* item = window->activeFocusItem(); item; item = item->parentItem()) {
        const QString name = item->objectName();
        if (name == QLatin1String("gamesPane") || name == QLatin1String("modsPane") ||
            name == QLatin1String("sidePane"))
            return name;
    }
    return {};
}

bool hasActiveFocus(QObject* object) {
    auto* item = qobject_cast<QQuickItem*>(object);
    return item && item->hasActiveFocus();
}

} // namespace

TEST_CASE("Pmm.Ui loads against the real backend without QML warnings", "[app][qml]") {
    qputenv("QT_QUICK_BACKEND", "software");
    // The playset name and a mod name look like rich text with a remote
    // image, as a received share code or a Workshop descriptor could.
    Listener listener;
    RefusalLog refusals;
    const QString playsetName = QStringLiteral("<img src=\"%1\">").arg(listener.url("playset.png"));
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("1001", "Alpha");
    world.addWorkshopMod("1002", "Beta");
    world.addLocalMod("gamma",
                      utf8(QStringLiteral("<img src=\"%1\">").arg(listener.url("mod.png"))));
    FakeAdapter adapter(world);
    RunnerOptions options;
    options.runtime = hermeticOptions(temp.path() / "profile", adapter);
    options.steam = false;
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);

    QmlWarnings warnings;
    {
        Backend backend(runner, {});
        backend.initialize(start);
        const auto registration = registerBackend(backend);
        OfflineNetworkAccessManagerFactory networkFactory; // as in main()
        QQmlApplicationEngine engine;
        engine.setNetworkAccessManagerFactory(&networkFactory);
        engine.loadFromModule("Pmm.Ui", "Main");
        REQUIRE(engine.rootObjects().size() == 1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        REQUIRE(window);
        REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
        QQmlExpression plainToolTips(
            qmlContext(window), window,
            QStringLiteral("split.ToolTip.toolTip.contentItem.textFormat === Text.PlainText"));
        CHECK(plainToolTips.evaluate().toBool());

        // Empty state, then a playset with mods.
        backend.playsets()->create(playsetName);
        REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
        CHECK(backend.playsets()->activeName() == playsetName);
        backend.mods()->addMods({"steam-workshop:1001", "steam-workshop:1002", "local:gamma"});
        REQUIRE(waitUntil([&] { return backend.mods()->count() == 3 && !backend.mods()->busy(); }));
        QMetaObject::invokeMethod(window, "selectMod", Q_ARG(int, 1));
        REQUIRE(waitUntil([&] { return backend.details()->row() == 1; }));
        CHECK(backend.details()->name() == "Beta");

        // Every dialog opens and binds.
        for (const char* open : {"openExport", "openAddMods", "openSettings", "openHistory",
                                 "openLocate", "openShortcuts"}) {
            INFO(open);
            REQUIRE(QMetaObject::invokeMethod(window, open));
            waitUntil([] { return false; }, std::chrono::milliseconds(150));
            QMetaObject::invokeMethod(window, "closeDialogs");
        }
        REQUIRE(waitUntil([&] { return !backend.sharing()->lastExportCode().isEmpty(); }));
        const QString code = backend.sharing()->lastExportCode();
        QMetaObject::invokeMethod(window, "openImport", Q_ARG(QString, code));
        REQUIRE(waitUntil(
            [&] { return backend.sharing()->hasPreview() && !backend.sharing()->busy(); }));
        CHECK(backend.sharing()->preview().value("ok").toBool());
        CHECK(backend.sharing()->preview().value("playsetName").toString() == playsetName);
        waitUntil([] { return false; }, std::chrono::milliseconds(500)); // the preview renders
        QMetaObject::invokeMethod(window, "closeDialogs");
        REQUIRE(waitUntil([&] { return !backend.sharing()->hasPreview(); }));

        // Filtering, sorting, theme, edits and deletion with the restore banner.
        backend.modView()->setStatusFilter(QStringLiteral("disabled"));
        backend.modView()->setSortMode(QStringLiteral("name"));
        backend.settings()->setTheme(QStringLiteral("dark"));
        waitUntil([] { return false; }, std::chrono::milliseconds(150));
        backend.modView()->setStatusFilter(QStringLiteral("all"));
        backend.modView()->setSortMode(QStringLiteral("order"));
        backend.mods()->moveRows({1, 2}, -1);
        backend.mods()->setEnabled(0, false);
        REQUIRE(waitUntil([&] { return !backend.mods()->busy(); }));
        backend.playsets()->remove(static_cast<int>(backend.activePlaysetId()));
        REQUIRE(waitUntil([&] { return backend.playsets()->canRestore(); }));
        waitUntil([] { return false; }, std::chrono::milliseconds(150));
        backend.playsets()->restoreLast();
        REQUIRE(waitUntil([&] { return backend.mods()->count() == 3; }));

        window->close();
        backend.shutdown();
        runner.shutdown();
    }
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
    // Shown as text: nothing tried to load the images, so nothing was refused
    // and nothing reached the listener.
    CHECK(refusals.count() == 0);
    CHECK(listener.connections() == 0);
}

TEST_CASE("The QML engine's network access refuses everything but local resources", "[app][qml]") {
    qputenv("QT_QUICK_BACKEND", "software");
    TempDir temp;
    using Factory = OfflineNetworkAccessManagerFactory;
    CHECK(Factory::allowed(QUrl(QStringLiteral("qrc:/qt/qml/Pmm/Ui/Main.qml"))));
    CHECK(Factory::allowed(QUrl(QStringLiteral("data:text/plain,hello"))));
    CHECK(Factory::allowed(QUrl::fromLocalFile(qs(platform::pathToUtf8(temp.path())))));
    CHECK_FALSE(Factory::allowed(QUrl(QStringLiteral("file://server/share/x.png")))); // UNC
    CHECK_FALSE(Factory::allowed(QUrl(QStringLiteral("http://127.0.0.1/x.png"))));
    CHECK_FALSE(Factory::allowed(QUrl(QStringLiteral("https://example.com/x.png"))));
    CHECK_FALSE(Factory::allowed(QUrl(QStringLiteral("ftp://example.com/x.png"))));
    CHECK_FALSE(Factory::allowed(QUrl(QStringLiteral("steam://url/CommunityFilePage/1"))));

    Listener listener;
    RefusalLog refusals;
    Factory factory;
    {
        std::unique_ptr<QNetworkAccessManager> manager(factory.create(nullptr));
        std::unique_ptr<QNetworkReply> refused(
            manager->get(QNetworkRequest(QUrl(listener.url("direct")))));
        REQUIRE(waitUntil([&] { return refused->isFinished(); }));
        CHECK(refused->error() != QNetworkReply::NoError);
        std::unique_ptr<QNetworkReply> data(
            manager->get(QNetworkRequest(QUrl(QStringLiteral("data:text/plain,hello")))));
        REQUIRE(waitUntil([&] { return data->isFinished(); }));
        CHECK(data->error() == QNetworkReply::NoError);
        CHECK(data->readAll() == "hello");
    }
    CHECK(refusals.count() == 1);
    CHECK(listener.connections() == 0);

    // What it guards against: Text.AutoText takes a leading tag for rich text
    // and fetches the image. Without the factory the listener sees it.
    const auto showTag = [&](QQmlEngine& engine, const QString& file) {
        QQmlComponent component(&engine);
        component.setData(QStringLiteral("import QtQuick\n"
                                         "Window { visible: true; width: 200; height: 40\n"
                                         "    Text { text: '<img src=\"%1\">' } }")
                              .arg(listener.url(file))
                              .toUtf8(),
                          QUrl());
        std::unique_ptr<QObject> window(component.create());
        REQUIRE(window);
        return window;
    };
    {
        QQmlEngine engine;
        const auto window = showTag(engine, QStringLiteral("unguarded.png"));
        CHECK(waitUntil([&] { return listener.requested(QStringLiteral("unguarded.png")); }));
    }
    {
        QQmlEngine engine;
        engine.setNetworkAccessManagerFactory(&factory);
        const auto window = showTag(engine, QStringLiteral("guarded.png"));
        CHECK(waitUntil([&] { return refusals.count() == 2; }));
        waitUntil([] { return false; }, std::chrono::milliseconds(300));
    }
    CHECK_FALSE(listener.requested(QStringLiteral("guarded.png")));
}

TEST_CASE("Pmm.Ui keyboard paths work against the real backend", "[app][qml][a11y]") {
    qputenv("QT_QUICK_BACKEND", "software");
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("1001", "Alpha");
    world.addWorkshopMod("1002", "Beta");
    world.addWorkshopMod("1003", "Gamma");
    world.addLocalMod("delta", "Delta");
    FakeAdapter adapter(world);
    RunnerOptions options;
    options.runtime = hermeticOptions(temp.path() / "profile", adapter);
    options.steam = false;
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);

    QmlWarnings warnings;
    {
        Backend backend(runner, {});
        backend.initialize(start);
        const auto registration = registerBackend(backend);
        QQmlApplicationEngine engine;
        engine.loadFromModule("Pmm.Ui", "Main");
        REQUIRE(engine.rootObjects().size() == 1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        REQUIRE(window);
        REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
        backend.playsets()->create(QStringLiteral("Keyboard"));
        REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
        backend.mods()->addMods(
            {"steam-workshop:1001", "steam-workshop:1002", "steam-workshop:1003", "local:delta"});
        REQUIRE(waitUntil([&] { return backend.mods()->count() == 4 && !backend.mods()->busy(); }));

        // Key events and window shortcuts need the window to be the focus window.
        window->requestActivate();
        REQUIRE(QTest::qWaitForWindowActive(window));
        QObject* list = window->findChild<QObject*>(QStringLiteral("modList"));
        REQUIRE(list);
        const auto key = [&](Qt::Key k, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QTest::keyClick(window, k, modifiers);
            QCoreApplication::processEvents();
        };
        const auto name = [&](int row) {
            return backend.mods()->get(row).value("name").toString();
        };
        QMetaObject::invokeMethod(window, "selectMod", Q_ARG(int, 0));
        REQUIRE(hasActiveFocus(list));

        SECTION("Tab visits the action column, the games column and the mods column in order") {
            QStringList visited;
            bool back = false;
            for (int i = 0; i < 80 && !back; ++i) {
                key(Qt::Key_Tab);
                const QString pane = focusedPane(window);
                if (!pane.isEmpty() && (visited.isEmpty() || visited.back() != pane))
                    visited.push_back(pane);
                back = hasActiveFocus(list);
            }
            INFO(visited.join(" > ").toStdString());
            CHECK(back);
            CHECK(visited.join(" > ") == "sidePane > gamesPane > modsPane");
        }

        SECTION("F6 and Shift+F6 cycle the columns") {
            key(Qt::Key_F6);
            CHECK(focusedPane(window) == "sidePane");
            key(Qt::Key_F6);
            CHECK(focusedPane(window) == "gamesPane");
            key(Qt::Key_F6);
            CHECK(hasActiveFocus(list));
            key(Qt::Key_F6, Qt::ShiftModifier);
            CHECK(focusedPane(window) == "gamesPane");
        }

        SECTION("Alt+Up/Down and Ctrl+Up/Down reorder the selection") {
            QMetaObject::invokeMethod(window, "selectMod", Q_ARG(int, 1));
            key(Qt::Key_Down, Qt::AltModifier);
            CHECK(name(2) == "Beta");
            key(Qt::Key_Down, Qt::ControlModifier);
            CHECK(name(3) == "Beta");
            key(Qt::Key_Up, Qt::AltModifier);
            CHECK(name(2) == "Beta");
            CHECK(list->property("currentIndex").toInt() == 2);
            REQUIRE(waitUntil([&] { return !backend.mods()->busy(); }));
            key(Qt::Key_Z, Qt::ControlModifier);
            key(Qt::Key_Z, Qt::ControlModifier);
            key(Qt::Key_Z, Qt::ControlModifier);
            REQUIRE(waitUntil([&] { return !backend.mods()->busy(); }));
            CHECK(name(1) == "Beta");
        }

        SECTION("Escape closes dialogs and the context menu and gives focus back") {
            QObject* confirm = window->findChild<QObject*>(QStringLiteral("confirmDialog"));
            REQUIRE(confirm);
            key(Qt::Key_Delete);
            REQUIRE(waitUntil([&] { return confirm->property("opened").toBool(); }));
            key(Qt::Key_Escape);
            REQUIRE(waitUntil([&] { return !confirm->property("visible").toBool(); }));
            CHECK(backend.mods()->count() == 4);
            CHECK(waitUntil([&] { return hasActiveFocus(list); }));

            key(Qt::Key_F10, Qt::ShiftModifier);
            REQUIRE(waitUntil([&] {
                QQuickItem* item = window->activeFocusItem();
                return item && item->property("text").toString() == "Enable";
            }));
            key(Qt::Key_Escape);
            CHECK(waitUntil([&] { return hasActiveFocus(list); }));

            QObject* shortcuts = window->findChild<QObject*>(QStringLiteral("shortcutsDialog"));
            REQUIRE(shortcuts);
            key(Qt::Key_F1);
            REQUIRE(waitUntil([&] { return shortcuts->property("opened").toBool(); }));
            key(Qt::Key_Escape);
            REQUIRE(waitUntil([&] { return !shortcuts->property("visible").toBool(); }));
            CHECK(waitUntil([&] { return hasActiveFocus(list); }));
        }

        window->close();
        backend.shutdown();
        runner.shutdown();
    }
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}

namespace {

// A repository under another name than the compiled one, as after a rename.
const QString kRenamed = QStringLiteral("Someone-Else/renamed-manager");
const QUrl kRenamedPage(
    QStringLiteral("https://github.com/Someone-Else/renamed-manager/releases/tag/v1.2.3"));

// The window on a hermetic profile, at its minimum size, with Settings open and
// the updates on a local stub. start() leaves Settings closed; `daily` turns the
// daily check on, due 50 ms after the start-up refresh.
struct UpdatesHarness {
    TempDir temp;
    FakeWorld world{temp.path() / "world"};
    FakeAdapter adapter{world};
    ReleaseStub stub;
    int starts = 0;
    int quits = 0;
    std::vector<QUrl> pages; // what the browser was asked to open
    bool daily = false;
    bool installed = true;
    QString installUnavailableReason;
    std::unique_ptr<ServiceRunner> runner;
    std::unique_ptr<Backend> backend;
    std::unique_ptr<BackendRegistration> registration;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;

    void open(bool readOnly) {
        start(readOnly);
        openSettings();
    }

    void openSettings() const {
        QMetaObject::invokeMethod(window, "openSettings");
        REQUIRE(waitUntil([&] { return item("settingsDialog")->property("opened").toBool(); }));
    }

    void start(bool readOnly) {
        RunnerOptions runnerOptions;
        runnerOptions.runtime = hermeticOptions(temp.path() / "profile", adapter);
        runnerOptions.steam = false;
        runner = std::make_unique<ServiceRunner>(runnerOptions);
        const RunnerStart start = runner->start();
        REQUIRE(start.ok);

        BackendOptions options;
        options.readOnly = readOnly;
        options.updates.currentVersion = QStringLiteral("0.1.0");
        options.updates.client.endpoints = stub.endpoints();
        options.updates.startProcess = [this](const platform::ProcessSpec&) {
            ++starts;
            return Result<platform::StartedProcess>(platform::StartedProcess{1});
        };
        options.updates.requestQuit = [this] { ++quits; };
        options.updates.openUrl = [this](const QUrl& url) { pages.push_back(url); };
        options.updates.installed = installed;
        options.updates.installUnavailableReason = installUnavailableReason;
        options.updates.dailyCheck = daily;
        options.updates.dailyCheckDelay = std::chrono::milliseconds(50);
        options.updates.today = [] { return QDate(2026, 11, 6); };
        backend = std::make_unique<Backend>(*runner, options);
        backend->initialize(start);
        registration = registerBackend(*backend);
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->loadFromModule("Pmm.Ui", "Main");
        REQUIRE(engine->rootObjects().size() == 1);
        window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
        REQUIRE(window);
        REQUIRE(waitUntil([&] { return backend->initialRefreshDone(); }));
        window->resize(window->minimumSize());
        window->requestActivate();
        REQUIRE(QTest::qWaitForWindowActive(window));
    }

    void close() {
        window->close();
        backend->shutdown();
        runner->shutdown();
        engine.reset();
        registration.reset();
        backend.reset();
    }

    QObject* item(const char* name) const {
        QObject* found = window->findChild<QObject*>(QLatin1String(name));
        REQUIRE(found);
        return found;
    }
    QString text(const char* name) const { return item(name)->property("text").toString(); }
    bool visible(const char* name) const { return item(name)->property("visible").toBool(); }
    void key(Qt::Key k) const {
        QTest::keyClick(window, k);
        QCoreApplication::processEvents();
    }
    // The way a keyboard user gets there; false if Tab never reaches it.
    bool tabTo(const char* name, int limit = 16) const {
        for (int i = 0; i < limit; ++i) {
            if (hasActiveFocus(item(name)))
                return true;
            key(Qt::Key_Tab);
        }
        return hasActiveFocus(item(name));
    }
    QVariant evaluate(const char* name, const QString& expression) const {
        QObject* object = item(name);
        return QQmlExpression(qmlContext(object), object, expression).evaluate();
    }
    QString accessibleName(const char* name) const {
        return evaluate(name, QStringLiteral("Accessible.name")).toString();
    }
    // The window's own layout (ids of Main.qml).
    QVariant evaluateInWindow(const QString& expression) const {
        return QQmlExpression(qmlContext(window), window, expression).evaluate();
    }
    // With PMM_UI_CAPTURE_DIR set, the window is saved there for a look at the
    // layout. Nothing is written otherwise.
    void capture(const QString& name) const {
        const QString dir = qEnvironmentVariable("PMM_UI_CAPTURE_DIR");
        if (dir.isEmpty())
            return;
        waitUntil([] { return false; }, std::chrono::milliseconds(300));
        CHECK(window->grabWindow().save(QDir(dir).filePath(name + QStringLiteral(".png"))));
    }
    bool waitForState(const char* state) const {
        return waitUntil([&] { return backend->updates()->state() == QLatin1String(state); });
    }
    // The dialog stays inside the smallest window, whatever the section shows.
    void checkFits() const {
        const qreal height = item("settingsDialog")->property("height").toReal();
        INFO("dialog height " << height << " in a window of " << window->height());
        CHECK(height <= window->height() - 32);
    }
};

} // namespace

TEST_CASE("Rows in the Add mods dialog show their selection at once", "[app][qml]") {
    qputenv("QT_QUICK_BACKEND", "software");
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("1001", "Alpha");
    world.addWorkshopMod("1002", "Beta");
    world.addWorkshopMod("1003", "Gamma");
    FakeAdapter adapter(world);
    RunnerOptions options;
    options.runtime = hermeticOptions(temp.path() / "profile", adapter);
    options.steam = false;
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);

    QmlWarnings warnings;
    {
        Backend backend(runner, {});
        backend.initialize(start);
        const auto registration = registerBackend(backend);
        QQmlApplicationEngine engine;
        engine.loadFromModule("Pmm.Ui", "Main");
        REQUIRE(engine.rootObjects().size() == 1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        REQUIRE(window);
        REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
        backend.playsets()->create(QStringLiteral("Picking"));
        REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
        window->requestActivate();
        REQUIRE(QTest::qWaitForWindowActive(window));

        REQUIRE(QMetaObject::invokeMethod(window, "openAddMods"));
        auto* dialog = window->findChild<QObject*>(QStringLiteral("addModsDialog"));
        REQUIRE(dialog);
        REQUIRE(waitUntil([&] { return dialog->property("opened").toBool(); }));
        auto* list = window->findChild<QQuickItem*>(QStringLiteral("inventoryList"));
        REQUIRE(list);
        REQUIRE(waitUntil([&] { return list->property("count").toInt() == 3; }));
        list->forceActiveFocus();
        REQUIRE(hasActiveFocus(list));

        const auto currentRow = [&] { return list->property("currentItem").value<QObject*>(); };
        const auto press = [&](Qt::Key k) {
            QTest::keyClick(window, k);
            QCoreApplication::processEvents();
        };
        REQUIRE(currentRow());
        CHECK_FALSE(currentRow()->property("checked").toBool());

        // Selecting: the row shows it, and so does the count.
        press(Qt::Key_Space);
        CHECK(currentRow()->property("checked").toBool());
        CHECK(dialog->property("pickedCount").toInt() == 1);

        // A second row is independent of the first.
        QObject* first = currentRow();
        press(Qt::Key_Down);
        REQUIRE(currentRow() != first);
        CHECK_FALSE(currentRow()->property("checked").toBool());
        press(Qt::Key_Space);
        CHECK(currentRow()->property("checked").toBool());
        CHECK(first->property("checked").toBool());
        CHECK(dialog->property("pickedCount").toInt() == 2);

        // Selecting again clears only that row.
        press(Qt::Key_Space);
        CHECK_FALSE(currentRow()->property("checked").toBool());
        CHECK(first->property("checked").toBool());
        CHECK(dialog->property("pickedCount").toInt() == 1);

        // The mouse takes the same path: click the last row.
        press(Qt::Key_Down);
        auto* last = qobject_cast<QQuickItem*>(currentRow());
        REQUIRE(last);
        CHECK_FALSE(last->property("checked").toBool());
        const QPoint center =
            last->mapToScene(QPointF(last->width() / 2, last->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center);
        REQUIRE(waitUntil([&] { return dialog->property("pickedCount").toInt() == 2; }));
        CHECK(last->property("checked").toBool());
        CHECK(first->property("checked").toBool());

        QMetaObject::invokeMethod(window, "closeDialogs");
        window->close();
        backend.shutdown();
    }
    runner.shutdown();
    CHECK(warnings.list().isEmpty());
}

TEST_CASE("Play is available without a playset and says the game starts without mods",
          "[app][qml][a11y]") {
    qputenv("QT_QUICK_BACKEND", "software");
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("1001", "Alpha");
    FakeAdapter adapter(world);
    RunnerOptions options;
    options.runtime = hermeticOptions(temp.path() / "profile", adapter);
    const auto hermetic = options.runtime.configureContext;
    options.runtime.configureContext = [hermetic](services::ServiceContext& context) {
        hermetic(context);
        // Pretends to start the game; nothing is executed.
        context.startProcess = [](const platform::ProcessSpec&) {
            return Result<platform::StartedProcess>(platform::StartedProcess{4242});
        };
    };
    options.steam = false;
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);

    QmlWarnings warnings;
    {
        Backend backend(runner, {});
        backend.initialize(start);
        const auto registration = registerBackend(backend);
        QQmlApplicationEngine engine;
        engine.loadFromModule("Pmm.Ui", "Main");
        REQUIRE(engine.rootObjects().size() == 1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        REQUIRE(window);
        REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
        REQUIRE(backend.playsets()->activeId() == -1);
        REQUIRE(backend.launcher()->canPlay());

        auto* play = window->findChild<QQuickItem*>(QStringLiteral("playButton"));
        REQUIRE(play);
        // In the Play section's context, where `root` is the section.
        const auto evaluate = [&](const char* expression) {
            return QQmlExpression(qmlContext(play), play, QString::fromLatin1(expression))
                .evaluate();
        };
        CHECK(play->property("available").toBool());
        CHECK(evaluate("Accessible.description").toString() == "Starts the game without mods");
        CHECK(evaluate("toolTipText").toString().contains("without mods"));
        CHECK(evaluate("root.warningLines.join('|')").toString() ==
              "No playset is selected, so the game starts without mods.");

        backend.launcher()->play();
        REQUIRE(waitUntil([&] { return backend.launcher()->state() == QLatin1String("started"); }));
        CHECK(evaluate("root.stateText('started')").toString() ==
              "Stellaris is running without mods.");

        // With a playset, Play applies it (an empty one also starts without
        // mods). The running game was started without one, and still says so.
        backend.playsets()->create(QStringLiteral("Campaign"));
        REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
        REQUIRE(waitUntil([&] {
            return evaluate("Accessible.description").toString() ==
                   "Starts the game with this playset";
        }));
        CHECK(play->property("available").toBool());
        CHECK(evaluate("root.stateText('started')").toString() ==
              "Stellaris is running without mods.");
        backend.launcher()->play();
        REQUIRE(waitUntil([&] { return backend.launcher()->startedWithPlayset(); }));
        CHECK(backend.launcher()->state() == QLatin1String("started"));
        CHECK(evaluate("root.stateText('started')").toString() ==
              "Stellaris is running with this playset.");

        window->close();
        backend.shutdown();
    }
    runner.shutdown();
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}

TEST_CASE("Play says Steam is not running and becomes available once it runs", "[app][qml][a11y]") {
    qputenv("QT_QUICK_BACKEND", "software");
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    auto steam = std::make_shared<std::atomic<bool>>(false);
    RunnerOptions options;
    options.runtime = hermeticOptions(temp.path() / "profile", adapter);
    const auto hermetic = options.runtime.configureContext;
    options.runtime.configureContext = [hermetic, steam](services::ServiceContext& context) {
        hermetic(context);
        context.listProcesses = [steam] {
            std::vector<platform::RunningProcess> processes{{7, "explorer.exe", {}}};
            if (*steam)
                processes.push_back(steamClient());
            return Result<std::vector<platform::RunningProcess>>(std::move(processes));
        };
    };
    options.steam = false;
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);

    QmlWarnings warnings;
    {
        BackendOptions backendOptions;
        backendOptions.launchRecheckMs = 50;
        Backend backend(runner, backendOptions);
        backend.initialize(start);
        const auto registration = registerBackend(backend);
        QQmlApplicationEngine engine;
        engine.loadFromModule("Pmm.Ui", "Main");
        REQUIRE(engine.rootObjects().size() == 1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        REQUIRE(window);
        REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));

        auto* play = window->findChild<QQuickItem*>(QStringLiteral("playButton"));
        REQUIRE(play);
        const auto evaluate = [&](const char* expression) {
            return QQmlExpression(qmlContext(play), play, QString::fromLatin1(expression))
                .evaluate();
        };
        const QString blocker =
            QStringLiteral("Steam is not running. Start Steam and sign in, then launch Stellaris.");
        CHECK_FALSE(play->property("available").toBool());
        CHECK(play->isEnabled()); // focusable, so the keyboard reaches the reasons
        CHECK(evaluate("root.blockerLines.join('|')").toString() == blocker);
        CHECK(evaluate("toolTipText").toString() == blocker);
        CHECK(evaluate("Accessible.description").toString() == "Blocked: " + blocker);

        // Steam starts; nothing else happens in the window.
        *steam = true;
        REQUIRE(waitUntil([&] { return play->property("available").toBool(); }));
        CHECK(evaluate("root.blockerLines.length").toInt() == 0);
        CHECK(evaluate("Accessible.description").toString() == "Starts the game without mods");

        window->close();
        backend.shutdown();
    }
    runner.shutdown();
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}

namespace {

// The QML expression in `object`'s context (its component's ids, `root`, ...).
QVariant evaluateIn(QObject* object, const QString& expression) {
    return QQmlExpression(qmlContext(object), object, expression).evaluate();
}

// Presses Tab until `item` has the focus; false if it never gets it.
bool tabTo(QQuickWindow* window, QObject* item, int limit) {
    for (int i = 0; i < limit && !hasActiveFocus(item); ++i) {
        QTest::keyClick(window, Qt::Key_Tab);
        QCoreApplication::processEvents();
    }
    return hasActiveFocus(item);
}

} // namespace

TEST_CASE("A blocked Play is reachable with Tab, says why, and launches once it is allowed",
          "[app][qml][a11y]") {
    qputenv("QT_QUICK_BACKEND", "software");
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("1001", "Alpha");
    FakeAdapter adapter(world);
    auto steam = std::make_shared<std::atomic<bool>>(false);
    auto launches = std::make_shared<std::atomic<int>>(0);
    RunnerOptions options;
    options.runtime = hermeticOptions(temp.path() / "profile", adapter);
    const auto hermetic = options.runtime.configureContext;
    options.runtime.configureContext = [hermetic, steam,
                                        launches](services::ServiceContext& context) {
        hermetic(context);
        context.listProcesses = [steam] {
            std::vector<platform::RunningProcess> processes{{7, "explorer.exe", {}}};
            if (*steam)
                processes.push_back(steamClient());
            return Result<std::vector<platform::RunningProcess>>(std::move(processes));
        };
        // Counts the launches; nothing is executed.
        context.startProcess = [launches](const platform::ProcessSpec&) {
            ++*launches;
            return Result<platform::StartedProcess>(platform::StartedProcess{4242});
        };
    };
    options.steam = false;
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);

    QmlWarnings warnings;
    {
        BackendOptions backendOptions;
        backendOptions.launchRecheckMs = 50;
        Backend backend(runner, backendOptions);
        backend.initialize(start);
        const auto registration = registerBackend(backend);
        QQmlApplicationEngine engine;
        engine.loadFromModule("Pmm.Ui", "Main");
        REQUIRE(engine.rootObjects().size() == 1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        REQUIRE(window);
        REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
        backend.playsets()->create(QStringLiteral("Blocked"));
        REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
        backend.mods()->addMods({"steam-workshop:1001"});
        REQUIRE(waitUntil([&] { return backend.mods()->count() == 1 && !backend.mods()->busy(); }));
        window->requestActivate();
        REQUIRE(QTest::qWaitForWindowActive(window));

        auto* play = window->findChild<QQuickItem*>(QStringLiteral("playButton"));
        REQUIRE(play);
        const auto evaluate = [&](const char* expression) {
            return evaluateIn(play, QString::fromLatin1(expression));
        };
        const auto key = [&](Qt::Key k) {
            QTest::keyClick(window, k);
            QCoreApplication::processEvents();
        };
        const QString blocker =
            QStringLiteral("Steam is not running. Start Steam and sign in, then launch Stellaris.");
        REQUIRE(waitUntil(
            [&] { return evaluate("root.blockerLines.join('|')").toString() == blocker; }));
        CHECK_FALSE(play->property("available").toBool());

        // From the mod list, Tab reaches it like any other button.
        QMetaObject::invokeMethod(window, "selectMod", Q_ARG(int, 0));
        REQUIRE(tabTo(window, play, 40));
        CHECK(evaluate("Accessible.name").toString() == "Play Stellaris, unavailable");
        CHECK(evaluate("Accessible.description").toString() == "Blocked: " + blocker);
        CHECK(evaluate("toolTipText").toString() == blocker);

        // Enter, Space and a click show the reasons instead of launching.
        QSignalSpy posted(backend.notifications(), &NotificationCenter::posted);
        key(Qt::Key_Return);
        key(Qt::Key_Space);
        const QPoint center =
            play->mapToScene(QPointF(play->width() / 2, play->height() / 2)).toPoint();
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center);
        REQUIRE(posted.size() == 3);
        for (const QList<QVariant>& arguments : posted) {
            CHECK(arguments.at(0).toString() == "warning");
            CHECK(arguments.at(1).toString() == "The game cannot be started yet");
            CHECK(arguments.at(2).toString() == blocker);
        }
        waitUntil([] { return false; }, std::chrono::milliseconds(300));
        CHECK(*launches == 0);
        CHECK(backend.launcher()->state() == QLatin1String("idle"));
        CHECK(play->hasActiveFocus());

        // Steam starts: Play becomes available where the focus already is, and
        // the same key launches.
        *steam = true;
        REQUIRE(waitUntil([&] { return play->property("available").toBool(); }));
        CHECK(play->hasActiveFocus());
        CHECK(evaluate("Accessible.name").toString() == "Play Stellaris");
        CHECK(evaluate("Accessible.description").toString() == "Starts the game with this playset");
        posted.clear();
        key(Qt::Key_Return);
        REQUIRE(waitUntil([&] { return backend.launcher()->state() == QLatin1String("started"); }));
        CHECK(*launches == 1);
        for (const QList<QVariant>& arguments : posted)
            CHECK(arguments.at(1).toString() != "The game cannot be started yet");
        // It kept the focus while the launch ran.
        CHECK(play->hasActiveFocus());

        window->close();
        backend.shutdown();
    }
    runner.shutdown();
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}

TEST_CASE(
    "A change Recover cannot undo is discarded only after a confirmation that names its files",
    "[app][qml][a11y]") {
    qputenv("QT_QUICK_BACKEND", "software");
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    FakeAdapter adapter(world);
    const fs::path profile = temp.path() / "profile";
    // A launch that crashed after writing its file, and whose backup is gone.
    const fs::path target = world.userData / "load_order.txt";
    writeText(target, "before");
    platform::TransactionRecord record;
    {
        platform::FileTransactionManager journal(profile / "transactions");
        platform::FileTransactionFaults crash;
        crash.crashAfterWrites = std::size_t{1};
        journal.setFaultsForTesting(crash);
        REQUIRE_FALSE(journal.apply("launch stellaris without mods", {{target, "after"}}).ok());
        record = require(journal.listIncomplete()).at(0);
        fs::remove(record.directory / record.entries.at(0).backupFile);
    }
    RunnerOptions options;
    options.runtime = hermeticOptions(profile, adapter);
    options.steam = false;
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    const auto stateOf = [&] {
        return runner.callBlocking([id = record.id](AppRuntime& runtime) {
            auto loaded = runtime.context().transactions.load(id);
            return loaded ? loaded.value().state : platform::TransactionState::Prepared;
        });
    };

    QmlWarnings warnings;
    {
        Backend backend(runner, {});
        backend.initialize(start);
        const auto registration = registerBackend(backend);
        QQmlApplicationEngine engine;
        engine.loadFromModule("Pmm.Ui", "Main");
        REQUIRE(engine.rootObjects().size() == 1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        REQUIRE(window);
        REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));
        auto* banner = window->findChild<QQuickItem*>(QStringLiteral("interruptedBanner"));
        auto* discard = window->findChild<QQuickItem*>(QStringLiteral("discardInterruptedButton"));
        auto* play = window->findChild<QQuickItem*>(QStringLiteral("playButton"));
        QObject* confirm = window->findChild<QObject*>(QStringLiteral("confirmDialog"));
        REQUIRE(banner);
        REQUIRE(discard);
        REQUIRE(play);
        REQUIRE(confirm);
        CHECK(banner->isVisible());
        CHECK_FALSE(play->property("available").toBool());

        // Offered only once Recover has failed for good.
        CHECK_FALSE(discard->isVisible());
        backend.launcher()->recoverInterrupted();
        REQUIRE(waitUntil([&] { return discard->isVisible(); }));
        CHECK(banner->property("text").toString().contains("a backup is missing or damaged"));
        CHECK(discard->property("text").toString() == "Discard this record");

        window->requestActivate();
        REQUIRE(QTest::qWaitForWindowActive(window));
        const auto key = [&](Qt::Key k) {
            QTest::keyClick(window, k);
            QCoreApplication::processEvents();
        };
        const auto openConfirmation = [&] {
            REQUIRE(QMetaObject::invokeMethod(discard, "clicked"));
            REQUIRE(waitUntil([&] { return confirm->property("opened").toBool(); }));
        };

        // The confirmation names the file and where the backups stay; Cancel
        // has the focus, so Enter cancels, as Escape does.
        openConfirmation();
        CHECK(confirm->property("message").toString().contains(qpath(target)));
        CHECK(confirm->property("detail").toString().contains(qpath(record.directory)));
        CHECK(confirm->property("detail").toString().contains("Paradox launcher"));
        REQUIRE(waitUntil([&] {
            QQuickItem* item = window->activeFocusItem();
            return item && item->objectName() == "cancelButton";
        }));
        CHECK(window->activeFocusItem()->property("text").toString() == "Cancel");
        key(Qt::Key_Return);
        REQUIRE(waitUntil([&] { return !confirm->property("visible").toBool(); }));
        openConfirmation();
        key(Qt::Key_Escape);
        REQUIRE(waitUntil([&] { return !confirm->property("visible").toBool(); }));
        CHECK(backend.launcher()->canDiscardInterrupted());
        CHECK(backend.launcher()->hasInterruptedTransaction());
        CHECK(stateOf() == platform::TransactionState::Applying);

        // Confirming from the keyboard discards it: the banner goes, Play is
        // available, and the focus leaves the hidden action for the mod list.
        discard->forceActiveFocus(Qt::TabFocusReason);
        key(Qt::Key_Return);
        REQUIRE(waitUntil([&] { return confirm->property("opened").toBool(); }));
        auto* confirmButton = window->findChild<QQuickItem*>(QStringLiteral("confirmButton"));
        REQUIRE(confirmButton);
        CHECK(confirmButton->property("text").toString() == "Discard");
        key(Qt::Key_Tab);
        REQUIRE(waitUntil([&] { return confirmButton->hasActiveFocus(); }));
        key(Qt::Key_Return);
        REQUIRE(waitUntil([&] { return !banner->isVisible(); }));
        REQUIRE(waitUntil([&] { return play->property("available").toBool(); }));
        REQUIRE(waitUntil([&] {
            QQuickItem* item = window->activeFocusItem();
            return item && item->objectName() == "modList";
        }));
        CHECK_FALSE(discard->hasActiveFocus());
        CHECK(stateOf() == platform::TransactionState::Abandoned);
        CHECK(require(platform::readFile(target)) == "after");

        window->close();
        backend.shutdown();
    }
    runner.shutdown();
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}

TEST_CASE("The Updates section works with the keyboard", "[app][qml][a11y][update]") {
    qputenv("QT_QUICK_BACKEND", "software");
    UpdatesHarness h;
    h.stub.serveRelease("1.2.3", QByteArray("installer"));
    QmlWarnings warnings;
    h.open(false);

    CHECK(h.visible("updateCheckButton"));
    CHECK_FALSE(h.visible("updateAction"));
    CHECK_FALSE(h.visible("updateError"));
    CHECK_FALSE(h.visible("updateDisabledReason"));
    CHECK(h.accessibleName("updateCheckButton") == "Check for updates");
    CHECK(h.stub.connections() == 0);
    h.checkFits();

    REQUIRE(h.tabTo("updateCheckButton"));
    h.key(Qt::Key_Return);
    REQUIRE(h.waitForState("available"));
    CHECK(h.text("updateStatus") == "Version 1.2.3 is available.");
    CHECK(h.accessibleName("updateStatus") == "Version 1.2.3 is available.");
    CHECK(h.text("updateAction") == "Update and restart");
    CHECK(h.accessibleName("updateAction") == "Update and restart");
    // The button that was pressed went away for a moment: focus stayed in the section.
    CHECK(waitUntil([&] { return hasActiveFocus(h.item("updateAction")); }));
    h.checkFits();

    h.key(Qt::Key_Return);
    REQUIRE(h.waitForState("installing"));
    CHECK(h.starts == 1);
    CHECK(h.quits == 1);
    CHECK(h.text("updateStatus").startsWith("Starting the installer"));
    h.checkFits();

    h.close();
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}

TEST_CASE("The Updates section shows errors with a way out", "[app][qml][update]") {
    qputenv("QT_QUICK_BACKEND", "software");
    UpdatesHarness h;
    // Under a new name, as after a rename: the way out follows the reply.
    h.stub.serveRelease("1.2.3", QByteArray("installer"), kRenamed);
    h.stub.routes[ReleaseStub::installerPath("1.2.3", kRenamed)].body = "tampered!";
    QmlWarnings warnings;
    h.open(false);

    h.backend->updates()->check();
    REQUIRE(h.waitForState("available"));
    QMetaObject::invokeMethod(h.item("updateAction"), "clicked");
    REQUIRE(h.waitForState("failed"));
    CHECK(h.visible("updateError"));
    CHECK(h.item("updateError")->property("text").toString().contains("does not match"));
    CHECK(h.text("updateAction") == "Try again");
    CHECK(h.visible("updateReleasePage"));
    CHECK(h.starts == 0);
    h.checkFits();

    QMetaObject::invokeMethod(h.item("updateReleasePage"), "clicked");
    REQUIRE(h.pages.size() == 1);
    CHECK(h.pages[0] == kRenamedPage);

    // Try again downloads anew.
    h.stub.routes[ReleaseStub::installerPath("1.2.3", kRenamed)].body = "installer";
    QMetaObject::invokeMethod(h.item("updateAction"), "clicked");
    REQUIRE(h.waitForState("installing"));
    CHECK(h.starts == 1);

    h.close();
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}

TEST_CASE("The Updates section explains why it is off", "[app][qml][update]") {
    qputenv("QT_QUICK_BACKEND", "software");
    UpdatesHarness h;
    QmlWarnings warnings;
    h.open(true);
    CHECK(h.visible("updateDisabledReason"));
    CHECK(h.item("updateDisabledReason")->property("text").toString().contains("read-only"));
    CHECK_FALSE(h.item("updateCheckButton")->property("enabled").toBool());
    CHECK_FALSE(h.tabTo("updateCheckButton"));
    CHECK_FALSE(h.item("updateAutoCheck")->property("enabled").toBool());
    CHECK_FALSE(h.tabTo("updateAutoCheck"));
    CHECK_FALSE(h.visible("updateInstallUnavailable"));
    h.checkFits();
    CHECK(h.stub.connections() == 0);

    h.close();
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}

TEST_CASE("The daily check can be turned off with the keyboard", "[app][qml][a11y][update]") {
    qputenv("QT_QUICK_BACKEND", "software");
    UpdatesHarness h;
    QmlWarnings warnings;
    h.open(false);

    QObject* toggle = h.item("updateAutoCheck");
    CHECK(h.visible("updateAutoCheck"));
    CHECK(toggle->property("enabled").toBool());
    CHECK(toggle->property("checked").toBool()); // on by default
    CHECK(h.accessibleName("updateAutoCheck") == "Check for updates automatically");
    const QString hint = h.text("updateAutoCheckHint");
    CHECK(hint.contains("GitHub once a day"));
    CHECK(h.accessibleName("updateAutoCheckHint") == hint);
    CHECK(h.evaluate("updateAutoCheck", QStringLiteral("Accessible.description")).toString() ==
          hint);
    CHECK_FALSE(h.visible("updateInstallUnavailable"));
    h.checkFits();
    h.capture(QStringLiteral("settings-updates-minimum"));

    const auto stored = [&] {
        return h.runner->callBlocking([](AppRuntime& runtime) {
            persistence::SettingsRepository settings(runtime.database());
            auto value = settings.get("app.updates.checkAutomatically");
            return value && value.value() ? *value.value() : std::string();
        });
    };
    REQUIRE(h.tabTo("updateAutoCheck"));
    h.key(Qt::Key_Space);
    CHECK_FALSE(toggle->property("checked").toBool());
    CHECK_FALSE(h.backend->settings()->checkUpdatesAutomatically());
    CHECK(stored() == "false"); // without closing the dialog or quitting
    h.key(Qt::Key_Space);
    CHECK(h.backend->settings()->checkUpdatesAutomatically());
    CHECK(stored() == "true");
    CHECK(h.stub.connections() == 0);

    h.close();
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}

TEST_CASE("The daily check's banner offers the update and waits for the click",
          "[app][qml][a11y][update]") {
    qputenv("QT_QUICK_BACKEND", "software");
    UpdatesHarness h;
    h.world.addWorkshopMod("1001", "Alpha");
    h.world.addLocalMod("beta", "Beta");
    h.stub.serveRelease("1.2.3", QByteArray("installer"));
    h.daily = true;
    QmlWarnings warnings;
    h.start(false);
    h.backend->playsets()->create(QStringLiteral("Daily"));
    REQUIRE(waitUntil([&] { return h.backend->activePlaysetId() >= 0; }));
    h.backend->mods()->addMods({"steam-workshop:1001", "local:beta"});
    QObject* list = h.item("modList");
    QMetaObject::invokeMethod(h.window, "selectMod", Q_ARG(int, 0));

    REQUIRE(h.waitForState("available"));
    REQUIRE(waitUntil([&] { return h.visible("updateBanner"); }));
    CHECK(h.accessibleName("updateBanner") == "Paradox Mod Manager 1.2.3 is available.");
    CHECK(h.text("updateBannerAction") == "Update and restart");
    CHECK(h.accessibleName("updateBannerAction") == "Update and restart");
    CHECK(h.text("updateBannerDismiss") == "Not now");
    CHECK(h.accessibleName("updateBannerDismiss") == "Not now");
    CHECK_FALSE(h.visible("updateBannerProgress"));
    // Above the columns, which move down for it; the keyboard stays where it was.
    const qreal bannerBottom = h.evaluate("updateBanner", QStringLiteral("y + height")).toReal();
    CHECK(h.evaluateInWindow(QStringLiteral("split.y")).toReal() > bannerBottom);
    CHECK(hasActiveFocus(list));
    h.capture(QStringLiteral("update-banner-minimum"));
    h.window->resize(1400, 860);
    h.capture(QStringLiteral("update-banner"));
    h.backend->settings()->setTheme(QStringLiteral("dark"));
    h.capture(QStringLiteral("update-banner-dark"));
    h.window->resize(h.window->minimumSize());

    // Nothing was downloaded or started for the offer.
    waitUntil([] { return false; }, std::chrono::milliseconds(300));
    CHECK(h.stub.requests().size() == 1);
    CHECK(h.stub.requestsFor(ReleaseStub::installerPath("1.2.3")) == 0);
    CHECK(h.starts == 0);

    // The banner's buttons are in the Tab order; Enter starts the update.
    REQUIRE(h.tabTo("updateBannerAction", 80));
    h.key(Qt::Key_Return);
    REQUIRE(h.waitForState("installing"));
    CHECK(h.starts == 1);
    CHECK(h.quits == 1);
    CHECK(h.visible("updateBanner"));
    CHECK(h.accessibleName("updateBanner")
              .endsWith("Starting the installer. The application will close and restart."));

    h.close();
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}

TEST_CASE("Not now hides the update banner for the session", "[app][qml][a11y][update]") {
    qputenv("QT_QUICK_BACKEND", "software");
    UpdatesHarness h;
    h.stub.serveRelease("1.2.3", QByteArray("installer"));
    h.daily = true;
    QmlWarnings warnings;
    h.start(false);
    REQUIRE(waitUntil([&] { return h.visible("updateBanner"); }));

    REQUIRE(h.tabTo("updateBannerDismiss", 80));
    h.key(Qt::Key_Return);
    CHECK_FALSE(h.visible("updateBanner"));
    CHECK(h.evaluateInWindow(QStringLiteral("split.y")).toReal() == 0);
    CHECK(waitUntil([&] { return hasActiveFocus(h.item("modList")); }));

    // Settings still offers it; the banner stays hidden after that.
    h.openSettings();
    CHECK(h.text("updateAction") == "Update and restart");
    h.key(Qt::Key_Escape);
    REQUIRE(waitUntil([&] { return !h.item("settingsDialog")->property("visible").toBool(); }));
    waitUntil([] { return false; }, std::chrono::milliseconds(300));
    CHECK_FALSE(h.visible("updateBanner"));
    CHECK(h.backend->updates()->state() == "available");
    CHECK(h.stub.requestsFor(ReleaseStub::installerPath("1.2.3")) == 0);
    CHECK(h.starts == 0);

    h.close();
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}

TEST_CASE("Copies and sessions that cannot install offer the release page", "[app][qml][update]") {
    qputenv("QT_QUICK_BACKEND", "software");
    const QString reason =
        QStringLiteral("Restart Paradox Mod Manager without --profile or --no-steam to update.");
    UpdatesHarness h;
    h.stub.serveRelease("1.2.3", QByteArray("installer"), kRenamed);
    h.daily = true;
    const bool session = GENERATE(true, false);
    INFO((session ? "--profile session of an installed copy" : "ZIP copy"));
    if (session)
        h.installUnavailableReason = reason;
    else
        h.installed = false;
    QmlWarnings warnings;
    h.start(false);
    REQUIRE(waitUntil([&] { return h.visible("updateBanner"); }));
    CHECK(h.accessibleName("updateBanner") ==
          (session ? "Paradox Mod Manager 1.2.3 is available. " + reason
                   : QStringLiteral("Paradox Mod Manager 1.2.3 is available.")));
    CHECK(h.text("updateBannerAction") == "Open release page");
    if (session)
        h.capture(QStringLiteral("update-banner-session"));

    QMetaObject::invokeMethod(h.item("updateBannerAction"), "clicked");
    REQUIRE(h.pages.size() == 1);
    CHECK(h.pages[0] == kRenamedPage); // where the reply says it is, not the compiled name
    waitUntil([] { return false; }, std::chrono::milliseconds(200));
    CHECK(h.stub.requests().size() == 1);
    CHECK(h.starts == 0);

    h.openSettings();
    CHECK(h.text("updateAction") == "Open release page");
    CHECK(h.visible("updateInstallUnavailable") == session);
    if (session) {
        CHECK(h.text("updateInstallUnavailable") == reason);
        h.window->resize(1400, 860);
        h.capture(QStringLiteral("settings-updates-session"));
    }
    h.checkFits();

    h.close();
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}
