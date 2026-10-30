#pragma once

// The update check's network side (docs/updates.md), on its own
// QNetworkAccessManager: asks GitHub for the latest release and downloads the
// installer with its checksum file. Nothing here runs until check() or
// download() is called. It sends a User-Agent and nothing else about the user:
// no token, no cookies. Every redirect hop is checked against the allow-list of
// services::UpdateEndpoints before it is followed; the installer is written to a
// temporary file, hashed while streaming and only gets its final name once the
// digest matches, so an unverified file can never be started.
//
// One operation at a time. Lives on, and must only be used from, one thread (the
// GUI thread: all work is asynchronous and the file writes are small).

#include "core/result.hpp"
#include "core/sha256.hpp"
#include "services/app_update.hpp"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <memory>
#include <optional>
#include <string>

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

namespace pmm::app {

struct UpdateClientOptions {
    services::UpdateEndpoints endpoints;
    QString updatesDir; // dedicated: every file in it is disposable
    QString userAgent;
    std::chrono::milliseconds checkTimeout{15000}; // a check, and the checksum file
    std::chrono::milliseconds stallTimeout{30000}; // the installer: no new data for this long
    qint64 maxInstallerBytes = services::kMaxInstallerBytes;
};

struct DownloadedInstaller {
    QString path;
    std::string sha256;
};

// Empty: nothing to offer (no release published yet, or one flagged as draft or
// prerelease). Both normal outcomes, not errors.
using UpdateCheckResult = Result<std::optional<services::ReleaseInfo>>;
using UpdateDownloadResult = Result<DownloadedInstaller>;

class UpdateClient : public QObject {
    Q_OBJECT

public:
    explicit UpdateClient(UpdateClientOptions options, QObject* parent = nullptr);
    ~UpdateClient() override;

    void setUpdatesDir(const QString& dir) { m_options.updatesDir = dir; }
    bool busy() const noexcept { return m_phase != Phase::Idle; }

    // Each ends with its signal, unless cancel() ended it first.
    void check();
    void download(const services::ReleaseInfo& release);
    void cancel();

    // Removes every file of the updates folder.
    void removeDownloads() const;
    // True when the file still has this digest (re-checked right before a start).
    static bool fileHasDigest(const QString& path, const std::string& sha256);

signals:
    void checkFinished(const pmm::app::UpdateCheckResult& result);
    void downloadProgress(qint64 received, qint64 total); // total 0: unknown
    void downloadFinished(const pmm::app::UpdateDownloadResult& result);

private:
    enum class Phase { Idle, Check, Checksums, Installer };

    void startPhase(Phase phase, const QUrl& url);
    void startHop(const QUrl& url);
    void stopReply();
    void onMetaData();
    void onReadyRead();
    void onFinished();
    void onTimeout();
    void followRedirect(QNetworkReply* reply);
    bool acceptBody(const QByteArray& chunk);
    void finishBody();
    void abortWith(Error error);
    void fail(Error error);
    void cleanUpPartialFile();

    UpdateClientOptions m_options;
    QNetworkAccessManager* m_network = nullptr; // created on first use
    QNetworkReply* m_reply = nullptr;
    Phase m_phase = Phase::Idle;
    int m_redirects = 0;
    std::optional<Error> m_abortError; // why m_reply was aborted
    QTimer m_timer;
    QByteArray m_body; // check and checksum replies
    services::ReleaseAsset m_installerAsset;
    std::string m_expectedDigest;
    std::unique_ptr<QSaveFile> m_file;
    Sha256 m_hash;
    qint64 m_received = 0;
    qint64 m_total = 0;
};

} // namespace pmm::app
