#pragma once

// Backend.updates (docs/updates.md, docs/ui-contract.md): "Check for updates" in
// Settings, and the same check once a day after start-up while the setting is on.
// Nothing is downloaded until the user asks. An installed copy downloads the
// verified installer, starts it and quits; every other build (ZIP, development)
// offers the release page instead, and so does a session that the installer's
// relaunch would not continue (--profile, --no-steam).
// Sessions that must not change anything (--read-only, verification runs) have
// the whole feature off, with the reason in disabledReason.

#include "app/core/update_client.hpp"
#include "app/gui/app_settings.hpp"
#include "platform/process.hpp"

#include <QDate>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace pmm::app {

struct UpdateControllerOptions {
    QString currentVersion;     // empty: the version of this build
    QString disabledReason;     // not empty: the feature is off, and this says why
    UpdateClientOptions client; // endpoints and limits; the defaults are the real ones
    QString appDir;             // empty: the folder of this executable
    // Not empty: this session offers the release page instead of installing, and
    // an installed copy says why (sessionInstallBlocker()).
    QString installUnavailableReason;
    // The daily check. Off unless main() turns it on, so that no test reaches
    // GitHub by accident.
    bool dailyCheck = false;
    std::chrono::milliseconds dailyCheckDelay{10000}; // after the start-up refresh
    std::function<QDate()> today;                     // empty: the local calendar date
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
    Q_PROPERTY(QString installUnavailableReason READ installUnavailableReason NOTIFY stateChanged)
    Q_PROPERTY(bool checkedAutomatically READ checkedAutomatically NOTIFY stateChanged)

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
    // Why this installed copy offers the release page in this session; empty
    // when it can install, or when it never could (ZIP, development build, a
    // release without the installer).
    QString installUnavailableReason() const;
    // The last check was the daily one (what the main window's banner offers).
    bool checkedAutomatically() const { return m_automatic; }

    // The reason for installUnavailableReason, empty when the session can install.
    // The installer relaunches the application without arguments.
    static QString sessionInstallBlocker(bool customProfile, bool noSteam);

    Q_INVOKABLE void check();
    // Installer mode: download, verify, start the installer, quit. Otherwise
    // the same as openReleasePage().
    Q_INVOKABLE void startUpdate();
    Q_INVOKABLE void cancel();
    // In the browser: the page of the version the last check saw, or the list
    // of releases, in the repository a reply named (docs/updates.md). The
    // compiled slug's list only while no reply has named one.
    Q_INVOKABLE void openReleasePage();
    // The step that failed, again.
    Q_INVOKABLE void retry();

    // The updates folder is known once the profile is open. Also removes what
    // an earlier update left behind.
    void setUpdatesDir(const QString& dir);
    void setInstallBlocker(std::function<void(std::function<void(const QString&)>)> blocker) {
        m_options.installBlocker = std::move(blocker);
    }
    // The daily check's switch and the date of its last answer.
    void setSettings(AppSettings* settings) { m_settings = settings; }
    // Once the start-up refresh has finished. After dailyCheckDelay the daily
    // check runs the same request as check(), if the setting is on, the state is
    // still "idle" and no daily check got an answer on this date. Its failures
    // are only logged; nothing is downloaded without startUpdate().
    void scheduleDailyCheck();

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
    void startCheck(bool automatic);
    void runDailyCheck();
    QDate today() const;
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
    std::string m_repository; // as the last reply named it; kept for the session
    std::optional<DownloadedInstaller> m_installer;
    bool m_askingBlocker = false;
    bool m_automatic = false; // the last check was the daily one
    QPointer<AppSettings> m_settings;
    QTimer m_dailyTimer;
};

} // namespace pmm::app
