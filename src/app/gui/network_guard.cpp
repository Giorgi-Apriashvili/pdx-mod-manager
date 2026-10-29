#include "app/gui/network_guard.hpp"

#include "app/core/qt_util.hpp"
#include "core/log.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequest>

namespace pmm::app {
namespace {

class OfflineNetworkAccessManager : public QNetworkAccessManager {
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request,
                                 QIODevice* outgoingData) override {
        if (OfflineNetworkAccessManagerFactory::allowed(request.url()))
            return QNetworkAccessManager::createRequest(op, request, outgoingData);
        // The URL may come from imported text: log its scheme only.
        logWarning("app.qml", "refused a network request of the user interface",
                   {{"scheme", utf8(request.url().scheme())}});
        // A request without a URL fails at once (unknown protocol).
        return QNetworkAccessManager::createRequest(op, QNetworkRequest(), nullptr);
    }
};

} // namespace

QNetworkAccessManager* OfflineNetworkAccessManagerFactory::create(QObject* parent) {
    return new OfflineNetworkAccessManager(parent);
}

bool OfflineNetworkAccessManagerFactory::allowed(const QUrl& url) {
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("qrc") || scheme == QLatin1String("data"))
        return true;
    return scheme == QLatin1String("file") && url.host().isEmpty();
}

} // namespace pmm::app
