#pragma once

// Drives the Workshop acquisition reducer (providers/acquisition.hpp) for the
// persisted acquisition jobs (spec section 6).
//
// The coordinator owns one AcquisitionContext per job it works on and one
// helper session per game. It never blocks (except closeSessionsForLaunch(),
// which waits for the closed helper to exit): the helper process and the Web
// API are reached through the host interfaces below, implemented by the Qt
// layer (QProcess, QNetworkAccessManager) and by fakes in tests, and their
// answers come back as onHelperEvent() / onHelperExited() / onMetadata()
// calls. Time advances only through tick(nowMs).
//
// Responsibilities:
//   - start(game) resumes every unfinished stored job of the game (restart
//     reconciliation: each run queries Steam before deciding anything, so
//     nothing is re-subscribed blindly and installed items are verified, not
//     downloaded again, except by an explicit update or re-download) and
//     picks up jobs created since (e.g. by an import). Update and re-download
//     jobs stay parked while the game runs;
//   - runs at most one job per mod: a job that starts supersedes the older,
//     finished in-memory jobs of the same mod, so that neither an automatic
//     retry nor a state report revives them next to it;
//   - opens the helper session on demand with the game's own Steam API library
//     (platform::steam::findSteamApiLibrary) and closes it as soon as no job
//     of the game is waiting for Steam;
//   - maps helper events with observationFromHelperEvent() and actions with
//     helperCommandFor(), batching state queries into one command;
//   - with a metadata host, holds every Subscribe, RequestDownload and
//     Unsubscribe until the Web API has confirmed that the item exists and
//     belongs to the game (share codes are untrusted input); state queries
//     and the verification of content Steam already installed go ahead, and
//     so does the download of an update Steam's own Workshop manifest flagged
//     for an installed item (it works without the Web API). Without a
//     verdict within identityTimeoutMs the job needs the user; Retry asks the
//     Web API again;
//   - keeps, for the session, the Web API's answers about installed items
//     (the update check: prepareUpdateCheck(), recordUpdateCheck()) and
//     compares them with the installed versions on demand
//     (workshopFindings()); creates update jobs (acquireUpdates()) and
//     re-download jobs (reinstall()), refusing both while the game runs;
//   - verifies reported content with the game adapter's
//     verifyWorkshopContent(), the rule of its own scan;
//   - persists jobs when the reducer says so (progress-only changes are
//     coalesced to one write per progressPersistIntervalMs);
//   - rescans the game's inventory when an item becomes Installed, so that
//     readiness flips to Ready only once the inventory has seen the content;
//   - reports provider status (Steam not running, not signed in, not owned,
//     unavailable) and, for jobs that need the user, the clearly labelled
//     manual Workshop page fallback (providers/fallback.hpp).
// Nothing here deletes Workshop content. Only a re-download the user asked
// for (reinstall()) unsubscribes, once per job; see providers/acquisition.hpp.
//
// Not thread-safe: call every method from the services worker thread. Host
// callbacks made re-entrantly (from inside open/send/close) are queued and
// processed before the outer call returns.

#include "core/model.hpp"
#include "core/readiness.hpp"
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
#include <string_view>
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
    // Blocks until the helper processes closed for `gameKey` have exited,
    // bounded by the host's grace period plus a short kill wait: a helper
    // still running then is killed (and a warning logged). Never delivers
    // events or exits. Meant for the launch path: Steam should no longer see
    // the game's app id when the game starts. The default returns at once.
    virtual void waitUntilClosed(const GameKey& /*gameKey*/) {}
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
    // The update check asks the Web API again only about items whose answer
    // is older than this (pressing the button repeatedly sends nothing new).
    std::int64_t updateCheckMaxAgeMs = 300'000;
    // pmm-steam-helper; when set, a missing file is reported before trying
    // to open a session. Empty: not checked (the host knows the path).
    std::filesystem::path helperExecutable;
    std::int64_t progressPersistIntervalMs = 2'000;
    // Content check for VerifyContent; defaults to the game adapter's
    // verifyWorkshopContent().
    std::function<Status(const GameKey&, const std::filesystem::path&)> verifyContent;
    // With a metadata host, how long a subscribe or download request waits
    // for the Web API to confirm the item before the job needs the user.
    std::int64_t identityTimeoutMs = 15'000;
};

struct JobSnapshot {
    AcquisitionJob job;
    std::string name;       // Web API title > playset name hint > provider id
    std::string stateText;  // "Queued", "Downloading 45 %", "Failed", ...
    double progress = -1.0; // 0..1 while the size is known, otherwise -1
    bool active = false;    // the coordinator is waiting for Steam on its behalf
    bool canRetry = false;
    bool canOpenFallback = false;
    // A failed update of an installed item: "Re-download from scratch..."
    // (reinstallJob()) is offered next to Retry.
    bool canReinstall = false;
    // Manual Workshop page (label: providers::kManualFallbackLabel); set when
    // canOpenFallback. A limited fallback: nothing is downloaded for the user.
    std::optional<providers::ManualFallbackLinks> fallback;
};

struct AcquisitionSnapshot {
    GameKey game;
    // Jobs worked on in this session plus stored jobs that need attention
    // (failed, needs user action, unavailable), newest job per mod only. Idle
    // acquire jobs whose mod a rescan has since found installed (for example
    // after the manual fallback) are left out: usable installed content wins.
    // Update and re-download jobs stay, failed or running.
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

// What the update check asks the Web API about (prepareUpdateCheck()).
struct UpdateCheckPlan {
    GameKey game;
    // The playset's installed Workshop mods, enabled and disabled, in
    // playset order, without an answer younger than updateCheckMaxAgeMs.
    // Only these ids are sent; ask in batches of
    // providers::kMaxPublishedFileDetailsItems.
    std::vector<std::string> ids;
    std::size_t installed = 0; // installed Workshop mods of the playset
};

// One installed Workshop mod of the playset in the update check.
struct UpdateCheckItem {
    ModIdentity identity;
    std::string name;      // installed descriptor name > name hint > id
    bool enabled = true;   // in the playset
    bool answered = false; // the Web API answered (now, or within updateCheckMaxAgeMs)
    providers::WorkshopUpdateVerdict verdict = providers::WorkshopUpdateVerdict::NotCompared;
    // Steam's own Workshop manifest says a newer version exists (no network).
    bool steamFlagged = false;
    // Verdict UpdateAvailable, or flagged by Steam and not refused: the row
    // shows "Update available" and acquireUpdates() takes it.
    bool updateAvailable = false;
    std::int64_t installedTimeUpdated = 0; // unix seconds; 0: not Steam's record
    std::int64_t workshopTimeUpdated = 0;  // unix seconds; 0: unknown
    std::string installedManifest;
    std::string workshopManifest;
    std::string message; // not answered / not compared / refused: why
};

struct UpdateCheckReport {
    CoordinatorUpdate update;
    GameKey game;
    std::int64_t playsetId = 0;
    std::vector<UpdateCheckItem> items; // playset order
    std::size_t checked = 0;            // answered
    std::size_t updates = 0;            // updateAvailable
    std::size_t notCompared = 0;
    std::size_t refused = 0;
    std::size_t unanswered = 0;
    // False when nothing could be asked or answered although there was
    // something to check (offline, Web API unreachable): offer Retry.
    bool reachable = true;
    // "Checked 12 Workshop mods: 3 have an update." / "All 12 Workshop mods
    // are up to date." / "The Steam Workshop could not be reached, ..."
    std::string summary;
};

// A mod acquireUpdates() or reinstall() did not queue, and why.
struct SkippedUpdate {
    ModIdentity identity;
    std::string name;
    std::string reason;
};

struct UpdateOutcome {
    CoordinatorUpdate update;
    std::vector<std::int64_t> jobIds; // queued, or already running for the mod
    std::vector<SkippedUpdate> skipped;
    std::string message; // one line for a notification
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

    // --- Updates of installed Workshop mods ---------------------------------

    // What the update check of the playset asks the Web API about. NotFound
    // for an unknown playset.
    Result<UpdateCheckPlan> prepareUpdateCheck(std::int64_t playsetId) const;
    // Keeps the Web API's answers for the session (they also confirm or
    // refuse items for the identity gate and running jobs, as onMetadata()
    // does) and reports every installed Workshop mod of the playset against
    // them. `asked` is the number of ids the caller asked about; when none of
    // them was answered the report is not reachable.
    Result<UpdateCheckReport>
    recordUpdateCheck(std::int64_t playsetId,
                      const std::vector<providers::WorkshopItemMetadata>& answers,
                      std::size_t asked);
    // The report from what is known now, without asking anything.
    Result<UpdateCheckReport> updateCheckReport(std::int64_t playsetId) const;
    // The session's answers compared with the current inventory of `game`.
    WorkshopFindings workshopFindings(const GameKey& game) const;
    // Update jobs for `ids` (provider ids of the playset's installed Workshop
    // mods), or for every mod of the playset with an update available when
    // `ids` is empty (disabled ones included), then start(). Mods without a
    // known update, not installed, or refused by the Workshop are skipped
    // with a reason; a mod with an unfinished job keeps that job. Refused with
    // ConflictingProcess while the game runs, and NotFound for an unknown
    // playset.
    Result<UpdateOutcome> acquireUpdates(std::int64_t playsetId,
                                         const std::vector<std::string>& ids = {});
    // A re-download from scratch of one installed Workshop mod of the playset
    // (the user confirmed it): unsubscribe, subscribe again, download. Needs a
    // metadata host (the Web API must confirm the item still exists before it
    // is unsubscribed). Refused with ConflictingProcess while the game runs,
    // AlreadyExists while another job for the mod is unfinished,
    // ItemUnavailable when the Workshop refused the item, InvalidArgument for
    // a mod that is not an installed Workshop mod of the playset.
    Result<UpdateOutcome> reinstall(std::int64_t playsetId, const std::string& providerId);
    // The same for the mod of job `jobId` (a failed update: canReinstall).
    Result<UpdateOutcome> reinstallJob(std::int64_t jobId);
    // Mods of `game` with an update or re-download job that is unfinished or
    // waiting for its automatic retry, stored or in memory: launching waits.
    std::vector<ModIdentity> updatesInProgress(const GameKey& game) const;

    CoordinatorUpdate onHelperEvent(const GameKey& game, const providers::HelperEvent& event);
    CoordinatorUpdate onHelperExited(const GameKey& game, int exitCode);
    CoordinatorUpdate onMetadata(const GameKey& game,
                                 const std::vector<providers::WorkshopItemMetadata>& items);
    CoordinatorUpdate tick(std::int64_t nowMs);

    // User-initiated retry of a failed / needs-action / unavailable /
    // cancelled job (a fresh retry cycle). AlreadyExists when another job for
    // the same mod is in progress or waiting for its automatic retry;
    // ConflictingProcess for an update or re-download while the game runs.
    Result<CoordinatorUpdate> retry(std::int64_t jobId);
    // Retries the newest job of every mod of `game` that failed or needs the user.
    CoordinatorUpdate retryAllFailed(const GameKey& game);
    // Stops application-managed work for `game`. Never unsubscribes or
    // deletes; the message names items a cancelled re-download left
    // unsubscribed.
    CancelOutcome cancelAll(const GameKey& game);
    // Marks the job as waiting for the user and returns the manual Workshop
    // page links for the caller to open.
    Result<FallbackOutcome> openFallback(std::int64_t jobId);

    // Before launching `game`: persists progress, closes its helper session
    // (Steam would otherwise consider the game already running), waits until
    // the closed helper has exited (WorkshopSessionHost::waitUntilClosed) and
    // parks its unfinished jobs; the next start(game) reconciles them with
    // Steam. A failed job whose automatic retry is due is parked as the
    // queued run the retry would have started.
    CoordinatorUpdate closeSessionsForLaunch(const GameKey& game);
    // On application exit: persists progress, saves failed jobs whose
    // automatic retry is due as queued (start(game) resumes them after the
    // restart) and closes every session.
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
        bool dirty = false;   // coalesced progress not written yet
        bool stopped = false; // a duplicate of another unfinished job; forgotten after the pass
        // Set while a subscribe/download waits for the Web API verdict.
        std::optional<std::int64_t> heldSinceMs;
    };
    // A Web API answer about an item, kept for the session.
    struct Answer {
        providers::WorkshopItemMetadata item;
        std::int64_t atMs = 0;
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
    // False when the write showed that another unfinished job owns the mod;
    // the runtime is then stopped at its last saved state.
    bool persist(Runtime& runtime, bool force);
    void park(Runtime& runtime);
    void setProvider(const GameKey& game, ProviderStatus status, std::string message);
    Runtime* adopt(const AcquisitionJob& job, bool resume);
    void dropSuperseded(std::int64_t jobId);
    void requestMetadata(const GameKey& game, const std::vector<std::string>& ids);
    // Requests verdicts for Workshop identities with neither a verdict nor a
    // request in flight.
    void requestVerdicts(const GameKey& game, const std::vector<ModIdentity>& identities);
    // Drops a missing or negative verdict so that the next request asks again.
    bool forgetUnconfirmed(const ModIdentity& identity);
    bool holdForIdentity(std::int64_t jobId, Runtime& runtime,
                         const providers::Transition& transition);
    void failUnconfirmed(Runtime& runtime);
    Result<CoordinatorUpdate> retryJob(std::int64_t jobId, bool recheckIdentity);
    // Records Web API answers (names, verdicts, the session's answers) and
    // applies the verdicts to waiting and held jobs. Inside run() only.
    void absorbMetadata(const GameKey& game,
                        const std::vector<providers::WorkshopItemMetadata>& items);
    Result<UpdateCheckReport> buildReport(std::int64_t playsetId) const;
    // ConflictingProcess when the game runs (or its processes cannot be
    // listed), for work that changes installed mods.
    Status refuseWhileRunning(const GameKey& game, std::string_view what) const;
    Result<UpdateOutcome> queueReinstall(const ModIdentity& identity,
                                         std::optional<std::int64_t> playsetId,
                                         const std::string& name);
    // Whether Steam's Workshop manifest flagged an update of the installed item.
    bool steamFlaggedUpdate(const ModIdentity& identity) const;
    void rememberNames(const std::vector<AcquisitionJob>& jobs);
    std::optional<GameInstallation> storedInstallation(const GameKey& game) const;
    std::string appIdFor(const GameKey& game) const;
    std::string nameFor(const AcquisitionJob& job) const;
    bool busy(const Runtime& runtime) const noexcept;
    // Unfinished, or Failed with an automatic retry due.
    bool inProgress(const Runtime& runtime) const noexcept;

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
    std::set<ModIdentity> m_metadataRequested; // Web API requests in flight
    std::map<ModIdentity, Status> m_verdicts;  // Web API: ok, or why it cannot be acquired
    std::map<ModIdentity, Answer> m_answers;   // Web API answers of this session
    std::uint64_t m_pass = 0;
    bool m_draining = false;
    bool m_changed = false;
};

} // namespace pmm::services
