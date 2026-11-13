#pragma once

// services::WorkshopMetadataHost on QNetworkAccessManager: a key-less POST to
// the Steam Web API GetPublishedFileDetails endpoint (body built by
// providers::buildPublishedFileDetailsRequest), parsed with
// providers::parsePublishedFileDetailsResponse. No API key, no cookies, no
// telemetry. The endpoint can be overridden with PMM_WORKSHOP_METADATA_URL
// (tests, local stubs). Two kinds of requests share it:
//
//   request()  for the acquisition coordinator: the answer goes to the sink
//              (the coordinator's onMetadata). Failures - offline, timeout,
//              HTTP errors, malformed or oversized replies - are logged and
//              simply not answered.
//   lookup()   for the import preview: the answer goes to its own completion,
//              exactly once, failures included, and never to the sink, so a
//              preview never rejects or confirms an acquisition job.
//
// Lives on, and must only be used from, the thread that runs the coordinator.

#include "core/result.hpp"
#include "providers/web_metadata.hpp"
#include "services/acquisition_coordinator.hpp"

#include <QObject>
#include <QUrl>

#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

class QNetworkAccessManager;

namespace pmm::app {

// The answer to QtWorkshopMetadataHost::lookup().
struct WorkshopLookupReply {
    bool answered = false;                              // a reply was received and understood
    std::vector<providers::WorkshopItemMetadata> items; // in reply order
    std::string error;                                  // why not, when !answered
};

class QtWorkshopMetadataHost : public QObject, public services::WorkshopMetadataHost {
    Q_OBJECT

public:
    using Sink =
        std::function<void(const GameKey&, const std::vector<providers::WorkshopItemMetadata>&)>;
    using Completion = std::function<void(WorkshopLookupReply)>;

    explicit QtWorkshopMetadataHost(QObject* parent = nullptr);
    ~QtWorkshopMetadataHost() override;

    static QUrl defaultEndpoint();

    void setSink(Sink sink) { m_sink = std::move(sink); }
    void setEndpoint(QUrl endpoint) { m_endpoint = std::move(endpoint); }
    void setTimeout(std::chrono::milliseconds timeout) { m_timeout = timeout; }
    int pendingRequests() const noexcept { return m_pending; }

    void request(const GameKey& gameKey, const std::vector<std::string>& ids) override;

    // At most providers::kMaxPublishedFileDetailsItems ids. `completion` runs
    // exactly once on this object's thread, never from inside lookup() itself:
    // with the items, or unanswered after an HTTP error, a malformed or
    // oversized reply, a timeout, or for ids no request can be built for. If
    // this object is destroyed first, it runs unanswered from the destructor.
    void lookup(const GameKey& gameKey, const std::vector<std::string>& ids, Completion completion);

signals:
    // Emitted after a request finished, answered or not (tests).
    void requestFinished(bool answered);

private:
    using Finished = std::function<void(Result<std::vector<providers::WorkshopItemMetadata>>)>;

    // Posts one request; `finished` runs when its reply has finished. Returns
    // false, without a request and without calling `finished`, when no request
    // can be built for `ids` (logged).
    bool post(const GameKey& gameKey, const std::vector<std::string>& ids, std::string_view purpose,
              Finished finished);
    // Runs the completion of lookup `id` unless it has run already.
    void answerLookup(quint64 id, WorkshopLookupReply reply);

    QNetworkAccessManager* m_network = nullptr; // created on first use, in this object's thread
    QUrl m_endpoint;
    std::chrono::milliseconds m_timeout{15000};
    Sink m_sink;
    int m_pending = 0;
    std::map<quint64, Completion> m_lookups; // not answered yet, by number
    quint64 m_lastLookup = 0;
};

} // namespace pmm::app
