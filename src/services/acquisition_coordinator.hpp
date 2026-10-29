#pragma once

// Drives the Workshop acquisition reducer (providers/acquisition.hpp) for the
// persisted acquisition jobs (spec section 6).
//
// The coordinator owns one AcquisitionContext per job it works on and one
// helper session per game. It never blocks: the helper process and the Web
// API are reached through the host interfaces below, implemented by the Qt
// layer (QProcess, QNetworkAccessManager) and by fakes in tests, and their
// answers come back as onHelperEvent() / onHelperExited() / onMetadata()
// calls. Time advances only through tick(nowMs).
//
// Responsibilities:
//   - start(game) resumes every unfinished stored job of the game (restart
//     reconciliation: each run queries Steam before deciding anything, so
//     nothing is re-subscribed blindly and installed items are verified, not
//     downloaded again) and picks up jobs created since (e.g. by an import);
//   - opens the helper session on demand with the game's own Steam API library
//     (platform::steam::findSteamApiLibrary) and closes it as soon as no job
//     of the game is waiting for Steam;
//   - maps helper events with observationFromHelperEvent() and actions with
//     helperCommandFor(), batching state queries into one command;
//   - verifies reported content with providers::verifyWorkshopContent();
//   - persists jobs when the reducer says so (progress-only changes are
//     coalesced to one write per progressPersistIntervalMs);
//   - rescans the game's inventory when an item becomes Installed, so that
//     readiness flips to Ready only once the inventory has seen the content;
//   - reports provider status (Steam not running, not signed in, not owned,
//     unavailable) and, for jobs that need the user, the clearly labelled
//     manual Workshop page fallback (providers/fallback.hpp).
// Nothing here ever unsubscribes or deletes Workshop content.
//
// Not thread-safe: call every method from the services worker thread. Host
// callbacks made re-entrantly (from inside open/send/close) are queued and
// processed before the outer call returns.

#include "core/model.hpp"
#include "core/result.hpp"
#include "providers/acquisition.hpp"
#include "providers/fallback.hpp"
#include "providers/helper_protocol.hpp"
#include "providers/web_metadata.hpp"
#include "services/game_service.hpp"
#include "services/service_context.hpp"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace pmm::services {

// Runs pmm-steam-helper sessions, one per game.
class WorkshopSessionHost {
public:
    virtual ~WorkshopSessionHost() = default;
    // Starts the helper for `gameKey` with `appId` and the game's Steam API
    // library. Returns false when the process cannot be started at all. The
    // helper's events and its exit are delivered later through
    // AcquisitionCoordinator::onHelperEvent() / onHelperExited().
    virtual bool open(const GameKey& gameKey, const std::string& appId,
                      const std::filesystem::path& steamApiLibrary) = 0;
    virtual void send(const GameKey& gameKey, const providers::HelperCommand& command) = 0;
    // Ends the session (QuitCommand, then terminate after a grace period).
    // After close() returns, no event or exit of that session may be delivered.
    virtual void close(const GameKey& gameKey) = 0;
};

// Performs Web API GetPublishedFileDetails requests (providers/web_metadata.hpp).
class WorkshopMetadataHost {
public:
    virtual ~WorkshopMetadataHost() = default;
    // At most providers::kMaxPublishedFileDetailsItems ids per call. Results
    // are delivered through AcquisitionCoordinator::onMetadata(); a failed
    // request (offline) may simply never be answered.
    virtual void request(const GameKey& gameKey, const std::vector<std::string>& ids) = 0;
};

enum class ProviderStatus { Unknown, Ready, SteamNotRunning, NotLoggedOn, NotOwned, Unavailable };
// "unknown", "ready", "steam_not_running", "not_logged_on", "not_owned", "unavailable"
const char* toString(ProviderStatus status) noexcept;

struct CoordinatorOptions {
    providers::AcquisitionPolicy policy;
    // pmm-steam-helper; when set, a missing file is reported before trying
    // to open a session. Empty: not checked (the host knows the path).
    std::filesystem::path helperExecutable;
    std::int64_t progressPersistIntervalMs = 2'000;
    // Content check for VerifyContent; defaults to providers::verifyWorkshopContent.
    std::function<Status(const std::filesystem::path&)> verifyContent;
};

struct JobSnapshot {
    AcquisitionJob job;
    std::string name;       // Web API title > playset name hint > provider id
    std::string stateText;  // "Queued", "Downloading 45 %", "Failed", ...
    double progress = -1.0; // 0..1 while the size is known, otherwise -1
    bool active = false;    // the coordinator is waiting for Steam on its behalf
    bool canRetry = false;
    bool canOpenFallback = false;
    // Manual Workshop page (label: providers::kManualFallbackLabel); set when
    // canOpenFallback. A limited fallback: nothing is downloaded for the user.
    std::optional<providers::ManualFallbackLinks> fallback;
};

struct AcquisitionSnapshot {
    GameKey game;
    // Jobs worked on in this session plus stored jobs that need attention
    // (failed, needs user action, unavailable), newest job per mod only. Idle
    // jobs whose mod a rescan has since found installed (for example after the
    // manual fallback) are left out: usable installed content wins.
    std::vector<JobSnapshot> jobs;
    ProviderStatus providerStatus = ProviderStatus::Unknown;
    std::string providerMessage; // recovery text for the status
    bool active = false;         // some job is in progress
    std::string summary;         // "2 downloading, 1 failed", empty when idle
};

// What a call changed, so the caller knows what to refresh.
struct CoordinatorUpdate {
    bool changed = false;             // job states/progress or provider status changed
    std::vector<GameState> rescanned; // inventories rescanned because items were installed
};

struct CancelOutcome {
    CoordinatorUpdate update;
    std::size_t cancelled = 0;
    std::string message; // explains that completed subscriptions/downloads remain in Steam
};

struct FallbackOutcome {
    CoordinatorUpdate update;
    providers::ManualFallbackLinks links; // open steamClientUrl, else webUrl
    std::string label;                    // providers::kManualFallbackLabel
};

class AcquisitionCoordinator {
public:
    // `metadata` may be null (no Web API checks). `games` performs the
    // inventory rescans after installs.
    AcquisitionCoordinator(const ServiceContext& context, GameService& games,
                           WorkshopSessionHost& sessions, WorkshopMetadataHost* metadata,
                           CoordinatorOptions options = {});

    // Resumes unfinished stored jobs of `game` and starts new ones.
    CoordinatorUpdate start(const GameKey& game);
    // Creates jobs for every missing Workshop entry of the playset (disabled
    // ones included; never for installed usable content), then start().
    Result<CoordinatorUpdate> acquireMissing(std::int64_t playsetId);

    CoordinatorUpdate onHelperEvent(const GameKey& game, const providers::HelperEvent& event);
    CoordinatorUpdate onHelperExited(const GameKey& game, int exitCode);
    CoordinatorUpdate onMetadata(const GameKey& game,
                                 const std::vector<providers::WorkshopItemMetadata>& items);
    CoordinatorUpdate tick(std::int64_t nowMs);

    // User-initiated retry of a failed / needs-action / unavailable /
    // cancelled job (a fresh retry cycle). AlreadyExists when another job for
    // the same mod is in progress.
    Result<CoordinatorUpdate> retry(std::int64_t jobId);
    // Retries the newest job of every mod of `game` that failed or needs the user.
    CoordinatorUpdate retryAllFailed(const GameKey& game);
    // Stops application-managed work for `game`. Never unsubscribes or deletes.
    CancelOutcome cancelAll(const GameKey& game);
    // Marks the job as waiting for the user and returns the manual Workshop
    // page links for the caller to open.
    Result<FallbackOutcome> openFallback(std::int64_t jobId);

    // Before launching `game`: persists progress, closes its helper session
    // (Steam would otherwise consider the game already running) and parks its
    // unfinished jobs; the next start(game) reconciles them with Steam.
    CoordinatorUpdate closeSessionsForLaunch(const GameKey& game);
    // On application exit: persists progress and closes every session.
    void shutdown();

    AcquisitionSnapshot snapshot(const GameKey& game) const;
    // In-memory jobs of `game` (fresher progress than the database rows), for
    // loadPlaysetView() and LaunchService::check().
    std::vector<AcquisitionJob> liveJobs(const GameKey& game) const;
    bool hasActiveWork(const GameKey& game) const;
    bool sessionOpen(const GameKey& game) const;

private:
    struct Runtime {
        GameKey game;
        providers::AcquisitionContext context;
        AcquisitionJob persisted; // last state written to the database
        std::int64_t persistedAtMs = 0;
        bool dirty = false; // coalesced progress not written yet
    };
    enum class SessionState { Closed, Opening, Ready, Failed };
    // Why a session could not be provided; applied to every job asking for it.
    struct SessionFailure {
        std::optional<providers::obs::UnavailableReason> reason; // fed to the reducer
        ErrorCode code = ErrorCode::ProviderUnavailable;         // else: set directly
        std::string message;
    };
    struct Session {
        SessionState state = SessionState::Closed;
        std::int64_t openedAtMs = 0;
        bool hostOpen = false; // open() succeeded; close() must be called
        providers::ReadyEvent ready;
        std::optional<SessionFailure> failure;
        std::uint64_t failedInPass = 0;
    };
    struct Provider {
        ProviderStatus status = ProviderStatus::Unknown;
        std::string message;
    };

    // Runs `work`, then processes queued observations, flushes helper
    // commands, rescans and closes idle sessions until nothing is left.
    CoordinatorUpdate run(const std::function<void()>& work);
    void enqueue(std::int64_t jobId, providers::Observation observation);
    void process(std::int64_t jobId, const providers::Observation& observation);
    void apply(std::int64_t jobId, providers::Transition transition);
    void perform(std::int64_t jobId, const providers::Action& action);
    void ensureSession(const GameKey& game, std::int64_t jobId);
    void applyFailure(std::int64_t jobId, const SessionFailure& failure);
    void failSession(const GameKey& game, SessionFailure failure, ProviderStatus status);
    void closeSession(const GameKey& game);
    void closeIdleSessions();
    void flushOutbox();
    void persist(Runtime& runtime, bool force);
    void setProvider(const GameKey& game, ProviderStatus status, std::string message);
    Runtime* adopt(const AcquisitionJob& job, bool resume);
    void requestMetadata(const GameKey& game, const std::vector<std::string>& ids);
    void rememberNames(const std::vector<AcquisitionJob>& jobs);
    std::optional<GameInstallation> storedInstallation(const GameKey& game) const;
    std::string appIdFor(const GameKey& game) const;
    std::string nameFor(const AcquisitionJob& job) const;
    bool busy(const Runtime& runtime) const noexcept;

    const ServiceContext& m_ctx;
    GameService& m_games;
    WorkshopSessionHost& m_sessions;
    WorkshopMetadataHost* m_metadata;
    CoordinatorOptions m_options;

    std::map<std::int64_t, Runtime> m_jobs;
    std::map<GameKey, Session> m_sessionState;
    std::map<GameKey, Provider> m_providers;
    std::map<GameKey, std::vector<providers::HelperCommand>> m_outbox;
    std::deque<std::pair<std::int64_t, providers::Observation>> m_queue;
    std::set<GameKey> m_rescanPending;
    std::map<ModIdentity, std::string> m_names;
    std::set<ModIdentity> m_metadataRequested;
    std::uint64_t m_pass = 0;
    bool m_draining = false;
    bool m_changed = false;
};

} // namespace pmm::services
