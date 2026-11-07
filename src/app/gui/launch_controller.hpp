#pragma once

// Play and configuration recovery. canPlay / blockers / warnings come from
// LaunchService::check() on the services worker and are refreshed whenever
// the playset, the inventory or the downloads change, and when the window is
// activated (a game or the Paradox launcher may have been started meanwhile).
// While Steam is not running, or a game or launcher is, Backend also checks
// the processes again every few seconds, so Play follows them on its own.
// The blockers always say why Play is unavailable: game not installed or not
// located, enabled mods not ready, a running game or launcher, Steam not
// running for a Steam installation, an interrupted configuration change.
// Without an active playset Play starts the game without mods, and a warning
// says so.
//
// play() moves through validating -> applying -> starting -> started (or
// failed); the configuration is written by LaunchService as one file
// transaction and the game executable is started directly. The state goes
// back to idle from started once the game was seen running and has exited,
// and from failed once Play, blocked after the failure, is available again.
// startedWithPlayset says whether the last started launch used a playset (the
// selection may change while the game runs). A launch parks the game's
// Workshop downloads (and resumes them if it fails), so the downloads view is
// refreshed after every outcome.
//
// recoverInterrupted() recovers every interrupted configuration change (any
// game). Those it cannot recover because a backup they need is missing or
// damaged (IntegrityCheckFailed: retrying cannot help) are remembered, and
// canDiscardInterrupted offers discardInterrupted() for exactly those, with
// their files and backup folders for the confirmation. The offer ends when the
// game changes, after a successful recovery or discard, and when a refresh
// shows no interrupted change any more. Recovery that fails for another
// reason (a locked file, say) offers Recover again only.

#include "app/core/view_data.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <vector>

namespace pmm::app {

class Backend;

class LaunchController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY changed)
    Q_PROPERTY(QStringList blockers READ blockers NOTIFY changed)
    Q_PROPERTY(QStringList warnings READ warnings NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString lastErrorDetail READ lastErrorDetail NOTIFY stateChanged)
    Q_PROPERTY(bool startedWithPlayset READ startedWithPlayset NOTIFY stateChanged)
    Q_PROPERTY(bool hasInterruptedTransaction READ hasInterruptedTransaction NOTIFY changed)
    Q_PROPERTY(bool canRestoreBackup READ canRestoreBackup NOTIFY changed)
    Q_PROPERTY(bool canDiscardInterrupted READ canDiscardInterrupted NOTIFY changed)
    Q_PROPERTY(QStringList discardableFiles READ discardableFiles NOTIFY changed)
    Q_PROPERTY(QStringList discardFolders READ discardFolders NOTIFY changed)

public:
    explicit LaunchController(Backend* backend, QObject* parent = nullptr);

    bool canPlay() const { return m_data.canPlay && !m_busy; }
    QStringList blockers() const { return m_data.blockers; }
    QStringList warnings() const { return m_data.warnings; }
    bool busy() const { return m_busy; }
    QString state() const { return m_state; }
    QString lastError() const { return m_lastError; }
    QString lastErrorDetail() const { return m_lastErrorDetail; }
    bool startedWithPlayset() const { return m_startedWithPlayset; }
    bool hasInterruptedTransaction() const { return m_data.interrupted; }
    bool canRestoreBackup() const { return m_data.canRestoreBackup; }
    bool canDiscardInterrupted() const { return !m_discardable.empty(); }
    // Display paths: the files the changes to discard wrote or were writing,
    // and the folders that keep their records and backups.
    QStringList discardableFiles() const;
    QStringList discardFolders() const;
    // The last verdict applied (C++ only).
    const LaunchData& data() const { return m_data; }

    Q_INVOKABLE void play();
    Q_INVOKABLE bool recoverInterrupted();
    // Discards the changes Recover could not undo (canDiscardInterrupted);
    // their files stay as they are now.
    Q_INVOKABLE bool discardInterrupted();
    Q_INVOKABLE bool restoreLastBackup();
    // Last generated report; copyDiagnostics() regenerates it.
    Q_INVOKABLE QString diagnosticsText() const { return m_diagnostics; }
    Q_INVOKABLE void copyDiagnostics();

    void apply(LaunchData data);
    // The worker is about to create the game process.
    void processStarting();

signals:
    void changed();
    void stateChanged();
    void launched(int pid);
    void launchFailed(const QString& message);
    void diagnosticsReady(const QString& text);

private:
    // An interrupted change that recovery cannot undo.
    struct Unrecoverable {
        QString id;
        QStringList files; // display paths
        QString folder;    // the transaction folder: journal and backups
        QString error;     // why recovery failed
    };

    void setState(const QString& state, bool busy);

    Backend* m_backend;
    LaunchData m_data;
    QString m_state = QStringLiteral("idle");
    bool m_busy = false;
    QString m_lastError;
    QString m_lastErrorDetail;
    bool m_startedWithPlayset = false;
    QString m_diagnostics;
    std::vector<Unrecoverable> m_discardable;
};

} // namespace pmm::app
