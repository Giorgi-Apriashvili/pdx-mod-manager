#pragma once

// Play and configuration recovery. canPlay / blockers / warnings come from
// LaunchService::check() on the services worker and are refreshed whenever
// the playset, the inventory or the downloads change, and when the window is
// activated (a game or the Paradox launcher may have been started meanwhile).
// The blockers always say why Play is unavailable: game not installed or not
// located, no active playset, enabled mods not ready, a running game or
// launcher, an interrupted configuration change.
//
// play() moves through validating -> applying -> starting -> started (or
// failed); the configuration is written by LaunchService as one file
// transaction and the game executable is started directly.

#include "app/core/view_data.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

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
    Q_PROPERTY(bool hasInterruptedTransaction READ hasInterruptedTransaction NOTIFY changed)
    Q_PROPERTY(bool canRestoreBackup READ canRestoreBackup NOTIFY changed)

public:
    explicit LaunchController(Backend* backend, QObject* parent = nullptr);

    bool canPlay() const { return m_data.canPlay && !m_busy; }
    QStringList blockers() const { return m_data.blockers; }
    QStringList warnings() const { return m_data.warnings; }
    bool busy() const { return m_busy; }
    QString state() const { return m_state; }
    QString lastError() const { return m_lastError; }
    QString lastErrorDetail() const { return m_lastErrorDetail; }
    bool hasInterruptedTransaction() const { return m_data.interrupted; }
    bool canRestoreBackup() const { return m_data.canRestoreBackup; }

    Q_INVOKABLE void play();
    Q_INVOKABLE bool recoverInterrupted();
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
    void setState(const QString& state, bool busy);

    Backend* m_backend;
    LaunchData m_data;
    QString m_state = QStringLiteral("idle");
    bool m_busy = false;
    QString m_lastError;
    QString m_lastErrorDetail;
    QString m_diagnostics;
};

} // namespace pmm::app
