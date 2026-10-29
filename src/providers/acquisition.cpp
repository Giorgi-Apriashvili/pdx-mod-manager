#include "providers/acquisition.hpp"

#include <algorithm>
#include <system_error>
#include <type_traits>

namespace pmm::providers {
namespace {

namespace fs = std::filesystem;

std::string seconds(std::int64_t ms) {
    return std::to_string((ms + 999) / 1000) + " s";
}

std::string minutes(std::int64_t ms) {
    const std::int64_t m = std::max<std::int64_t>(1, (ms + 59'999) / 60'000);
    return std::to_string(m) + (m == 1 ? " minute" : " minutes");
}

// Applies one observation to a copy of the context. Each handler leaves the
// machine in a consistent (state, awaiting) pair.
class Reducer {
public:
    Reducer(const AcquisitionContext& context, const AcquisitionPolicy& policy)
        : m_policy(policy) {
        m_t.context = context;
    }

    Transition take() && { return std::move(m_t); }

    void start() {
        if (isTerminal(job().state))
            return;
        if (!identityAcquirable())
            return;
        if (job().attempts == 0) {
            job().attempts = 1;
            touch();
        }
        beginRun();
    }

    void on(const obs::ProviderReady& o) {
        ctx().sessionKnown = true;
        ctx().loggedOn = o.loggedOn;
        ctx().ownsApp = o.ownsApp;
        switch (ctx().awaiting) {
        case AwaitStep::Nothing:
        case AwaitStep::Verification:
            return;
        default:
            // A (re)started helper lost any request in flight: re-query.
            queryState();
        }
    }

    void on(const obs::ProviderUnavailable& o) {
        ctx().sessionKnown = false;
        const AwaitStep step = ctx().awaiting;
        if (step == AwaitStep::Nothing || step == AwaitStep::Verification)
            return;
        using R = obs::UnavailableReason;
        switch (o.reason) {
        case R::SteamNotRunning:
            needsUser(ErrorCode::ProviderUnavailable,
                      "Steam is not running. Start Steam, sign in, and retry.");
            return;
        case R::NotLoggedOn:
            needsUser(ErrorCode::ProviderUnavailable,
                      "Steam is not signed in. Sign in to Steam and retry.");
            return;
        case R::InitFailed:
            needsUser(ErrorCode::ProviderUnavailable,
                      "Steam refused to open a Workshop session for this game. Make sure Steam "
                      "is running and signed in to the account that owns the game, then retry.");
            return;
        case R::LibraryMissing:
            needsUser(ErrorCode::ProviderUnavailable,
                      "The game's Steam API library is missing or could not be loaded. Verify "
                      "the game files in Steam and retry, or use the manual Workshop page.");
            return;
        case R::Incompatible:
            needsUser(ErrorCode::ProviderUnavailable,
                      "This game's Steam API library does not provide what automatic Workshop "
                      "downloads need. Use the manual Workshop page, then rescan.");
            return;
        case R::HelperFailed:
            failRetryable(ErrorCode::ProviderUnavailable,
                          "The Steam helper stopped unexpectedly.");
            return;
        }
    }

    void on(const obs::ItemState& o) {
        if (o.status.id != job().identity.id)
            return;
        const std::uint32_t previousFlags = ctx().lastFlags;
        ctx().lastFlags = o.status.flags;
        switch (ctx().awaiting) {
        case AwaitStep::State:
            plan(o.status);
            return;
        case AwaitStep::Progress:
            progress(o.status, previousFlags);
            return;
        case AwaitStep::Nothing:
            rescan(o.status);
            return;
        default:
            return; // stale answer to an earlier query
        }
    }

    void on(const obs::SubscribeResult& o) {
        if (ctx().awaiting != AwaitStep::SubscribeResult)
            return;
        const SteamResultInfo info = classifySteamResult(o.result);
        if (info.ok()) {
            ctx().subscribeConfirmed = true;
            setState(AcquisitionState::Subscribed);
            clearError();
            queryState();
            return;
        }
        failWith(o.result, "Subscribing to Workshop item " + job().identity.id);
    }

    void on(const obs::DownloadAccepted& o) {
        if (ctx().awaiting != AwaitStep::DownloadAccepted)
            return;
        if (!o.accepted) {
            failRetryable(ErrorCode::ProviderUnavailable,
                          "Steam did not accept the download request for Workshop item " +
                              job().identity.id + ".");
            return;
        }
        ctx().downloadRequested = true;
        setState(AcquisitionState::DownloadRequested);
        clearError();
        enterProgress();
        emit(ActionKind::QueryState);
    }

    void on(const obs::ContentVerified& o) {
        if (ctx().awaiting != AwaitStep::Verification)
            return;
        idle();
        if (o.ok) {
            setState(AcquisitionState::Installed);
            clearError();
            if (job().bytesTotal > 0 && job().bytesDownloaded != job().bytesTotal) {
                job().bytesDownloaded = job().bytesTotal;
                touch();
            }
            return;
        }
        setState(AcquisitionState::Failed);
        setError(ErrorCode::ValidationFailed,
                 "Steam reports Workshop item " + job().identity.id +
                     " as installed, but its content is not usable (" + o.detail +
                     "). Verify the game files in Steam, or unsubscribe and resubscribe there, "
                     "then retry.");
    }

    void on(const obs::ItemRejected& o) {
        const AcquisitionState s = job().state;
        if (s == AcquisitionState::Installed || s == AcquisitionState::Cancelled)
            return;
        idle();
        setState(AcquisitionState::Unavailable);
        setError(o.code, o.message);
    }

    void on(const obs::Tick& o) {
        if (o.nowMs > ctx().clockMs)
            ctx().clockMs = o.nowMs;
        const std::int64_t now = ctx().clockMs;
        const std::int64_t waited = now - ctx().awaitingSinceMs;
        switch (ctx().awaiting) {
        case AwaitStep::Progress:
            if (now - ctx().lastProgressAtMs >= m_policy.stallTimeoutMs) {
                failRetryable(ErrorCode::Timeout,
                              "The download of Workshop item " + job().identity.id +
                                  " made no progress for " + minutes(m_policy.stallTimeoutMs) +
                                  ". Check Steam's Downloads page (paused downloads, disk space, "
                                  "download restrictions), then retry.");
            } else if (now - ctx().lastPollAtMs >= m_policy.pollIntervalMs) {
                ctx().lastPollAtMs = now;
                emit(ActionKind::QueryState);
            }
            return;
        case AwaitStep::SubscribeResult:
            if (waited >= m_policy.subscribeTimeoutMs)
                failRetryable(ErrorCode::Timeout,
                              "Steam did not confirm the subscription in time.");
            return;
        case AwaitStep::Session:
            if (waited >= m_policy.responseTimeoutMs)
                failRetryable(ErrorCode::Timeout, "The Steam helper did not become ready in time.");
            return;
        case AwaitStep::State:
        case AwaitStep::DownloadAccepted:
            if (waited >= m_policy.responseTimeoutMs)
                failRetryable(ErrorCode::Timeout, "Steam did not answer in time.");
            return;
        case AwaitStep::Verification:
            if (waited >= m_policy.responseTimeoutMs)
                failRetryable(ErrorCode::Timeout,
                              "Checking the downloaded content did not finish in time.");
            return;
        case AwaitStep::Nothing:
            if (job().state == AcquisitionState::Failed && ctx().retryAtMs > 0 &&
                now >= ctx().retryAtMs) {
                ctx().retryAtMs = 0;
                ++job().attempts;
                setState(AcquisitionState::Pending);
                clearError();
                beginRun();
            }
            return;
        }
    }

    void on(const obs::Cancel&) {
        const AcquisitionState s = job().state;
        if (s == AcquisitionState::Installed || s == AcquisitionState::Cancelled)
            return;
        const bool downloadAsked = ctx().downloadRequested ||
                                   s == AcquisitionState::DownloadRequested ||
                                   s == AcquisitionState::Downloading;
        const bool subscribed = ctx().subscribeConfirmed || s == AcquisitionState::Subscribed;
        const bool subscribeMaybe = ctx().subscribeSent || s == AcquisitionState::Subscribing;
        idle();
        setState(AcquisitionState::Cancelled);
        std::string message;
        if (downloadAsked) {
            message = "Cancelled. Steam keeps the subscription and may finish the download that "
                      "was already requested; unsubscribe in Steam if you no longer want this "
                      "item.";
        } else if (subscribed) {
            message = "Cancelled. The Steam subscription made for this item remains; "
                      "unsubscribe in Steam if you no longer want it.";
        } else if (subscribeMaybe) {
            message = "Cancelled. A subscription request was already sent and may still complete "
                      "in Steam; unsubscribe there if you no longer want this item.";
        } else {
            message = "Cancelled before anything was requested from Steam.";
        }
        setError(ErrorCode::Cancelled, std::move(message));
    }

    void on(const obs::Retry&) {
        switch (job().state) {
        case AcquisitionState::Failed:
        case AcquisitionState::NeedsUserAction:
        case AcquisitionState::Unavailable:
        case AcquisitionState::Cancelled:
            break;
        default:
            return; // in progress or already installed
        }
        idle();
        job().attempts = 1;
        setState(AcquisitionState::Pending);
        clearError();
        if (!identityAcquirable())
            return;
        beginRun();
    }

    void on(const obs::ManualFallback&) {
        if (job().state == AcquisitionState::Installed)
            return;
        idle();
        emit(ActionKind::OpenFallbackPage);
        setState(AcquisitionState::NeedsUserAction);
        job().errorCode.clear();
        job().errorMessage = "Opened the Workshop page for item " + job().identity.id +
                             ". Subscribe there and let Steam finish the download, then rescan. "
                             "(Manual fallback: this application does not download the item "
                             "itself.)";
        touch();
    }

private:
    AcquisitionContext& ctx() { return m_t.context; }
    AcquisitionJob& job() { return m_t.context.job; }

    void touch() {
        job().updatedAt = ctx().clockMs;
        m_t.persist = true;
    }

    void setState(AcquisitionState s) {
        if (job().state != s) {
            job().state = s;
            touch();
        }
    }

    void setError(ErrorCode code, std::string message) {
        job().errorCode = std::string(errorCodeName(code));
        job().errorMessage = std::move(message);
        touch();
    }

    void clearError() {
        if (!job().errorCode.empty() || !job().errorMessage.empty()) {
            job().errorCode.clear();
            job().errorMessage.clear();
            touch();
        }
    }

    void await(AwaitStep step) {
        ctx().awaiting = step;
        ctx().awaitingSinceMs = ctx().clockMs;
    }

    void idle() {
        ctx().awaiting = AwaitStep::Nothing;
        ctx().retryAtMs = 0;
    }

    void emit(ActionKind kind) {
        Action a;
        a.kind = kind;
        a.itemId = job().identity.id;
        if (kind == ActionKind::VerifyContent)
            a.installPath = ctx().installPath;
        a.highPriority = m_policy.highPriorityDownloads;
        m_t.actions.push_back(std::move(a));
    }

    void queryState() {
        await(AwaitStep::State);
        emit(ActionKind::QueryState);
    }

    void enterProgress() {
        await(AwaitStep::Progress);
        ctx().lastProgressAtMs = ctx().clockMs;
        ctx().lastPollAtMs = ctx().clockMs;
    }

    void verify(const std::string& installPath) {
        switch (job().state) {
        case AcquisitionState::Subscribed:
        case AcquisitionState::DownloadRequested:
        case AcquisitionState::Downloading:
            break;
        default:
            // Steam reports the item subscribed and installed; readiness still
            // depends on the content check.
            setState(AcquisitionState::Subscribed);
            break;
        }
        clearError();
        ctx().installPath = installPath;
        await(AwaitStep::Verification);
        emit(ActionKind::VerifyContent);
    }

    void needsUser(ErrorCode code, std::string message) {
        idle();
        setState(AcquisitionState::NeedsUserAction);
        setError(code, std::move(message));
    }

    void failRetryable(ErrorCode code, std::string message) {
        idle();
        setState(AcquisitionState::Failed);
        const int attempts = std::max(1, job().attempts);
        if (attempts < m_policy.maxAutomaticAttempts) {
            const std::int64_t delay = retryDelayMs(attempts, m_policy);
            ctx().retryAtMs = ctx().clockMs + delay;
            m_t.retryAfterMs = delay;
            message += " Retrying automatically in " + seconds(delay) + " (attempt " +
                       std::to_string(attempts + 1) + " of " +
                       std::to_string(m_policy.maxAutomaticAttempts) + ").";
        } else {
            message += " Automatic retries are exhausted; use Retry, or open the item's "
                       "Workshop page manually.";
        }
        setError(code, std::move(message));
    }

    void failWith(int eresult, const std::string& context) {
        const SteamResultInfo info = classifySteamResult(eresult);
        Error error = steamResultError(eresult, context);
        switch (info.disposition) {
        case FailureDisposition::Unavailable:
            idle();
            setState(AcquisitionState::Unavailable);
            setError(error.code, std::move(error.message));
            return;
        case FailureDisposition::UserAction:
            needsUser(error.code, std::move(error.message));
            return;
        case FailureDisposition::None:
        case FailureDisposition::Retryable:
            failRetryable(error.code, std::move(error.message));
            return;
        }
    }

    bool identityAcquirable() {
        const ModIdentity& id = job().identity;
        if (id.source != source::SteamWorkshop) {
            needsUser(ErrorCode::ProviderUnavailable,
                      "Local mods cannot be downloaded automatically. Install the mod folder "
                      "yourself and map it to this entry.");
            return false;
        }
        if (!isValidPublishedFileId(id.id)) {
            idle();
            setState(AcquisitionState::Unavailable);
            setError(ErrorCode::InvalidArgument,
                     "\"" + id.id.substr(0, 64) + "\" is not a valid Workshop item id.");
            return false;
        }
        return true;
    }

    void beginRun() {
        ctx().subscribeSent = false;
        ctx().subscribeConfirmed = false;
        ctx().downloadRequested = false;
        ctx().installPath.clear();
        ctx().retryAtMs = 0;
        if (ctx().sessionKnown) {
            queryState();
        } else {
            await(AwaitStep::Session);
            emit(ActionKind::QuerySession);
        }
    }

    // Network operations need a signed-in session that owns the game.
    bool requireOnline() {
        if (!ctx().loggedOn) {
            needsUser(ErrorCode::ProviderUnavailable,
                      "Steam is offline or not signed in, so Workshop items cannot be "
                      "downloaded. Switch Steam to online mode, sign in, and retry.");
            return false;
        }
        if (!ctx().ownsApp) {
            needsUser(ErrorCode::AccessDenied,
                      "The signed-in Steam account does not own this game, so Steam will not "
                      "download its Workshop items. Sign in with the account that owns it and "
                      "retry.");
            return false;
        }
        return true;
    }

    void recordBytes(const ItemStatus& s, bool& changed) {
        if (s.bytesDownloaded != job().bytesDownloaded || s.bytesTotal != job().bytesTotal) {
            job().bytesDownloaded = s.bytesDownloaded;
            job().bytesTotal = s.bytesTotal;
            touch();
            changed = true;
        }
    }

    void trackDownload(const ItemStatus& s, const ItemFlags& f) {
        if (f.downloading)
            setState(AcquisitionState::Downloading);
        else if (f.downloadPending)
            setState(AcquisitionState::DownloadRequested);
        bool changed = false;
        recordBytes(s, changed);
    }

    // Decides the next step from a fresh state report.
    void plan(const ItemStatus& s) {
        const ItemFlags f = decodeItemFlags(s.flags);
        if (f.subscribed)
            ctx().subscribeConfirmed = true;
        const bool subscribed = ctx().subscribeConfirmed;

        if (subscribed && f.installedAndCurrent() && !s.installPath.empty()) {
            verify(s.installPath);
            return;
        }
        if (!subscribed) {
            if (!requireOnline())
                return;
            setState(AcquisitionState::Subscribing);
            clearError();
            ctx().subscribeSent = true;
            await(AwaitStep::SubscribeResult);
            emit(ActionKind::Subscribe);
            return;
        }
        if (job().state == AcquisitionState::Pending || job().state == AcquisitionState::Subscribing)
            setState(AcquisitionState::Subscribed);
        if (f.downloading || f.downloadPending) {
            enterProgress();
            trackDownload(s, f);
            return;
        }
        if (ctx().downloadRequested) {
            enterProgress(); // accepted, but Steam has not queued it yet
            return;
        }
        if (!requireOnline())
            return;
        clearError();
        await(AwaitStep::DownloadAccepted);
        emit(ActionKind::RequestDownload);
    }

    void progress(const ItemStatus& s, std::uint32_t previousFlags) {
        const ItemFlags f = decodeItemFlags(s.flags);
        if (f.installedAndCurrent() && !s.installPath.empty()) {
            verify(s.installPath);
            return;
        }
        if (!f.subscribed && !f.downloading && !f.downloadPending) {
            // Steam will not download an item nobody is subscribed to.
            needsUser(ErrorCode::ValidationFailed,
                      "Workshop item " + job().identity.id +
                          " is no longer subscribed in Steam (it may have been unsubscribed "
                          "manually). Retry to subscribe again.");
            return;
        }
        if (f.downloading)
            setState(AcquisitionState::Downloading);
        else if (f.downloadPending)
            setState(AcquisitionState::DownloadRequested);
        bool changed = s.flags != previousFlags;
        recordBytes(s, changed);
        if (changed)
            ctx().lastProgressAtMs = ctx().clockMs;
    }

    // State reports while idle come from rescans (e.g. after the manual
    // fallback). Completed content is verified; a running download is tracked.
    void rescan(const ItemStatus& s) {
        switch (job().state) {
        case AcquisitionState::Failed:
        case AcquisitionState::NeedsUserAction:
        case AcquisitionState::Unavailable:
            break;
        default:
            return;
        }
        const ItemFlags f = decodeItemFlags(s.flags);
        if (!f.subscribed)
            return;
        if (f.installedAndCurrent() && !s.installPath.empty()) {
            ctx().retryAtMs = 0;
            ctx().subscribeConfirmed = true;
            verify(s.installPath);
            return;
        }
        if (f.downloading || f.downloadPending) {
            ctx().retryAtMs = 0;
            ctx().subscribeConfirmed = true;
            clearError();
            enterProgress();
            trackDownload(s, f);
        }
    }

    Transition m_t;
    const AcquisitionPolicy& m_policy;
};

} // namespace

const char* toString(AwaitStep step) noexcept {
    switch (step) {
    case AwaitStep::Nothing:
        return "nothing";
    case AwaitStep::Session:
        return "session";
    case AwaitStep::State:
        return "state";
    case AwaitStep::SubscribeResult:
        return "subscribe_result";
    case AwaitStep::DownloadAccepted:
        return "download_accepted";
    case AwaitStep::Progress:
        return "progress";
    case AwaitStep::Verification:
        return "verification";
    }
    return "nothing";
}

const char* toString(ActionKind kind) noexcept {
    switch (kind) {
    case ActionKind::None:
        return "none";
    case ActionKind::QuerySession:
        return "query_session";
    case ActionKind::QueryState:
        return "query_state";
    case ActionKind::Subscribe:
        return "subscribe";
    case ActionKind::RequestDownload:
        return "request_download";
    case ActionKind::VerifyContent:
        return "verify_content";
    case ActionKind::OpenFallbackPage:
        return "open_fallback_page";
    }
    return "none";
}

std::int64_t retryDelayMs(int attempts, const AcquisitionPolicy& policy) noexcept {
    std::int64_t delay = std::max<std::int64_t>(0, policy.retryBaseDelayMs);
    for (int i = 1; i < attempts && delay < policy.retryMaxDelayMs; ++i)
        delay *= 2;
    return std::min(delay, policy.retryMaxDelayMs);
}

Transition startAcquisition(const AcquisitionJob& job, std::int64_t nowMs,
                            const AcquisitionPolicy& policy) {
    AcquisitionContext context;
    context.job = job;
    context.clockMs = nowMs;
    context.awaitingSinceMs = nowMs;
    Reducer r(context, policy);
    r.start();
    return std::move(r).take();
}

Transition reduceAcquisition(const AcquisitionContext& context, const Observation& observation,
                             const AcquisitionPolicy& policy) {
    Reducer r(context, policy);
    std::visit([&r](const auto& o) { r.on(o); }, observation);
    return std::move(r).take();
}

std::optional<Observation> observationFromHelperEvent(const HelperEvent& event,
                                                      std::string_view itemId) {
    return std::visit(
        [itemId](const auto& e) -> std::optional<Observation> {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, ReadyEvent>) {
                if (e.ok)
                    return obs::ProviderReady{e.loggedOn, e.ownsApp};
                using R = obs::UnavailableReason;
                R reason = R::InitFailed;
                switch (e.error) {
                case HelperFailure::SteamNotRunning:
                    reason = R::SteamNotRunning;
                    break;
                case HelperFailure::NotLoggedOn:
                    reason = R::NotLoggedOn;
                    break;
                case HelperFailure::LibraryLoadFailed:
                    reason = R::LibraryMissing;
                    break;
                case HelperFailure::SymbolsMissing:
                    reason = R::Incompatible;
                    break;
                case HelperFailure::InvalidArguments:
                    reason = R::HelperFailed;
                    break;
                case HelperFailure::None:
                case HelperFailure::InitFailed:
                    reason = R::InitFailed;
                    break;
                }
                return obs::ProviderUnavailable{reason, e.message};
            } else if constexpr (std::is_same_v<T, ItemStateEvent>) {
                if (e.status.id == itemId)
                    return obs::ItemState{e.status};
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, SubscribeResultEvent>) {
                if (e.id == itemId)
                    return obs::SubscribeResult{e.result};
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, DownloadRequestedEvent>) {
                if (e.id == itemId)
                    return obs::DownloadAccepted{e.accepted};
                return std::nullopt;
            } else {
                return std::nullopt;
            }
        },
        event);
}

std::optional<HelperCommand> helperCommandFor(const Action& action) {
    switch (action.kind) {
    case ActionKind::QueryState:
        return HelperCommand{StateCommand{{action.itemId}}};
    case ActionKind::Subscribe:
        return HelperCommand{SubscribeCommand{action.itemId}};
    case ActionKind::RequestDownload:
        return HelperCommand{DownloadCommand{action.itemId, action.highPriority}};
    case ActionKind::None:
    case ActionKind::QuerySession:
    case ActionKind::VerifyContent:
    case ActionKind::OpenFallbackPage:
        return std::nullopt;
    }
    return std::nullopt;
}

Status verifyWorkshopContent(const fs::path& installPath) {
    const auto u8 = installPath.u8string();
    const std::string shown(u8.begin(), u8.end());
    std::error_code ec;
    const auto st = fs::status(installPath, ec);
    if (ec || !fs::exists(st)) {
        return makeError(ErrorCode::NotFound,
                         "the content folder reported by Steam does not exist", shown);
    }
    if (fs::is_regular_file(st)) {
        const auto size = fs::file_size(installPath, ec);
        if (ec || size == 0)
            return makeError(ErrorCode::ValidationFailed, "the downloaded file is empty", shown);
        return {};
    }
    if (!fs::is_directory(st)) {
        return makeError(ErrorCode::ValidationFailed,
                         "the content path is neither a folder nor a file", shown);
    }
    const fs::path descriptor = installPath / "descriptor.mod";
    const auto dst = fs::status(descriptor, ec);
    if (ec || !fs::is_regular_file(dst)) {
        return makeError(ErrorCode::ValidationFailed,
                         "descriptor.mod is missing from the downloaded content", shown);
    }
    const auto size = fs::file_size(descriptor, ec);
    if (ec || size == 0)
        return makeError(ErrorCode::ValidationFailed, "descriptor.mod is empty", shown);
    return {};
}

} // namespace pmm::providers
