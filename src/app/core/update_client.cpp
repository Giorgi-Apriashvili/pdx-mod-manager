#include "app/core/update_client.hpp"

#include "app/core/qt_util.hpp"
#include "core/log.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

#include <span>
#include <utility>

namespace pmm::app {
namespace {

constexpr const char* kComponent = "app.update";
constexpr qint64 kChunkBytes = qint64{64} * 1024;
constexpr qint64 kReadBufferBytes = qint64{1024} * 1024;

int httpStatus(const QNetworkReply* reply) {
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

bool isRedirectStatus(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

std::span<const std::uint8_t> bytesOf(const QByteArray& data) {
    return {reinterpret_cast<const std::uint8_t*>(data.constData()),
            static_cast<std::size_t>(data.size())};
}

std::string_view viewOf(const QByteArray& data) {
    return {data.constData(), static_cast<std::size_t>(data.size())};
}

} // namespace

UpdateClient::UpdateClient(UpdateClientOptions options, QObject* parent)
    : QObject(parent), m_options(std::move(options)) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &UpdateClient::onTimeout);
}

UpdateClient::~UpdateClient() {
    cancel();
}

void UpdateClient::check() {
    if (busy())
        return;
    startPhase(Phase::Check, QUrl(qs(services::latestReleaseApiUrl(m_options.endpoints))));
}

void UpdateClient::download(const services::ReleaseInfo& release) {
    if (busy())
        return;
    if (!release.installer || !release.checksums) {
        emit downloadFinished(
            makeError(ErrorCode::NotFound, "This release has no installer with a checksum file."));
        return;
    }
    if (m_options.updatesDir.isEmpty() || !QDir().mkpath(m_options.updatesDir)) {
        emit downloadFinished(makeError(ErrorCode::IoError,
                                        "The folder for the downloaded installer could not be "
                                        "created. Check the free disk space and try again.",
                                        utf8(m_options.updatesDir)));
        return;
    }
    removeDownloads();
    m_installerAsset = *release.installer;
    m_expectedDigest.clear();
    startPhase(Phase::Checksums, QUrl(qs(release.checksums->url)));
}

void UpdateClient::cancel() {
    stopReply();
    cleanUpPartialFile();
    m_phase = Phase::Idle;
}

void UpdateClient::removeDownloads() const {
    if (m_options.updatesDir.isEmpty()) // QDir("") would be the working folder
        return;
    const QDir dir(m_options.updatesDir);
    const auto files = dir.entryInfoList(QDir::Files | QDir::Hidden | QDir::System);
    for (const QFileInfo& file : files) {
        // A file in use (an installer still finishing) goes on the next attempt.
        if (!QFile::remove(file.absoluteFilePath()))
            logDebug(kComponent, "an old download could not be removed yet",
                     {{"file", utf8(file.fileName())}});
    }
}

bool UpdateClient::fileHasDigest(const QString& path, const std::string& sha256) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    Sha256 hash;
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kChunkBytes);
        if (chunk.isEmpty())
            return false;
        hash.update(bytesOf(chunk));
    }
    return Sha256::toHex(hash.finish()) == sha256;
}

void UpdateClient::startPhase(Phase phase, const QUrl& url) {
    m_phase = phase;
    m_redirects = 0;
    m_abortError.reset();
    m_body.clear();
    m_received = 0;
    m_total = 0;
    m_timer.setInterval(phase == Phase::Installer ? m_options.stallTimeout
                                                  : m_options.checkTimeout);
    m_timer.start();
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    startHop(url);
}

void UpdateClient::startHop(const QUrl& url) {
    QNetworkRequest request(url);
    // Every hop goes through followRedirect() and the allow-list.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setHeader(QNetworkRequest::UserAgentHeader, m_options.userAgent);
    if (m_phase == Phase::Check)
        request.setRawHeader("Accept", "application/vnd.github+json");

    m_reply = m_network->get(request);
    m_reply->setReadBufferSize(kReadBufferBytes);
    connect(m_reply, &QNetworkReply::metaDataChanged, this, &UpdateClient::onMetaData);
    connect(m_reply, &QNetworkReply::readyRead, this, &UpdateClient::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateClient::onFinished);
    logDebug(kComponent, "update request", {{"host", utf8(url.host())}});
}

void UpdateClient::stopReply() {
    m_timer.stop();
    if (!m_reply)
        return;
    QNetworkReply* reply = std::exchange(m_reply, nullptr);
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
}

void UpdateClient::cleanUpPartialFile() {
    if (m_file) {
        m_file->cancelWriting();
        m_file.reset();
    }
}

void UpdateClient::abortWith(Error error) {
    if (!m_abortError)
        m_abortError = std::move(error);
    if (m_reply)
        m_reply->abort(); // finished() follows
}

void UpdateClient::onTimeout() {
    abortWith(makeError(ErrorCode::Timeout, m_phase == Phase::Installer
                                                ? "The download stalled. Check the internet "
                                                  "connection and try again."
                                                : "GitHub did not answer in time. Check the "
                                                  "internet connection and try again."));
}

void UpdateClient::onMetaData() {
    if (!m_reply || httpStatus(m_reply) != 200)
        return;
    const QVariant length = m_reply->header(QNetworkRequest::ContentLengthHeader);
    if (m_phase == Phase::Installer) {
        m_total = length.isValid() ? length.toLongLong() : 0;
        if (m_total > m_options.maxInstallerBytes)
            abortWith(makeError(ErrorCode::LimitExceeded,
                                "The installer is larger than this application accepts, so it was "
                                "not downloaded."));
    } else if (length.isValid() &&
               length.toLongLong() > (m_phase == Phase::Check
                                          ? static_cast<qint64>(services::kMaxReleaseJsonBytes)
                                          : static_cast<qint64>(services::kMaxChecksumFileBytes))) {
        abortWith(makeError(ErrorCode::LimitExceeded,
                            "GitHub's answer is larger than expected, so it was ignored."));
    }
}

void UpdateClient::onReadyRead() {
    if (!m_reply || m_abortError)
        return;
    // Redirects and errors: only the status counts, the body is dropped and
    // does not hold off the timeouts.
    const bool body = httpStatus(m_reply) == 200;
    while (m_reply->bytesAvailable() > 0) {
        const QByteArray chunk = m_reply->read(kChunkBytes);
        if (chunk.isEmpty())
            break;
        if (body && !acceptBody(chunk))
            return;
    }
    if (body && m_phase == Phase::Installer) {
        m_timer.start(); // the stall timeout counts from the last data
        emit downloadProgress(m_received, m_total);
    }
}

bool UpdateClient::acceptBody(const QByteArray& chunk) {
    m_received += chunk.size();
    if (m_phase != Phase::Installer) {
        const auto limit =
            static_cast<qint64>(m_phase == Phase::Check ? services::kMaxReleaseJsonBytes
                                                        : services::kMaxChecksumFileBytes);
        if (m_received > limit) {
            abortWith(makeError(ErrorCode::LimitExceeded,
                                "GitHub's answer is larger than expected, so it was ignored."));
            return false;
        }
        m_body.append(chunk);
        return true;
    }
    if (m_received > m_options.maxInstallerBytes) {
        abortWith(makeError(ErrorCode::LimitExceeded,
                            "The installer is larger than this application accepts, so the "
                            "download was stopped."));
        return false;
    }
    m_hash.update(bytesOf(chunk));
    if (m_file->write(chunk) != chunk.size()) {
        abortWith(makeError(ErrorCode::IoError,
                            "The installer could not be saved. Check the free disk space and try "
                            "again.",
                            utf8(m_file->errorString())));
        return false;
    }
    return true;
}

void UpdateClient::onFinished() {
    auto* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply)
        return;
    reply->deleteLater();
    if (reply != m_reply)
        return;
    // What the last readyRead left behind.
    onReadyRead();
    m_reply = nullptr;

    if (m_abortError) {
        fail(*std::exchange(m_abortError, std::nullopt));
        return;
    }
    const int status = httpStatus(reply);
    if (isRedirectStatus(status)) {
        followRedirect(reply);
        return;
    }
    // A missing "latest release" means none was published yet.
    if (m_phase == Phase::Check && status == 404) {
        logInfo(kComponent, "no release has been published yet");
        m_phase = Phase::Idle;
        m_timer.stop();
        emit checkFinished(std::optional<services::ReleaseInfo>{});
        return;
    }
    if (status == 0 || reply->error() == QNetworkReply::SslHandshakeFailedError) {
        // No HTTP answer at all: offline, name resolution, TLS, connection lost.
        fail(makeError(ErrorCode::Offline,
                       "Could not reach GitHub. Check the internet connection and try again.",
                       utf8(reply->errorString())));
        return;
    }
    if (status == 403 || status == 429) {
        fail(makeError(ErrorCode::ProviderUnavailable,
                       "GitHub is limiting requests from this network. Try again later.",
                       "HTTP " + std::to_string(status)));
        return;
    }
    if (status != 200 || reply->error() != QNetworkReply::NoError) {
        fail(makeError(ErrorCode::ProviderUnavailable,
                       "GitHub answered with an error (HTTP " + std::to_string(status) +
                           "). Try again later.",
                       utf8(reply->errorString())));
        return;
    }
    finishBody();
}

void UpdateClient::followRedirect(QNetworkReply* reply) {
    const QUrl target =
        reply->url().resolved(QUrl::fromEncoded(reply->rawHeader("Location"), QUrl::StrictMode));
    const std::string text = utf8(target.toString(QUrl::FullyEncoded));
    if (!target.isValid() || !services::isAllowedRedirectTarget(text, m_options.endpoints)) {
        // The host is enough to report; the full address may be long and signed.
        logWarning(kComponent, "refused a redirect", {{"host", utf8(target.host())}});
        fail(makeError(ErrorCode::ValidationFailed,
                       "GitHub redirected this request to an address this application does not "
                       "trust, so it was stopped.",
                       "redirect host: " + utf8(target.host())));
        return;
    }
    if (++m_redirects > services::kMaxUpdateRedirects) {
        fail(makeError(ErrorCode::ValidationFailed,
                       "GitHub redirected this request too many times, so it was stopped."));
        return;
    }
    startHop(target);
}

void UpdateClient::finishBody() {
    switch (m_phase) {
    case Phase::Check: {
        auto parsed = services::parseLatestRelease(viewOf(m_body), m_options.endpoints);
        if (!parsed) {
            fail(std::move(parsed).error());
            return;
        }
        m_phase = Phase::Idle;
        m_timer.stop();
        emit checkFinished(parsed);
        return;
    }
    case Phase::Checksums: {
        const std::string& name = m_installerAsset.name;
        auto digest = services::findChecksum(viewOf(m_body), name);
        if (!digest) {
            fail(std::move(digest).error());
            return;
        }
        m_expectedDigest = std::move(digest).value();
        m_hash = Sha256();
        m_file = std::make_unique<QSaveFile>(QDir(m_options.updatesDir).filePath(qs(name)));
        if (!m_file->open(QIODevice::WriteOnly)) {
            fail(makeError(ErrorCode::IoError,
                           "The installer could not be saved. Check the free disk space and try "
                           "again.",
                           utf8(m_file->errorString())));
            return;
        }
        startPhase(Phase::Installer, QUrl(qs(m_installerAsset.url)));
        return;
    }
    case Phase::Installer: {
        const std::string actual = Sha256::toHex(m_hash.finish());
        if (actual != m_expectedDigest) {
            logWarning(kComponent, "the installer does not match its checksum",
                       {{"expected", m_expectedDigest}, {"actual", actual}});
            fail(makeError(ErrorCode::IntegrityCheckFailed,
                           "The downloaded installer does not match its checksum, so it was "
                           "deleted. Try again; if this keeps happening, download it from the "
                           "release page."));
            return;
        }
        const QString path = m_file->fileName();
        if (!m_file->commit()) {
            fail(makeError(ErrorCode::IoError,
                           "The installer could not be saved. Check the free disk space and try "
                           "again.",
                           utf8(m_file->errorString())));
            return;
        }
        m_file.reset();
        m_phase = Phase::Idle;
        m_timer.stop();
        logInfo(kComponent, "installer downloaded and verified", {{"sha256", m_expectedDigest}});
        emit downloadFinished(DownloadedInstaller{path, m_expectedDigest});
        return;
    }
    case Phase::Idle:
        return;
    }
}

void UpdateClient::fail(Error error) {
    const Phase phase = m_phase;
    m_timer.stop();
    cleanUpPartialFile();
    m_phase = Phase::Idle;
    logWarning(kComponent, "update request failed",
               {{"error", error.message}, {"detail", error.detail}});
    if (phase == Phase::Check)
        emit checkFinished(std::move(error));
    else
        emit downloadFinished(std::move(error));
}

} // namespace pmm::app
