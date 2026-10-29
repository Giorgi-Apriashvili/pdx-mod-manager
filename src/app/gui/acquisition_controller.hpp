#pragma once

// Workshop downloads of the current game: the job list, provider status and
// the recovery actions (retry, cancel, the clearly labelled manual fallback
// page, rescan). Everything runs through the coordinator on the services
// worker; with Steam disabled for the session (--no-steam, read-only mode)
// the actions explain that and do nothing.

#include "app/core/view_data.hpp"
#include "app/gui/jobs_model.hpp"

#include <QObject>
#include <QString>

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

public:
    explicit AcquisitionController(Backend* backend, QObject* parent = nullptr);

    JobsModel* jobs() { return &m_jobs; }
    bool active() const { return m_data.active; }
    QString summary() const { return m_data.summary; }
    QString providerStatus() const { return m_data.providerStatus; }
    QString providerMessage() const { return m_data.providerMessage; }
    bool downloadsEnabled() const;

    Q_INVOKABLE void acquireMissing();
    Q_INVOKABLE void retry(int jobId);
    Q_INVOKABLE void retryAllFailed();
    Q_INVOKABLE void cancelAll();
    Q_INVOKABLE void openFallback(int jobId);
    Q_INVOKABLE void rescanNow();

    void apply(AcquisitionData data);

    // steam://url/CommunityFilePage/<digits>, or an https://steamcommunity.com/ page.
    static bool isTrustedFallbackUrl(const QString& url);

signals:
    void changed();

private:
    bool refuseWithoutSteam();
    void runUpdate(const QString& failureTitle,
                   std::function<Result<services::CoordinatorUpdate>(AppRuntime&)> op,
                   const QString& successTitle = {}, const QString& successText = {});

    Backend* m_backend;
    JobsModel m_jobs;
    AcquisitionData m_data;
};

} // namespace pmm::app
