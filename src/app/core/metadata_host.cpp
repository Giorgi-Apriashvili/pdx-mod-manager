#include "app/core/metadata_host.hpp"

#include "app/core/qt_util.hpp"
#include "core/log.hpp"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace pmm::app {
namespace {

constexpr const char* kComponent = "app.metadata";

} // namespace

QtWorkshopMetadataHost::QtWorkshopMetadataHost(QObject* parent)
    : QObject(parent), m_endpoint(defaultEndpoint()) {}

QtWorkshopMetadataHost::~QtWorkshopMetadataHost() = default;

QUrl QtWorkshopMetadataHost::defaultEndpoint() {
    const QString overrideUrl = qEnvironmentVariable("PMM_WORKSHOP_METADATA_URL");
    if (!overrideUrl.isEmpty())
        return QUrl(overrideUrl);
    return QUrl(qs(providers::kPublishedFileDetailsUrl));
}

void QtWorkshopMetadataHost::request(const GameKey& gameKey, const std::vector<std::string>& ids) {
    auto body = providers::buildPublishedFileDetailsRequest(ids);
    if (!body) {
        logWarning(kComponent, "workshop metadata request not built",
                   {{"game", gameKey}, {"error", body.error().message}});
        return;
    }
    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    QNetworkRequest request(m_endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      qs(providers::kPublishedFileDetailsContentType));
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setTransferTimeout(static_cast<int>(m_timeout.count()));
    QNetworkReply* reply = m_network->post(request, QByteArray::fromStdString(body.value()));
    ++m_pending;
    logDebug(kComponent, "workshop metadata requested",
             {{"game", gameKey}, {"items", std::to_string(ids.size())}});

    // Oversized replies are cut off instead of being buffered whole.
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > static_cast<qint64>(providers::kMaxPublishedFileDetailsResponseBytes))
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, gameKey] {
        reply->deleteLater();
        --m_pending;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            logWarning(kComponent, "workshop metadata request failed",
                       {{"game", gameKey},
                        {"status", std::to_string(status)},
                        {"error", utf8(reply->errorString())}});
            emit requestFinished(false);
            return;
        }
        const QByteArray bytes = reply->readAll();
        auto items = providers::parsePublishedFileDetailsResponse(
            std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
        if (!items) {
            logWarning(kComponent, "workshop metadata reply not understood",
                       {{"game", gameKey}, {"error", items.error().message}});
            emit requestFinished(false);
            return;
        }
        logDebug(kComponent, "workshop metadata received",
                 {{"game", gameKey}, {"items", std::to_string(items.value().size())}});
        if (m_sink)
            m_sink(gameKey, items.value());
        emit requestFinished(true);
    });
}

} // namespace pmm::app
