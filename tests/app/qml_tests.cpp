// The real QML module (Pmm.Ui) against the real Backend in a hermetic
// profile: the window and every dialog load and bind without QML warnings,
// the main flows work through the window's own functions, and the keyboard
// paths of docs/accessibility.md work with real key events.

#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQmlApplicationEngine>
#include <QQmlEngineExtensionPlugin>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
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

// The singleton is registered once per process and resolves to the Backend of
// the running test, so several test cases can load the module.
Backend*& currentBackend() {
    static Backend* backend = nullptr;
    return backend;
}

void registerBackend(Backend& backend) {
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
        Backend backend(runner, {});
        backend.initialize(start);
        registerBackend(backend);
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
        registerBackend(backend);
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
