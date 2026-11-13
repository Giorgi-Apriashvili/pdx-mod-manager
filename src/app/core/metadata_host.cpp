#include "app/core/metadata_host.hpp"

#include "app/core/qt_util.hpp"
#include "core/log.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <utility>

namespace pmm::app {
namespace {

constexpr const char* kComponent = "app.metadata";

} // namespace

QtWorkshopMetadataHost::QtWorkshopMetadataHost(QObject* parent)
    : QObject(parent), m_endpoint(defaultEndpoint()) {}

QtWorkshopMetadataHost::~QtWorkshopMetadataHost() {
    // The replies die with this object: their lookups are answered now.
    for (auto& [id, completion] : std::exchange(m_lookups, {})) {
        WorkshopLookupReply reply;
        reply.error = "the Web API host was closed before the reply arrived";
        completion(std::move(reply));
    }
}

QUrl QtWorkshopMetadataHost::defaultEndpoint() {
    const QString overrideUrl = qEnvironmentVariable("PMM_WORKSHOP_METADATA_URL");
    if (!overrideUrl.isEmpty())
        return QUrl(overrideUrl);
    return QUrl(qs(providers::kPublishedFileDetailsUrl));
}

void QtWorkshopMetadataHost::request(const GameKey& gameKey, const std::vector<std::string>& ids) {
    post(gameKey, ids, "acquisition",
         [this, gameKey](Result<std::vector<providers::WorkshopItemMetadata>> items) {
             if (items && m_sink)
                 m_sink(gameKey, items.value());
         });
}

void QtWorkshopMetadataHost::lookup(const GameKey& gameKey, const std::vector<std::string>& ids,
                                    Completion completion) {
    const quint64 id = ++m_lastLookup;
    m_lookups.emplace(id, std::move(completion));
    const bool posted =
        post(gameKey, ids, "preview",
             [this, id](Result<std::vector<providers::WorkshopItemMetadata>> items) {
                 WorkshopLookupReply reply;
                 reply.answered = items.ok();
                 if (items)
                     reply.items = std::move(items).value();
                 else
                     reply.error = items.error().message;
                 answerLookup(id, std::move(reply));
             });
    if (!posted) {
        // Answered later all the same, as a caller expects of a request.
        QMetaObject::invokeMethod(
            this,
            [this, id] {
                WorkshopLookupReply reply;
                reply.error = "no request could be built for the item ids";
                answerLookup(id, std::move(reply));
            },
            Qt::QueuedConnection);
    }
}

void QtWorkshopMetadataHost::answerLookup(quint64 id, WorkshopLookupReply reply) {
    const auto it = m_lookups.find(id);
    if (it == m_lookups.end())
        return;
    const Completion completion = std::move(it->second);
    m_lookups.erase(it);
    completion(std::move(reply));
}

bool QtWorkshopMetadataHost::post(const GameKey& gameKey, const std::vector<std::string>& ids,
                                  std::string_view purpose, Finished finished) {
    auto body = providers::buildPublishedFileDetailsRequest(ids);
    if (!body) {
        logWarning(kComponent, "workshop metadata request not built",
                   {{"game", gameKey},
                    {"purpose", std::string(purpose)},
                    {"error", body.error().message}});
        return false;
    }
    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    QNetworkRequest request(m_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      qs(providers::kPublishedFileDetailsContentType));
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    // Fixed, so that nothing depends on this computer: Qt would send the
    // system's locale.
    request.setRawHeader("Accept-Language", "en");
    request.setTransferTimeout(static_cast<int>(m_timeout.count()));
    QNetworkReply* reply = m_network->post(request, QByteArray::fromStdString(body.value()));
    ++m_pending;
    logDebug(kComponent, "workshop metadata requested",
             {{"game", gameKey},
              {"purpose", std::string(purpose)},
              {"items", std::to_string(ids.size())}});

    // Oversized replies are cut off instead of being buffered whole.
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > static_cast<qint64>(providers::kMaxPublishedFileDetailsResponseBytes))
            reply->abort();
    });
    connect(
        reply, &QNetworkReply::finished, this,
        [this, reply, gameKey, purpose = std::string(purpose), finished = std::move(finished)] {
            reply->deleteLater();
            --m_pending;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() != QNetworkReply::NoError || status != 200) {
                const std::string error = utf8(reply->errorString());
                logWarning(kComponent, "workshop metadata request failed",
                           {{"game", gameKey},
                            {"purpose", purpose},
                            {"status", std::to_string(status)},
                            {"error", error}});
                finished(makeError(ErrorCode::ProviderUnavailable,
                                   "The Steam Web API could not be reached.",
                                   "HTTP " + std::to_string(status) + ": " + error));
                emit requestFinished(false);
                return;
            }
            const QByteArray bytes = reply->readAll();
            auto items = providers::parsePublishedFileDetailsResponse(
                std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
            if (!items) {
                logWarning(
                    kComponent, "workshop metadata reply not understood",
                    {{"game", gameKey}, {"purpose", purpose}, {"error", items.error().message}});
                finished(std::move(items).error());
                emit requestFinished(false);
                return;
            }
            logDebug(kComponent, "workshop metadata received",
                     {{"game", gameKey},
                      {"purpose", purpose},
                      {"items", std::to_string(items.value().size())}});
            finished(std::move(items));
            emit requestFinished(true);
        });
    return true;
}

} // namespace pmm::app
