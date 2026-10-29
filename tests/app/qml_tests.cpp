// The real QML module (Pmm.Ui) against the real Backend in a hermetic
// profile: the window and every dialog load and bind without QML warnings,
// and the main flows work through the window's own functions. Names that look
// like rich text never make the window fetch anything, and the engine's
// network access refuses everything but local resources.

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "app/gui/network_guard.hpp"
#include "core/log.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngineExtensionPlugin>
#include <QQmlExpression>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTcpSocket>
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
        Backend::registerQmlTypes();
        Backend backend(runner, {});
        backend.initialize(start);
        qmlRegisterSingletonInstance("Pmm.Backend", 1, 0, "Backend", &backend);
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
        for (const char* open :
             {"openExport", "openAddMods", "openSettings", "openHistory", "openLocate"}) {
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
