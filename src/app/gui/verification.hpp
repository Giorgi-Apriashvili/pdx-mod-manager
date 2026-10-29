#pragma once

// Hidden verification switches of pmm-app (docs/app.md). They drive the real
// application - backend, models and QML window - without user input, so its
// behaviour against a real installation can be checked and captured:
//
//   --screenshot <png>      grab the window once the initial rescan finished,
//                           after the optional --open/--select-row/--filter
//                           steps, then quit
//   --self-test <steps>     scripted share-code round trip (see run())
//
// Both imply --read-only: nothing is written to game folders, no game is
// launched and Steam is never contacted.

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

class QQmlApplicationEngine;
class QQuickWindow;

namespace pmm::app {

class Backend;

struct VerificationOptions {
    QString screenshot;
    int screenshotDelayMs = 1500;
    QString theme;    // light | dark
    QStringList open; // export | add-mods | settings | history | locate | import:<code file>
    int selectRow = -1;
    QString statusFilter;
    QString search;
    // Comma-separated steps: "config" (playset from the game's current
    // configuration), "disable:<row>" and "move:<from>:<to>" (edits through
    // the mod list model), "export:<file>" (write the active playset's code),
    // "import:<file>" (preview and import a code as a new playset),
    // "dump:<file>" (write the active playset's entries as JSON).
    QString selfTest;

    bool active() const { return !screenshot.isEmpty() || !selfTest.isEmpty(); }
};

class Verification : public QObject {
    Q_OBJECT

public:
    Verification(QQmlApplicationEngine& engine, Backend& backend, VerificationOptions options,
                 QObject* parent = nullptr);

    // Starts once the backend finished its initial refresh.
    void start();

private:
    using Step = std::function<void(std::function<void()> next)>;

    void runSteps();
    void fail(const QString& message);
    void finish();
    QQuickWindow* window() const;
    void callWindow(const char* method, const QString& argument = {});
    void callWindow(const char* method, int argument);
    std::vector<Step> selfTestSteps();
    std::vector<Step> screenshotSteps();
    void waitFor(std::function<bool()> condition, std::function<void()> then, int timeoutMs,
                 const QString& what);

    QQmlApplicationEngine& m_engine;
    Backend& m_backend;
    VerificationOptions m_options;
    std::vector<Step> m_steps;
    std::size_t m_next = 0;
    bool m_failed = false;
};

} // namespace pmm::app
