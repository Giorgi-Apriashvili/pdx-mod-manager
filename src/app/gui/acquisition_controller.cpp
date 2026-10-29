#include "app/gui/acquisition_controller.hpp"

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "app/gui/mod_details.hpp"
#include "app/gui/notification_center.hpp"
#include "providers/fallback.hpp"

#include <QDesktopServices>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>

namespace pmm::app {
namespace {

bool steamUrlHandlerRegistered() {
#if defined(Q_OS_WIN)
    // Without a registered handler Windows would show an "open with" prompt.
    const QSettings steam(QStringLiteral("HKEY_CLASSES_ROOT\\steam"), QSettings::NativeFormat);
    return steam.contains(QStringLiteral("URL Protocol"));
#else
    return true; // openUrl() fails without a handler; the web page is the fallback
#endif
}

} // namespace

AcquisitionController::AcquisitionController(Backend* backend, QObject* parent)
    : QObject(parent), m_backend(backend) {}

bool AcquisitionController::downloadsEnabled() const {
    return m_backend->steamEnabled();
}

bool AcquisitionController::isTrustedFallbackUrl(const QString& url) {
    static const QRegularExpression steamPage(
        QStringLiteral("^steam://url/CommunityFilePage/[0-9]{1,20}$"));
    return steamPage.match(url).hasMatch() || ModDetails::isTrustedWorkshopUrl(url);
}

void AcquisitionController::apply(AcquisitionData data) {
    m_jobs.setRows(std::move(data.jobs));
    m_data = std::move(data);
    emit changed();
}

bool AcquisitionController::refuseWithoutSteam() {
    if (m_backend->steamEnabled())
        return false;
    m_backend->notifications()->warning(
        tr("Workshop downloads are off"),
        tr("This session was started with --no-steam or in read-only mode, so nothing is "
           "subscribed or downloaded. Restart Paradox Mod Manager normally to download mods."));
    return true;
}

void AcquisitionController::runUpdate(
    const QString& failureTitle, std::function<Result<services::CoordinatorUpdate>(AppRuntime&)> op,
    const QString& successTitle, const QString& successText) {
    ServiceRunner* runner = &m_backend->runner();
    runner->call(
        this,
        [op = std::move(op), runner](AppRuntime& runtime) -> std::optional<Error> {
            auto update = op(runtime);
            if (!update)
                return update.error();
            runner->report(update.value());
            return std::nullopt;
        },
        [this, failureTitle, successTitle, successText](std::optional<Error> error) {
            if (error)
                m_backend->notifications()->warning(failureTitle, qs(error->message));
            else if (!successTitle.isEmpty())
                m_backend->notifications()->info(successTitle, successText);
            m_backend->refreshAcquisition();
        });
}

void AcquisitionController::acquireMissing() {
    if (refuseWithoutSteam())
        return;
    const std::int64_t playsetId = m_backend->activePlaysetId();
    if (playsetId < 0)
        return;
    runUpdate(
        tr("Could not queue the downloads"),
        [playsetId](AppRuntime& runtime) -> Result<services::CoordinatorUpdate> {
            if (!runtime.acquisition())
                return makeError(ErrorCode::ProviderUnavailable, "Workshop downloads are off.");
            return runtime.acquisition()->acquireMissing(playsetId);
        },
        tr("Downloads queued"),
        tr("Missing Workshop mods of this playset, including disabled ones, are subscribed and "
           "downloaded through Steam."));
}

void AcquisitionController::retry(int jobId) {
    if (refuseWithoutSteam())
        return;
    runUpdate(tr("Could not retry the download"),
              [jobId](AppRuntime& runtime) -> Result<services::CoordinatorUpdate> {
                  if (!runtime.acquisition())
                      return makeError(ErrorCode::ProviderUnavailable,
                                       "Workshop downloads are off.");
                  return runtime.acquisition()->retry(jobId);
              });
}

void AcquisitionController::retryAllFailed() {
    if (refuseWithoutSteam())
        return;
    const QString game = m_backend->currentGame();
    runUpdate(tr("Could not retry the downloads"),
              [game](AppRuntime& runtime) -> Result<services::CoordinatorUpdate> {
                  if (!runtime.acquisition())
                      return makeError(ErrorCode::ProviderUnavailable,
                                       "Workshop downloads are off.");
                  return runtime.acquisition()->retryAllFailed(utf8(game));
              });
}

void AcquisitionController::cancelAll() {
    if (refuseWithoutSteam())
        return;
    const QString game = m_backend->currentGame();
    ServiceRunner* runner = &m_backend->runner();
    runner->call(
        this,
        [game, runner](AppRuntime& runtime) -> QString {
            if (!runtime.acquisition())
                return {};
            services::CancelOutcome outcome = runtime.acquisition()->cancelAll(utf8(game));
            runner->report(outcome.update);
            return qs(outcome.message);
        },
        [this](const QString& message) {
            m_backend->notifications()->warning(tr("Downloads cancelled"), message);
            m_backend->refreshAcquisition();
        });
}

void AcquisitionController::openFallback(int jobId) {
    if (!m_backend->steamEnabled()) {
        refuseWithoutSteam();
        return;
    }
    ServiceRunner* runner = &m_backend->runner();
    struct Outcome {
        std::optional<Error> error;
        QString clientUrl;
        QString webUrl;
        QString label;
    };
    runner->call(
        this,
        [jobId, runner](AppRuntime& runtime) {
            Outcome outcome;
            if (!runtime.acquisition()) {
                outcome.error =
                    makeError(ErrorCode::ProviderUnavailable, "Workshop downloads are off.");
                return outcome;
            }
            auto fallback = runtime.acquisition()->openFallback(jobId);
            if (!fallback) {
                outcome.error = fallback.error();
                return outcome;
            }
            runner->report(fallback.value().update);
            outcome.clientUrl = qs(fallback.value().links.steamClientUrl);
            outcome.webUrl = qs(fallback.value().links.webUrl);
            outcome.label = qs(fallback.value().label);
            return outcome;
        },
        [this](Outcome outcome) {
            m_backend->refreshAcquisition();
            if (outcome.error) {
                m_backend->notifications()->warning(tr("Could not open the Workshop page"),
                                                    qs(outcome.error->message));
                return;
            }
            bool opened = false;
            if (steamUrlHandlerRegistered() && isTrustedFallbackUrl(outcome.clientUrl))
                opened = QDesktopServices::openUrl(QUrl(outcome.clientUrl));
            if (!opened && isTrustedFallbackUrl(outcome.webUrl))
                opened = QDesktopServices::openUrl(QUrl(outcome.webUrl));
            if (!opened) {
                m_backend->notifications()->error(
                    tr("Could not open the Workshop page"),
                    tr("Open %1 in a browser, subscribe there, then rescan.").arg(outcome.webUrl));
                return;
            }
            m_backend->notifications()->info(
                outcome.label,
                tr("Manual step: subscribe on the Workshop page. Nothing is downloaded for you; "
                   "the "
                   "manager picks the mod up when Steam has installed it (Rescan)."));
        });
}

void AcquisitionController::rescanNow() {
    m_backend->rescanCurrentMods();
}

} // namespace pmm::app
