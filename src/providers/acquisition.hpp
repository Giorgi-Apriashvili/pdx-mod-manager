#pragma once

// Pure, deterministic state machine for acquiring one Steam Workshop item.
//
// The reducer takes the current job context plus one observation (something
// the provider reported, the clock advanced, or the user acted) and returns
// the next context plus the actions the driver must perform. It never does
// I/O and never reads the clock itself; time arrives through Tick and the
// `nowMs` argument of startAcquisition(), so every transition is reproducible
// in tests.
//
// Invariants:
//   - Installed is reached only through ContentVerified(ok) for the install
//     path Steam reported; "subscribed" and "download requested" are separate
//     states and never imply readiness.
//   - Every run (fresh start, restart after an interrupted session, retry)
//     queries the item state before deciding anything, so items that are
//     already installed are verified rather than re-downloaded, and a job
//     restored from the database is reconciled instead of re-subscribed.
//   - The machine never asks to unsubscribe or delete anything. Cancel only
//     stops application-managed work and says what remains in Steam.
//
//   start/resume --QuerySession--> ProviderReady --QueryState--> ItemState
//     not subscribed ............ Subscribe -> SubscribeResult(OK) -> QueryState
//     subscribed, not installed . RequestDownload -> DownloadAccepted -> poll QueryState
//     downloading / pending ..... poll QueryState on Tick (stall timeout -> Failed)
//     installed and current ..... VerifyContent(installPath) -> Installed | Failed
//
// The persisted part is the core AcquisitionJob; the rest of the context is
// transient and rebuilt on restart by calling startAcquisition() again.

#include "core/model.hpp"
#include "core/result.hpp"
#include "providers/helper_protocol.hpp"
#include "providers/workshop_item.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pmm::providers {

struct AcquisitionPolicy {
    int maxAutomaticAttempts = 3;              // runs per retry cycle, including the first
    std::int64_t retryBaseDelayMs = 5'000;     // doubled per attempt ...
    std::int64_t retryMaxDelayMs = 120'000;    // ... up to this cap
    std::int64_t responseTimeoutMs = 30'000;   // session, state, download request, verification
    std::int64_t subscribeTimeoutMs = 60'000;  // subscribe needs a server round trip
    std::int64_t stallTimeoutMs = 300'000;     // download without any progress
    std::int64_t pollIntervalMs = 1'000;       // QueryState cadence while downloading
    bool highPriorityDownloads = true;
};

// What the job is currently waiting for. Transient (not persisted).
enum class AwaitStep {
    Nothing,          // idle: terminal, or waiting for the user
    Session,          // QuerySession sent
    State,            // QueryState sent, deciding the next step
    SubscribeResult,  // Subscribe sent
    DownloadAccepted, // RequestDownload sent
    Progress,         // download requested/running; polling state
    Verification,     // VerifyContent sent
};
const char* toString(AwaitStep step) noexcept;

struct AcquisitionContext {
    AcquisitionJob job; // persisted part

    // Transient part, rebuilt by startAcquisition():
    AwaitStep awaiting = AwaitStep::Nothing;
    std::int64_t clockMs = 0;          // latest time seen (start or Tick)
    std::int64_t awaitingSinceMs = 0;  // when `awaiting` was entered
    std::int64_t lastProgressAtMs = 0; // last byte/flag change while downloading
    std::int64_t lastPollAtMs = 0;
    std::int64_t retryAtMs = 0;        // > 0: automatic retry scheduled
    bool sessionKnown = false;
    bool loggedOn = false;
    bool ownsApp = false;
    bool subscribeSent = false;      // this run sent Subscribe
    bool subscribeConfirmed = false; // this run saw SubscribeResult(OK) or the Subscribed flag
    bool downloadRequested = false;  // this run's RequestDownload was accepted
    std::uint32_t lastFlags = 0;
    std::string installPath; // path handed to VerifyContent
};

enum class ActionKind {
    None, // not emitted; an empty action list means "nothing to do"
    QuerySession,     // make sure the helper runs and report ProviderReady/Unavailable
    QueryState,       // helper "state" for itemId
    Subscribe,        // helper "subscribe" for itemId
    RequestDownload,  // helper "download" for itemId
    VerifyContent,    // check installPath (verifyWorkshopContent), report ContentVerified
    OpenFallbackPage, // open manualFallbackLinks(itemId) for the user
};
const char* toString(ActionKind kind) noexcept;

struct Action {
    ActionKind kind = ActionKind::None;
    std::string itemId;
    std::string installPath; // VerifyContent only (UTF-8, as reported by Steam)
    bool highPriority = true; // RequestDownload only

    bool operator==(const Action&) const = default;
};

namespace obs {

struct ProviderReady {
    bool loggedOn = false;
    bool ownsApp = false;
};

enum class UnavailableReason {
    SteamNotRunning,
    NotLoggedOn,
    InitFailed,     // Steam refused the session (ownership, wrong account, ...)
    LibraryMissing, // the game's Steam API library is missing or failed to load
    Incompatible,   // the library lacks the required API (symbols missing)
    HelperFailed,   // helper crashed or stopped responding: transient
};

struct ProviderUnavailable {
    UnavailableReason reason = UnavailableReason::InitFailed;
    std::string detail; // technical, for logs only
};

struct ItemState {
    ItemStatus status;
};

struct SubscribeResult {
    int result = 0; // EResult
};

struct DownloadAccepted {
    bool accepted = false;
};

struct ContentVerified {
    bool ok = false;
    std::string detail; // user-facing reason when !ok
};

// The item was rejected before acquisition (e.g. Web API: not found, banned,
// wrong game). `code` is usually ItemUnavailable or WrongGame.
struct ItemRejected {
    ErrorCode code = ErrorCode::ItemUnavailable;
    std::string message;
};

struct Tick {
    std::int64_t nowMs = 0;
};

struct Cancel {};
struct Retry {};          // user-initiated; starts a fresh retry cycle
struct ManualFallback {}; // user chose "Open in Steam (manual)"

} // namespace obs

using Observation =
    std::variant<obs::ProviderReady, obs::ProviderUnavailable, obs::ItemState,
                 obs::SubscribeResult, obs::DownloadAccepted, obs::ContentVerified,
                 obs::ItemRejected, obs::Tick, obs::Cancel, obs::Retry, obs::ManualFallback>;

struct Transition {
    AcquisitionContext context;
    std::vector<Action> actions;
    bool persist = false; // context.job changed and should be saved (progress may be coalesced)
    std::optional<std::int64_t> retryAfterMs; // set when an automatic retry was scheduled
};

// Begins or resumes a job (fresh, or loaded from the database after a
// restart). Terminal jobs yield no actions. Non-terminal jobs always start by
// establishing the session and querying the item state.
Transition startAcquisition(const AcquisitionJob& job, std::int64_t nowMs,
                            const AcquisitionPolicy& policy = {});

Transition reduceAcquisition(const AcquisitionContext& context, const Observation& observation,
                             const AcquisitionPolicy& policy = {});

// Backoff before automatic attempt `attempts + 1`: base * 2^(attempts-1), capped.
std::int64_t retryDelayMs(int attempts, const AcquisitionPolicy& policy) noexcept;

// --- Driver glue ---------------------------------------------------------------

// Maps a helper event to an observation for the job acquiring `itemId`.
// Events about other items, pongs and error replies yield nullopt.
std::optional<Observation> observationFromHelperEvent(const HelperEvent& event,
                                                      std::string_view itemId);

// Helper command for QueryState/Subscribe/RequestDownload; nullopt for the
// actions the driver performs itself (QuerySession, VerifyContent,
// OpenFallbackPage).
std::optional<HelperCommand> helperCommandFor(const Action& action);

// The agreed content check for VerifyContent (performs filesystem I/O): a
// directory must contain a non-empty regular "descriptor.mod"; a single file
// (legacy Workshop item) must be non-empty.
Status verifyWorkshopContent(const std::filesystem::path& installPath);

} // namespace pmm::providers
