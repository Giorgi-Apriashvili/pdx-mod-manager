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
#include "test_support.hpp"
#include "update_stub.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngineExtensionPlugin>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QtQml/qqml.h>

#include <atomic>
#include <memory>
#include <mutex>

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

// The window on a hermetic profile, at its minimum size, with Settings open and
// the updates on a local stub.
struct UpdatesHarness {
    TempDir temp;
    FakeWorld world{temp.path() / "world"};
    FakeAdapter adapter{world};
    ReleaseStub stub;
    int starts = 0;
    int quits = 0;
    int pages = 0;
    std::unique_ptr<ServiceRunner> runner;
    std::unique_ptr<Backend> backend;
    std::unique_ptr<BackendRegistration> registration;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;

    void open(bool readOnly) {
        RunnerOptions runnerOptions;
        runnerOptions.runtime = hermeticOptions(temp.path() / "profile", adapter);
        runnerOptions.steam = false;
        runner = std::make_unique<ServiceRunner>(runnerOptions);
        const RunnerStart start = runner->start();
        REQUIRE(start.ok);

        BackendOptions options;
        options.readOnly = readOnly;
        options.updates.currentVersion = QStringLiteral("0.1.0");
        options.updates.installed = true;
        options.updates.client.endpoints = stub.endpoints();
        options.updates.startProcess = [this](const platform::ProcessSpec&) {
            ++starts;
            return Result<platform::StartedProcess>(platform::StartedProcess{1});
        };
        options.updates.requestQuit = [this] { ++quits; };
        options.updates.openUrl = [this](const QUrl&) { ++pages; };
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
        QMetaObject::invokeMethod(window, "openSettings");
        REQUIRE(waitUntil([&] { return item("settingsDialog")->property("opened").toBool(); }));
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
    bool tabTo(const char* name) const {
        for (int i = 0; i < 16; ++i) {
            if (hasActiveFocus(item(name)))
                return true;
            key(Qt::Key_Tab);
        }
        return hasActiveFocus(item(name));
    }
    QString accessibleName(const char* name) const {
        QObject* object = item(name);
        QQmlExpression expression(qmlContext(object), object, QStringLiteral("Accessible.name"));
        return expression.evaluate().toString();
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
    h.stub.serveRelease("1.2.3", QByteArray("installer"));
    h.stub.routes[ReleaseStub::installerPath("1.2.3")].body = "tampered!";
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
    CHECK(h.pages == 1);

    // Try again downloads anew.
    h.stub.routes[ReleaseStub::installerPath("1.2.3")].body = "installer";
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
    h.checkFits();
    CHECK(h.stub.connections() == 0);

    h.close();
    INFO(warnings.list().join('\n').toStdString());
    CHECK(QmlWarnings::list().isEmpty());
}
