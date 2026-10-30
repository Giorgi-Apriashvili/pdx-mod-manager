#pragma once

// Backend.updates (docs/updates.md, docs/ui-contract.md): "Check for updates" in
// Settings. Nothing touches the network until the user asks. An installed copy
// downloads the verified installer, starts it and quits; every other build (ZIP,
// development) offers the release page instead.
// Sessions that must not change anything (--read-only, verification runs) have
// the whole feature off, with the reason in disabledReason.

#include "app/core/update_client.hpp"
#include "platform/process.hpp"

#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

namespace pmm::app {

struct UpdateControllerOptions {
    QString currentVersion;     // empty: the version of this build
    QString disabledReason;     // not empty: the feature is off, and this says why
    UpdateClientOptions client; // endpoints and limits; the defaults are the real ones
    QString appDir;             // empty: the folder of this executable
    // Test seams; the defaults decide by platform and layout, start the process
    // for real, quit the application and open the browser.
    std::optional<bool> installed;
    std::function<Result<platform::StartedProcess>(const platform::ProcessSpec&)> startProcess;
    std::function<void()> requestQuit;
    std::function<void(const QUrl&)> openUrl;
    // Answers with why an update cannot install right now, empty when it can.
    // May answer later; the Backend asks the services worker.
    std::function<void(std::function<void(const QString&)>)> installBlocker;
};

class UpdateController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY stateChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY stateChanged)
    Q_PROPERTY(bool canCheck READ canCheck NOTIFY stateChanged)
    Q_PROPERTY(QString disabledReason READ disabledReason CONSTANT)

public:
    explicit UpdateController(UpdateControllerOptions options, QObject* parent = nullptr);
    ~UpdateController() override;

    // "idle", "checking", "upToDate", "noRelease", "available", "downloading",
    // "ready", "installing" or "failed".
    QString state() const;
    QString currentVersion() const { return m_currentVersion; }
    QString latestVersion() const;
    qreal progress() const { return m_progress; } // 0..1, -1 while the size is unknown
    QString errorMessage() const { return m_errorMessage; }
    QString mode() const; // "installer" or "releasePage"
    bool canCheck() const;
    QString disabledReason() const { return m_options.disabledReason; }

    Q_INVOKABLE void check();
    // Installer mode: download, verify, start the installer, quit. Otherwise
    // the same as openReleasePage().
    Q_INVOKABLE void startUpdate();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openReleasePage();
    // The step that failed, again.
    Q_INVOKABLE void retry();

    // The updates folder is known once the profile is open. Also removes what
    // an earlier update left behind.
    void setUpdatesDir(const QString& dir);
    void setInstallBlocker(std::function<void(std::function<void(const QString&)>)> blocker) {
        m_options.installBlocker = std::move(blocker);
    }

signals:
    void stateChanged();
    void progressChanged();

private:
    enum class State {
        Idle,
        Checking,
        UpToDate,
        NoRelease,
        Available,
        Downloading,
        Ready,
        Installing,
        Failed
    };

    void setState(State state, const QString& error = {});
    void setProgress(qreal progress);
    bool enabled() const { return m_options.disabledReason.isEmpty(); }
    bool canInstall() const;
    void whenUnblocked(std::function<void()> next);
    void onCheckFinished(const UpdateCheckResult& result);
    void onDownloadFinished(const UpdateDownloadResult& result);
    void beginDownload();
    void install();

    UpdateControllerOptions m_options;
    QString m_currentVersion;
    std::optional<services::AppVersion> m_current;
    UpdateClient m_client;
    bool m_installed = false;
    State m_state = State::Idle;
    QString m_errorMessage;
    qreal m_progress = 0;
    std::optional<services::AppVersion> m_latest;   // what the last check saw
    std::optional<services::ReleaseInfo> m_release; // a newer release to offer
    std::optional<DownloadedInstaller> m_installer;
    bool m_askingBlocker = false;
};

} // namespace pmm::app
