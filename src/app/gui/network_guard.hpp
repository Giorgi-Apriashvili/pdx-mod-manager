#pragma once

// Network access of the QML engine. The user interface never loads anything
// over the network, but rich text, images and XMLHttpRequest would, and
// imported or Workshop strings must never become a request (no telemetry, no
// leaking that a code was previewed). The managers this factory creates
// refuse every URL except qrc: and data:, and file: without a host (a host
// would make Windows open a UNC path). QtWorkshopMetadataHost and UpdateClient
// use their own QNetworkAccessManager and are not affected.

#include <QQmlNetworkAccessManagerFactory>
#include <QUrl>

namespace pmm::app {

class OfflineNetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory {
public:
    // Called by the engine from several threads; creates a new manager.
    QNetworkAccessManager* create(QObject* parent) override;

    // True for the URLs the user interface may load.
    static bool allowed(const QUrl& url);
};

} // namespace pmm::app
