// The real QML module (Pmm.Ui) against the real Backend in a hermetic
// profile: the window and every dialog load and bind without QML warnings,
// and the main flows work through the window's own functions.

#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQmlApplicationEngine>
#include <QQmlEngineExtensionPlugin>
#include <QQuickWindow>
#include <QtQml/qqml.h>

#include <mutex>

Q_IMPORT_QML_PLUGIN(Pmm_UiPlugin)

using namespace pmm;
using namespace pmm::app;
using namespace pmm::app::test;

namespace {

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
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    world.addWorkshopMod("1001", "Alpha");
    world.addWorkshopMod("1002", "Beta");
    world.addLocalMod("gamma", "Gamma");
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
        QQmlApplicationEngine engine;
        engine.loadFromModule("Pmm.Ui", "Main");
        REQUIRE(engine.rootObjects().size() == 1);
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        REQUIRE(window);
        REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }));

        // Empty state, then a playset with mods.
        backend.playsets()->create(QStringLiteral("QML"));
        REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
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
}
