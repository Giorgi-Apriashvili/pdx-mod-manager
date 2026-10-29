#pragma once

// services::WorkshopSessionHost on QProcess: one pmm-steam-helper process per
// game, started as `pmm-steam-helper --app-id <id> --steam-api <library>`.
// Commands go to its stdin as encodeCommand() lines; stdout is split into
// lines (providers::LineSplitter) and decoded with providers::decodeEvent()
// (undecodable lines are logged and skipped); the process exit is reported
// once all output was delivered. stderr can contain the user's SteamID, so it
// is only logged redacted, at debug level.
//
// Every signal is handled through queued connections and checked against the
// session it belongs to, so nothing is ever delivered re-entrantly from
// open()/send()/close(), and nothing of a session arrives after close():
// close() forgets the session, sends QuitCommand and lets the process exit on
// its own, killing it after the grace period.
//
// Lives on, and must only be used from, the thread that runs the coordinator.

#include "providers/helper_protocol.hpp"
#include "services/acquisition_coordinator.hpp"

#include <QObject>
#include <QString>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

class QProcess;

namespace pmm::app {

class QtWorkshopSessionHost : public QObject, public services::WorkshopSessionHost {
    Q_OBJECT

public:
    using EventSink = std::function<void(const GameKey&, const providers::HelperEvent&)>;
    using ExitSink = std::function<void(const GameKey&, int exitCode)>;

    explicit QtWorkshopSessionHost(QString helperExecutable, QObject* parent = nullptr);
    ~QtWorkshopSessionHost() override;

    // PMM_STEAM_HELPER when set (development override), otherwise
    // pmm-steam-helper[.exe] next to the application executable.
    static QString defaultHelperPath();

    const QString& helperExecutable() const noexcept { return m_helper; }
    void setSinks(EventSink events, ExitSink exits);
    void setQuitGracePeriod(std::chrono::milliseconds grace) { m_grace = grace; }

    bool open(const GameKey& gameKey, const std::string& appId,
              const std::filesystem::path& steamApiLibrary) override;
    void send(const GameKey& gameKey, const providers::HelperCommand& command) override;
    void close(const GameKey& gameKey) override;

    bool isOpen(const GameKey& gameKey) const;
    // Closes every session and waits (bounded) until the processes are gone.
    void closeAll();

private:
    struct Session {
        std::uint64_t serial = 0;
        QProcess* process = nullptr;
        providers::LineSplitter out;
        providers::LineSplitter err;
    };

    Session* find(const GameKey& gameKey, std::uint64_t serial);
    void readOutput(const GameKey& gameKey, std::uint64_t serial);
    void readErrors(const GameKey& gameKey, std::uint64_t serial);
    void finished(const GameKey& gameKey, std::uint64_t serial, int exitCode);
    void retire(QProcess* process, bool sendQuit);
    void waitForRetired();

    QString m_helper;
    EventSink m_events;
    ExitSink m_exits;
    std::chrono::milliseconds m_grace{3000};
    std::map<GameKey, std::unique_ptr<Session>> m_sessions;
    std::vector<QProcess*> m_retired; // closed, waiting to exit
    std::uint64_t m_nextSerial = 1;
};

} // namespace pmm::app
