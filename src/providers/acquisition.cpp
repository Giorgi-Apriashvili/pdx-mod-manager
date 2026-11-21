#include "providers/acquisition.hpp"

#include <algorithm>
#include <type_traits>

namespace pmm::providers {
namespace {

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
    Reducer(const AcquisitionContext& context, const AcquisitionPolicy& policy) : m_policy(policy) {
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
            failRetryable(ErrorCode::ProviderUnavailable, "The Steam helper stopped unexpectedly.");
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
        case AwaitStep::Removal:
            if (kind() == AcquisitionKind::Reinstall)
                removal(o.status);
            else
                plan(o.status); // only a reinstall waits for a removal
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
            if (kind() == AcquisitionKind::Reinstall)
                setStep(ReinstallStep::Resubscribed);
            setState(AcquisitionState::Subscribed);
            clearError();
            queryState();
            return;
        }
        failWith(o.result, "Subscribing to Workshop item " + job().identity.id);
    }

    void on(const obs::UnsubscribeResult& o) {
        if (ctx().awaiting != AwaitStep::UnsubscribeResult)
            return;
        const SteamResultInfo info = classifySteamResult(o.result);
        if (!info.ok()) {
            // Nothing changed in Steam: the item is still subscribed.
            failWith(o.result, "Unsubscribing from Workshop item " + job().identity.id);
            return;
        }
        setStep(ReinstallStep::Unsubscribed);
        clearError();
        await(AwaitStep::Removal);
        ctx().droppedAtMs = 0;
        ctx().lastPollAtMs = ctx().clockMs;
        emit(ActionKind::QueryState);
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
        ctx().downloadRequestedAtMs = ctx().clockMs;
        ctx().currentButOld = false;
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
            } else if (ctx().currentButOld &&
                       now - ctx().downloadRequestedAtMs >= m_policy.updateRecheckMs) {
                if (!ctx().reRequested) {
                    // Steam may have answered the first request before it
                    // noticed the new version: ask once more.
                    ctx().reRequested = true;
                    ctx().currentButOld = false;
                    await(AwaitStep::DownloadAccepted);
                    emit(ActionKind::RequestDownload);
                } else {
                    failNotSeen();
                }
            } else if (now - ctx().lastPollAtMs >= m_policy.pollIntervalMs) {
                ctx().lastPollAtMs = now;
                emit(ActionKind::QueryState);
            }
            return;
        case AwaitStep::Removal:
            if (waited >= m_policy.unsubscribeTimeoutMs) {
                failRetryable(ErrorCode::Timeout,
                              "Steam did not drop the subscription in time after unsubscribing.");
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
        case AwaitStep::UnsubscribeResult:
            if (waited >= m_policy.unsubscribeTimeoutMs)
                failRetryable(ErrorCode::Timeout,
                              "Steam did not confirm in time that it unsubscribed Workshop item " +
                                  job().identity.id +
                                  ", so the item may be unsubscribed now. A retry subscribes to "
                                  "it again in that case.");
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
        const bool unsubscribeMaybe = ctx().unsubscribeSent || s == AcquisitionState::Unsubscribing;
        idle();
        setState(AcquisitionState::Cancelled);
        std::string message;
        if (kind() == AcquisitionKind::Reinstall &&
            job().reinstallStep == ReinstallStep::Unsubscribed) {
            message = "Cancelled. Workshop item " + job().identity.id +
                      " was unsubscribed for the re-download and is not subscribed again: "
                      "Retry subscribes to it again and downloads it, or subscribe on its "
                      "Workshop page.";
            if (subscribeMaybe)
                message += " A subscription request was already sent and may still complete.";
        } else if (kind() == AcquisitionKind::Reinstall &&
                   job().reinstallStep == ReinstallStep::Start && unsubscribeMaybe) {
            message = "Cancelled. An unsubscribe request was already sent and may still complete "
                      "in Steam; if it does, Retry subscribes to the item again and downloads "
                      "it.";
        } else if (kind() != AcquisitionKind::Acquire) {
            message = downloadAsked ? "Cancelled. Steam may still finish the download that was "
                                      "already requested."
                                    : "Cancelled before a download was requested from Steam; "
                                      "the installed version stays.";
        } else if (downloadAsked) {
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
    AcquisitionKind kind() const { return m_t.context.job.kind; }

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

    void setStep(ReinstallStep step) {
        if (job().reinstallStep != step) {
            job().reinstallStep = step;
            touch();
        }
    }

    // Every failure of a re-download that unsubscribed the item says so: the
    // item must never be left unsubscribed without the user knowing.
    void setError(ErrorCode code, std::string message) {
        if (kind() == AcquisitionKind::Reinstall &&
            job().reinstallStep == ReinstallStep::Unsubscribed && code != ErrorCode::Cancelled)
            message += " Workshop item " + job().identity.id +
                       " is now unsubscribed in Steam: Retry subscribes to it again and downloads "
                       "it.";
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
        ctx().currentButOld = false;
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

    // Steam keeps reporting the old version: retrying within seconds would not
    // help (Steam checks about every 45 minutes), so only the user retries.
    void failNotSeen() {
        idle();
        setState(AcquisitionState::Failed);
        setError(ErrorCode::UpdateNotSeen, std::string(kUpdateNotSeenMessage));
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
        ctx().unsubscribeSent = false;
        ctx().downloadRequestedAtMs = 0;
        ctx().reRequested = false;
        ctx().currentButOld = false;
        ctx().droppedAtMs = 0;
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

    // Update and reinstall jobs: the installed version is the one wanted. With
    // a known target, Steam's install timestamp must reach it; without one, an
    // update is done once Steam calls the item current after this run's
    // request, a reinstall once its download was requested.
    bool targetReached(const ItemStatus& s) {
        if (job().targetTimeUpdated > 0)
            return static_cast<std::int64_t>(s.timestamp) >= job().targetTimeUpdated;
        return ctx().downloadRequested || kind() == AcquisitionKind::Reinstall;
    }

    // Decides the next step from a fresh state report.
    void plan(const ItemStatus& s) {
        switch (kind()) {
        case AcquisitionKind::Acquire:
            planAcquire(s);
            return;
        case AcquisitionKind::Update:
            planUpdate(s);
            return;
        case AcquisitionKind::Reinstall:
            planReinstall(s);
            return;
        }
    }

    void planAcquire(const ItemStatus& s) {
        const ItemFlags f = decodeItemFlags(s.flags);
        if (f.subscribed)
            ctx().subscribeConfirmed = true;
        const bool subscribed = ctx().subscribeConfirmed;

        if (subscribed && f.installedAndCurrent() && !s.installPath.empty()) {
            verify(s.installPath);
            return;
        }
        if (!subscribed) {
            subscribe();
            return;
        }
        if (job().state == AcquisitionState::Pending ||
            job().state == AcquisitionState::Subscribing)
            setState(AcquisitionState::Subscribed);
        download(s, f);
    }

    void planUpdate(const ItemStatus& s) {
        const ItemFlags f = decodeItemFlags(s.flags);
        if (!f.subscribed) {
            // Steam does not keep an unsubscribed item updated, and an update
            // never subscribes on the user's behalf.
            needsUser(ErrorCode::ValidationFailed,
                      "Workshop item " + job().identity.id +
                          " is not subscribed in Steam, so Steam does not update it. Subscribe "
                          "to it on its Workshop page, or use Re-download from scratch, then "
                          "retry.");
            return;
        }
        ctx().subscribeConfirmed = true;
        if (f.installedAndCurrent() && !s.installPath.empty() && targetReached(s)) {
            verify(s.installPath); // already the wanted version
            return;
        }
        download(s, f);
    }

    void planReinstall(const ItemStatus& s) {
        const ItemFlags f = decodeItemFlags(s.flags);
        if (job().reinstallStep == ReinstallStep::Start) {
            if (f.subscribed) {
                if (!requireOnline())
                    return;
                setState(AcquisitionState::Unsubscribing);
                clearError();
                ctx().unsubscribeSent = true;
                await(AwaitStep::UnsubscribeResult);
                emit(ActionKind::Unsubscribe);
                return;
            }
            setStep(ReinstallStep::Unsubscribed); // nothing to unsubscribe
        }
        if (job().reinstallStep == ReinstallStep::Unsubscribed && f.subscribed)
            setStep(ReinstallStep::Resubscribed); // subscribed again meanwhile
        if (job().reinstallStep == ReinstallStep::Resubscribed && !f.subscribed)
            setStep(ReinstallStep::Unsubscribed); // unsubscribed again meanwhile
        if (job().reinstallStep == ReinstallStep::Unsubscribed) {
            subscribe();
            return;
        }
        ctx().subscribeConfirmed = true;
        if (job().state == AcquisitionState::Pending ||
            job().state == AcquisitionState::Subscribing ||
            job().state == AcquisitionState::Unsubscribing)
            setState(AcquisitionState::Subscribed);
        download(s, f);
    }

    void subscribe() {
        if (!requireOnline())
            return;
        setState(AcquisitionState::Subscribing);
        clearError();
        ctx().subscribeSent = true;
        await(AwaitStep::SubscribeResult);
        emit(ActionKind::Subscribe);
    }

    // Subscribed: follow a running download, wait for the one this run asked
    // for, or ask for one (for an update or reinstall also when Steam calls
    // the installed version current).
    void download(const ItemStatus& s, const ItemFlags& f) {
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
        const bool dropped = !f.subscribed && !f.downloading && !f.downloadPending;
        if (dropped && kind() == AcquisitionKind::Update) {
            needsUser(ErrorCode::ValidationFailed,
                      "Workshop item " + job().identity.id +
                          " is no longer subscribed in Steam (it may have been unsubscribed "
                          "manually), so Steam does not update it. Subscribe to it on its "
                          "Workshop page, or use Re-download from scratch, then retry.");
            return;
        }
        if (dropped && kind() == AcquisitionKind::Reinstall) {
            setStep(ReinstallStep::Unsubscribed); // a retry subscribes again
            needsUser(ErrorCode::ValidationFailed,
                      "Steam no longer reports a subscription to the re-downloaded item (it may "
                      "have been unsubscribed manually).");
            return;
        }
        if (f.installedAndCurrent() && !s.installPath.empty()) {
            if (kind() == AcquisitionKind::Acquire || targetReached(s)) {
                verify(s.installPath);
                return;
            }
            // Steam calls the old version current; the Tick decides when to
            // ask again or give up.
            ctx().currentButOld = true;
            bool changed = s.flags != previousFlags;
            recordBytes(s, changed);
            if (changed)
                ctx().lastProgressAtMs = ctx().clockMs;
            return;
        }
        ctx().currentButOld = false;
        if (dropped) {
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

    // Reinstall: after the unsubscribe, until Steam no longer reports the
    // subscription and, within removalGraceMs, no longer the installed files.
    void removal(const ItemStatus& s) {
        const ItemFlags f = decodeItemFlags(s.flags);
        if (f.subscribed)
            return; // the Tick polls again and times out
        if (ctx().droppedAtMs == 0)
            ctx().droppedAtMs = std::max<std::int64_t>(1, ctx().clockMs);
        if (f.installed && ctx().clockMs - ctx().droppedAtMs < m_policy.removalGraceMs)
            return;
        subscribe();
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
        // An update is complete only at its target; a re-download only once
        // this job saw it download.
        const bool complete =
            kind() == AcquisitionKind::Acquire ||
            (kind() == AcquisitionKind::Update && job().targetTimeUpdated > 0 && targetReached(s));
        if (complete && f.installedAndCurrent() && !s.installPath.empty()) {
            ctx().retryAtMs = 0;
            ctx().subscribeConfirmed = true;
            verify(s.installPath);
            return;
        }
        if (f.downloading || f.downloadPending) {
            ctx().retryAtMs = 0;
            ctx().subscribeConfirmed = true;
            if (kind() == AcquisitionKind::Reinstall)
                setStep(ReinstallStep::Resubscribed);
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
    case AwaitStep::UnsubscribeResult:
        return "unsubscribe_result";
    case AwaitStep::Removal:
        return "removal";
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
    case ActionKind::Unsubscribe:
        return "unsubscribe";
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
            } else if constexpr (std::is_same_v<T, UnsubscribeResultEvent>) {
                if (e.id == itemId)
                    return obs::UnsubscribeResult{e.result};
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
    case ActionKind::Unsubscribe:
        return HelperCommand{UnsubscribeCommand{action.itemId}};
    case ActionKind::None:
    case ActionKind::QuerySession:
    case ActionKind::VerifyContent:
    case ActionKind::OpenFallbackPage:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace pmm::providers
