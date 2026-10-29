#pragma once

// services::WorkshopMetadataHost on QNetworkAccessManager: a key-less POST to
// the Steam Web API GetPublishedFileDetails endpoint (body built by
// providers::buildPublishedFileDetailsRequest), parsed with
// providers::parsePublishedFileDetailsResponse and handed to the sink (the
// coordinator's onMetadata). Failures - offline, timeout, HTTP errors,
// malformed or oversized replies - are logged and simply not answered. No API
// key, no cookies, no telemetry. The endpoint can be overridden with
// PMM_WORKSHOP_METADATA_URL (tests, local stubs).
//
// Lives on, and must only be used from, the thread that runs the coordinator.

#include "providers/web_metadata.hpp"
#include "services/acquisition_coordinator.hpp"

#include <QObject>
#include <QUrl>

#include <chrono>
#include <functional>
#include <vector>

class QNetworkAccessManager;

namespace pmm::app {

class QtWorkshopMetadataHost : public QObject, public services::WorkshopMetadataHost {
    Q_OBJECT

public:
    using Sink =
        std::function<void(const GameKey&, const std::vector<providers::WorkshopItemMetadata>&)>;

    explicit QtWorkshopMetadataHost(QObject* parent = nullptr);
    ~QtWorkshopMetadataHost() override;

    static QUrl defaultEndpoint();

    void setSink(Sink sink) { m_sink = std::move(sink); }
    void setEndpoint(QUrl endpoint) { m_endpoint = std::move(endpoint); }
    void setTimeout(std::chrono::milliseconds timeout) { m_timeout = timeout; }
    int pendingRequests() const noexcept { return m_pending; }

    void request(const GameKey& gameKey, const std::vector<std::string>& ids) override;

signals:
    // Emitted after a request finished, answered or not (tests).
    void requestFinished(bool answered);

private:
    QNetworkAccessManager* m_network = nullptr; // created on first use, in this object's thread
    QUrl m_endpoint;
    std::chrono::milliseconds m_timeout{15000};
    Sink m_sink;
    int m_pending = 0;
};

} // namespace pmm::app
