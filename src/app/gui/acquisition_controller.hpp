#pragma once

// Workshop downloads of the current game: the job list, provider status and
// the recovery actions (retry, cancel, the clearly labelled manual fallback
// page, rescan), and the updates of installed Workshop mods: "Check for mod
// updates" (the Steam Web API, only when the user asks), "Update mods",
// "Update from Workshop" and "Re-download from scratch". Everything runs
// through the coordinator on the services worker; with Steam disabled for the
// session (--no-steam, read-only mode) the actions explain that and do
// nothing, and nothing is sent.
//
// The result of a check is kept per playset for the session and shown for
// the active playset. After a reload of the playset (a rescan, a finished
// job) it is rebuilt from what the coordinator knows, without asking again.

#include "app/core/view_data.hpp"
#include "app/gui/jobs_model.hpp"

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>

namespace pmm::app {

class Backend;

class AcquisitionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(pmm::app::JobsModel* jobs READ jobs CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString providerStatus READ providerStatus NOTIFY changed)
    Q_PROPERTY(QString providerMessage READ providerMessage NOTIFY changed)
    // False when the session runs without Steam (--no-steam, read-only mode).
    Q_PROPERTY(bool downloadsEnabled READ downloadsEnabled CONSTANT)
    // "Check for mod updates" runs for the active playset.
    Q_PROPERTY(bool checkingUpdates READ checkingUpdates NOTIFY changed)
    // The result line of the active playset's last check ("" before one).
    Q_PROPERTY(QString updateCheckSummary READ updateCheckSummary NOTIFY changed)
    // That check could not reach the Steam Workshop for every mod: offer Retry.
    Q_PROPERTY(bool updateCheckFailed READ updateCheckFailed NOTIFY changed)

public:
    explicit AcquisitionController(Backend* backend, QObject* parent = nullptr);

    JobsModel* jobs() { return &m_jobs; }
    bool active() const { return m_data.active; }
    QString summary() const { return m_data.summary; }
    QString providerStatus() const { return m_data.providerStatus; }
    QString providerMessage() const { return m_data.providerMessage; }
    bool downloadsEnabled() const;
    bool checkingUpdates() const;
    QString updateCheckSummary() const;
    bool updateCheckFailed() const;

    Q_INVOKABLE void acquireMissing();
    Q_INVOKABLE void retry(int jobId);
    Q_INVOKABLE void retryAllFailed();
    Q_INVOKABLE void cancelAll();
    Q_INVOKABLE void openFallback(int jobId);
    Q_INVOKABLE void rescanNow();
    // "Check for mod updates": asks the Steam Web API about the installed
    // Workshop mods of the active playset (ids only). Ignored while a check runs.
    Q_INVOKABLE void checkForUpdates();
    // "Update mods": every mod of the active playset with a known update,
    // enabled and disabled.
    Q_INVOKABLE void updateMods();
    // "Update from Workshop" on one row.
    Q_INVOKABLE void updateMod(const QString& providerId);
    // "Re-download from scratch" on one row, after the confirmation.
    Q_INVOKABLE void reinstallMod(const QString& providerId);
    // The same for the mod of a failed update job (JobsModel canReinstall).
    Q_INVOKABLE void reinstallJob(int jobId);

    void apply(AcquisitionData data);
    // The active playset was (re)loaded: rebuilds its check result.
    void refreshUpdateCheck();

    // steam://url/CommunityFilePage/<digits>, or an https://steamcommunity.com/ page.
    static bool isTrustedFallbackUrl(const QString& url);

signals:
    void changed();
    // A check ended (also when it failed): the result line was updated.
    void updateCheckFinished();

private:
    bool refuseWithoutSteam();
    void runUpdate(const QString& failureTitle,
                   std::function<Result<services::CoordinatorUpdate>(AppRuntime&)> op,
                   const QString& successTitle = {}, const QString& successText = {});
    // For acquireUpdates(), reinstall() and reinstallJob(): their message as
    // a notification titled `successTitle` (a warning when mods were skipped).
    void runUpdateOutcome(const QString& failureTitle, const QString& successTitle,
                          std::function<Result<services::UpdateOutcome>(AppRuntime&)> op);
    const UpdateCheckData* activeCheck() const;

    Backend* m_backend;
    JobsModel m_jobs;
    AcquisitionData m_data;
    // The session's check results by playset id.
    std::map<std::int64_t, UpdateCheckData> m_checks;
    std::int64_t m_checkingPlayset = -1; // -1: no check runs
    // Answers of an older check or rebuild are dropped.
    quint64 m_checkGeneration = 0;
};

} // namespace pmm::app
