#include "app/gui/update_controller.hpp"

#include "app/core/qt_util.hpp"
#include "core/log.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>

#include <cmath>
#include <utility>

namespace pmm::app {
namespace {

constexpr const char* kComponent = "app.update";
constexpr qreal kProgressStep = 0.005; // no property update per network chunk

UpdateControllerOptions withDefaults(UpdateControllerOptions options) {
    if (options.currentVersion.isEmpty())
        options.currentVersion = QStringLiteral(PMM_APP_VERSION);
    // GitHub's API requires a User-Agent.
    options.client.userAgent = QStringLiteral("ParadoxModManager/") + options.currentVersion;
    if (options.appDir.isEmpty())
        options.appDir = QCoreApplication::applicationDirPath();
    if (!options.startProcess)
        options.startProcess = platform::startDetached;
    if (!options.requestQuit)
        options.requestQuit = [] { QCoreApplication::quit(); };
    if (!options.openUrl)
        options.openUrl = [](const QUrl& url) { QDesktopServices::openUrl(url); };
    return options;
}

bool detectInstalled(const QString& appDir) {
#if defined(_WIN32)
    return services::hasInstallerLayout(toPath(appDir));
#else
    (void)appDir;
    return false;
#endif
}

} // namespace

UpdateController::UpdateController(UpdateControllerOptions options, QObject* parent)
    : QObject(parent), m_options(withDefaults(std::move(options))),
      m_currentVersion(m_options.currentVersion),
      m_current(services::parseAppVersion(utf8(m_currentVersion))), m_client(m_options.client) {
    m_installed = m_options.installed ? *m_options.installed : detectInstalled(m_options.appDir);
    if (enabled() && !m_current) {
        // Development builds with an unusual version number have nothing to compare.
        m_options.disabledReason = tr("This build has no release version to compare.");
    }
    connect(&m_client, &UpdateClient::checkFinished, this, &UpdateController::onCheckFinished);
    connect(&m_client, &UpdateClient::downloadFinished, this,
            &UpdateController::onDownloadFinished);
    connect(&m_client, &UpdateClient::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                const qreal value =
                    total > 0 ? qMin(1.0, static_cast<qreal>(received) / static_cast<qreal>(total))
                              : -1.0;
                if (std::abs(value - m_progress) >= kProgressStep || value >= 1.0 || value < 0)
                    setProgress(value);
            });
}

UpdateController::~UpdateController() = default;

QString UpdateController::state() const {
    switch (m_state) {
    case State::Idle:
        return QStringLiteral("idle");
    case State::Checking:
        return QStringLiteral("checking");
    case State::UpToDate:
        return QStringLiteral("upToDate");
    case State::NoRelease:
        return QStringLiteral("noRelease");
    case State::Available:
        return QStringLiteral("available");
    case State::Downloading:
        return QStringLiteral("downloading");
    case State::Ready:
        return QStringLiteral("ready");
    case State::Installing:
        return QStringLiteral("installing");
    case State::Failed:
        return QStringLiteral("failed");
    }
    return {};
}

QString UpdateController::latestVersion() const {
    return m_latest ? qs(m_latest->toString()) : QString();
}

QString UpdateController::mode() const {
    return canInstall() ? QStringLiteral("installer") : QStringLiteral("releasePage");
}

// A release without the installer or its checksums has only the release page.
bool UpdateController::canInstall() const {
    return m_installed && (!m_release || (m_release->installer && m_release->checksums));
}

bool UpdateController::canCheck() const {
    return enabled() && m_state != State::Checking && m_state != State::Downloading &&
           m_state != State::Installing;
}

void UpdateController::setState(State state, const QString& error) {
    if (m_state == state && m_errorMessage == error) {
        emit stateChanged(); // the release behind the same state may have changed
        return;
    }
    m_state = state;
    m_errorMessage = error;
    emit stateChanged();
}

void UpdateController::setProgress(qreal progress) {
    if (qFuzzyCompare(m_progress, progress))
        return;
    m_progress = progress;
    emit progressChanged();
}

void UpdateController::setUpdatesDir(const QString& dir) {
    m_client.setUpdatesDir(dir);
    if (enabled())
        m_client.removeDownloads();
}

void UpdateController::check() {
    if (!canCheck())
        return;
    m_latest.reset();
    m_release.reset();
    m_installer.reset();
    setState(State::Checking);
    m_client.check();
}

void UpdateController::onCheckFinished(const UpdateCheckResult& result) {
    if (!result) {
        setState(State::Failed, qs(result.error().message));
        return;
    }
    if (!result.value()) {
        setState(State::NoRelease);
        return;
    }
    const services::ReleaseInfo& release = *result.value();
    m_latest = release.version;
    if (release.version > *m_current) {
        m_release = release;
        logInfo(kComponent, "a newer release is available",
                {{"version", release.version.toString()}});
        setState(State::Available);
    } else {
        setState(State::UpToDate);
    }
}

void UpdateController::startUpdate() {
    if (!enabled() || !m_release || m_askingBlocker || m_state == State::Checking ||
        m_state == State::Downloading || m_state == State::Installing)
        return;
    if (!canInstall()) {
        openReleasePage();
        return;
    }
    whenUnblocked([this] {
        if (m_installer && QFileInfo::exists(m_installer->path))
            install();
        else
            beginDownload();
    });
}

// Only the state the request was made in may continue: a new check or a
// cancelled download took over otherwise.
void UpdateController::whenUnblocked(std::function<void()> next) {
    if (!m_options.installBlocker) {
        next();
        return;
    }
    m_askingBlocker = true;
    const State asked = m_state;
    const std::optional<services::AppVersion> release =
        m_release ? std::optional(m_release->version) : std::nullopt;
    m_options.installBlocker([this, asked, release, next = std::move(next)](const QString& reason) {
        m_askingBlocker = false;
        if (m_state != asked || !m_release || m_release->version != release)
            return;
        if (reason.isEmpty()) {
            next();
            return;
        }
        logInfo(kComponent, "update held back", {{"reason", utf8(reason)}});
        // A failed step stays failed; otherwise the offer stays with the reason.
        setState(m_state, reason);
    });
}

void UpdateController::beginDownload() {
    m_installer.reset();
    setProgress(0);
    setState(State::Downloading);
    m_client.download(*m_release);
}

void UpdateController::onDownloadFinished(const UpdateDownloadResult& result) {
    if (!result) {
        setState(State::Failed, qs(result.error().message));
        return;
    }
    m_installer = result.value();
    setProgress(1);
    setState(State::Ready);
    whenUnblocked([this] { install(); });
}

void UpdateController::install() {
    if (!m_installer)
        return;
    // It has been sitting in a folder of this user since it was verified.
    if (!UpdateClient::fileHasDigest(m_installer->path, m_installer->sha256)) {
        m_client.removeDownloads();
        m_installer.reset();
        setState(State::Failed, tr("The downloaded installer changed on disk, so it was deleted. "
                                   "Try again."));
        return;
    }
    platform::ProcessSpec spec;
    spec.executable = toPath(m_installer->path);
    spec.arguments = services::updateInstallerArguments();
    setState(State::Installing);
    const auto started = m_options.startProcess(spec);
    if (!started) {
        logWarning(kComponent, "the installer could not be started",
                   {{"error", started.error().message}, {"detail", started.error().detail}});
        setState(State::Failed,
                 tr("The installer could not be started. %1").arg(qs(started.error().message)));
        return;
    }
    logInfo(kComponent, "installer started, quitting",
            {{"pid", std::to_string(started.value().pid)}});
    m_options.requestQuit();
}

void UpdateController::cancel() {
    if (m_state == State::Checking) {
        m_client.cancel();
        setState(State::Idle);
    } else if (m_state == State::Downloading) {
        m_client.cancel();
        setProgress(0);
        setState(State::Available);
    }
}

void UpdateController::openReleasePage() {
    if (enabled())
        m_options.openUrl(QUrl(qs(services::releasePageUrl(m_latest))));
}

void UpdateController::retry() {
    if (m_state != State::Failed)
        return;
    if (m_release && canInstall())
        startUpdate();
    else
        check();
}

} // namespace pmm::app
