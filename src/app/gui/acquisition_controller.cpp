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

#include <optional>

namespace pmm::app {
namespace {

bool steamUrlHandlerRegistered() {
    // Without a registered handler Windows would show an "open with" prompt.
    const QSettings steam(QStringLiteral("HKEY_CLASSES_ROOT\\steam"), QSettings::NativeFormat);
    return steam.contains(QStringLiteral("URL Protocol"));
}

} // namespace

AcquisitionController::AcquisitionController(Backend* backend, QObject* parent)
    : QObject(parent), m_backend(backend) {
    connect(backend, &Backend::contextLoaded, this, &AcquisitionController::refreshUpdateCheck);
}

bool AcquisitionController::downloadsEnabled() const {
    return m_backend->steamEnabled();
}

const UpdateCheckData* AcquisitionController::activeCheck() const {
    const auto found = m_checks.find(m_backend->activePlaysetId());
    return found == m_checks.end() ? nullptr : &found->second;
}

bool AcquisitionController::checkingUpdates() const {
    return m_checkingPlayset >= 0 && m_checkingPlayset == m_backend->activePlaysetId();
}

QString AcquisitionController::updateCheckSummary() const {
    const UpdateCheckData* check = activeCheck();
    return check ? check->summary : QString();
}

bool AcquisitionController::updateCheckFailed() const {
    const UpdateCheckData* check = activeCheck();
    return check && (!check->reachable || check->unanswered > 0);
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

void AcquisitionController::runUpdateOutcome(
    const QString& failureTitle, const QString& successTitle,
    std::function<Result<services::UpdateOutcome>(AppRuntime&)> op) {
    ServiceRunner* runner = &m_backend->runner();
    struct Outcome {
        std::optional<Error> error;
        QString message;
        bool queued = false;
        bool skipped = false;
    };
    runner->call(
        this,
        [op = std::move(op), runner](AppRuntime& runtime) {
            Outcome outcome;
            auto result = op(runtime);
            if (!result) {
                outcome.error = result.error();
                return outcome;
            }
            runner->report(result.value().update);
            outcome.message = qs(result.value().message);
            outcome.queued = !result.value().jobIds.empty();
            outcome.skipped = !result.value().skipped.empty();
            return outcome;
        },
        [this, failureTitle, successTitle](const Outcome& outcome) {
            NotificationCenter* notifications = m_backend->notifications();
            if (outcome.error)
                notifications->warning(failureTitle, qs(outcome.error->message));
            else if (outcome.queued && !outcome.skipped)
                notifications->info(successTitle, outcome.message);
            else if (outcome.queued)
                notifications->warning(successTitle, outcome.message);
            else if (outcome.skipped)
                notifications->warning(tr("Nothing was updated"), outcome.message);
            else
                notifications->info(tr("Nothing to update"), outcome.message);
            // The new jobs block Play: the launch check follows at once.
            m_backend->refreshAcquisition(true);
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

void AcquisitionController::checkForUpdates() {
    if (refuseWithoutSteam())
        return;
    const std::int64_t playsetId = m_backend->activePlaysetId();
    if (playsetId < 0 || m_checkingPlayset >= 0)
        return;
    m_checkingPlayset = playsetId;
    ++m_checkGeneration; // a rebuild still on its way is older than this check
    emit changed();
    m_backend->runner().checkWorkshopUpdates(
        this, playsetId, [this, playsetId](WorkshopUpdateCheck check) {
            m_checkingPlayset = -1;
            ++m_checkGeneration;
            if (!check.available) {
                // No Web API host in this session (or shutting down): nothing was sent.
                m_backend->notifications()->warning(
                    tr("Could not check for mod updates"),
                    tr("The Steam Workshop cannot be asked in this session, so nothing was "
                       "checked."));
            } else if (check.error) {
                m_backend->notifications()->warning(tr("Could not check for mod updates"),
                                                    qs(check.error->message));
            } else {
                m_checks[playsetId] = queries::updateCheckData(check.report);
            }
            emit changed();
            emit updateCheckFinished();
            // The rows, the counts and the launch check's warning follow the answers.
            m_backend->refreshAcquisition(true);
        });
}

void AcquisitionController::refreshUpdateCheck() {
    const std::int64_t playsetId = m_backend->activePlaysetId();
    const auto found = m_checks.find(playsetId);
    // A failed check keeps its own words; a running one brings a new result.
    if (found == m_checks.end() || !found->second.reachable || m_checkingPlayset >= 0 ||
        !m_backend->steamEnabled())
        return;
    const quint64 generation = ++m_checkGeneration;
    m_backend->runner().call(
        this,
        [playsetId](AppRuntime& runtime) -> std::optional<UpdateCheckData> {
            if (!runtime.acquisition())
                return std::nullopt;
            auto report = runtime.acquisition()->updateCheckReport(playsetId);
            if (!report)
                return std::nullopt;
            return queries::updateCheckData(report.value());
        },
        [this, playsetId, generation](std::optional<UpdateCheckData> data) {
            if (generation != m_checkGeneration || !data)
                return;
            m_checks[playsetId] = std::move(*data);
            emit changed();
        });
}

void AcquisitionController::updateMods() {
    if (refuseWithoutSteam())
        return;
    const std::int64_t playsetId = m_backend->activePlaysetId();
    if (playsetId < 0)
        return;
    runUpdateOutcome(tr("Could not update the mods"), tr("Updates requested"),
                     [playsetId](AppRuntime& runtime) -> Result<services::UpdateOutcome> {
                         if (!runtime.acquisition())
                             return makeError(ErrorCode::ProviderUnavailable,
                                              "Workshop downloads are off.");
                         return runtime.acquisition()->acquireUpdates(playsetId);
                     });
}

void AcquisitionController::updateMod(const QString& providerId) {
    if (refuseWithoutSteam())
        return;
    const std::int64_t playsetId = m_backend->activePlaysetId();
    if (playsetId < 0 || providerId.isEmpty())
        return;
    runUpdateOutcome(
        tr("Could not update the mod"), tr("Update requested"),
        [playsetId, id = utf8(providerId)](AppRuntime& runtime) -> Result<services::UpdateOutcome> {
            if (!runtime.acquisition())
                return makeError(ErrorCode::ProviderUnavailable, "Workshop downloads are off.");
            return runtime.acquisition()->acquireUpdates(playsetId, {id});
        });
}

void AcquisitionController::reinstallMod(const QString& providerId) {
    if (refuseWithoutSteam())
        return;
    const std::int64_t playsetId = m_backend->activePlaysetId();
    if (playsetId < 0 || providerId.isEmpty())
        return;
    runUpdateOutcome(
        tr("Could not download the mod again"), tr("Downloading again"),
        [playsetId, id = utf8(providerId)](AppRuntime& runtime) -> Result<services::UpdateOutcome> {
            if (!runtime.acquisition())
                return makeError(ErrorCode::ProviderUnavailable, "Workshop downloads are off.");
            return runtime.acquisition()->reinstall(playsetId, id);
        });
}

void AcquisitionController::reinstallJob(int jobId) {
    if (refuseWithoutSteam())
        return;
    runUpdateOutcome(tr("Could not download the mod again"), tr("Downloading again"),
                     [jobId](AppRuntime& runtime) -> Result<services::UpdateOutcome> {
                         if (!runtime.acquisition())
                             return makeError(ErrorCode::ProviderUnavailable,
                                              "Workshop downloads are off.");
                         return runtime.acquisition()->reinstallJob(jobId);
                     });
}

} // namespace pmm::app
