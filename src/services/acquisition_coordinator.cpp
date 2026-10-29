#include "services/acquisition_coordinator.hpp"

#include "core/catalog.hpp"
#include "core/log.hpp"
#include "core/readiness.hpp"
#include "persistence/acquisition_job_repository.hpp"
#include "persistence/installation_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "platform/fs.hpp"
#include "platform/steam.hpp"
#include "providers/workshop_item.hpp"
#include "services/internal/util.hpp"

#include <algorithm>
#include <iterator>
#include <system_error>
#include <type_traits>
#include <variant>

namespace pmm::services {
namespace {

namespace obs = providers::obs;
using providers::ActionKind;
using providers::AwaitStep;

constexpr const char* kComponent = "services.acquisition";

double progressOf(const AcquisitionJob& job) {
    if (job.bytesTotal == 0)
        return -1.0;
    return std::min(1.0,
                    static_cast<double>(job.bytesDownloaded) / static_cast<double>(job.bytesTotal));
}

std::string stateTextOf(const AcquisitionJob& job) {
    switch (job.state) {
    case AcquisitionState::Pending:
        return "Queued";
    case AcquisitionState::Subscribing:
        return "Subscribing";
    case AcquisitionState::Subscribed:
        return "Subscribed, waiting for the download";
    case AcquisitionState::DownloadRequested:
        return "Download requested";
    case AcquisitionState::Downloading: {
        const double progress = progressOf(job);
        if (progress < 0)
            return "Downloading";
        return "Downloading " + std::to_string(static_cast<int>(progress * 100.0)) + " %";
    }
    case AcquisitionState::Installed:
        return "Installed";
    case AcquisitionState::Failed:
        return "Failed";
    case AcquisitionState::Unavailable:
        return "Unavailable";
    case AcquisitionState::NeedsUserAction:
        return "Action needed";
    case AcquisitionState::Cancelled:
        return "Cancelled";
    }
    return "Unknown";
}

bool retryable(AcquisitionState state) {
    switch (state) {
    case AcquisitionState::Failed:
    case AcquisitionState::NeedsUserAction:
    case AcquisitionState::Unavailable:
    case AcquisitionState::Cancelled:
        return true;
    default:
        return false;
    }
}

obs::UnavailableReason reasonForExitCode(int exitCode) {
    using R = obs::UnavailableReason;
    switch (static_cast<providers::HelperExitCode>(exitCode)) {
    case providers::HelperExitCode::SteamNotRunning:
        return R::SteamNotRunning;
    case providers::HelperExitCode::NotLoggedOn:
        return R::NotLoggedOn;
    case providers::HelperExitCode::InitFailed:
        return R::InitFailed;
    case providers::HelperExitCode::LibraryLoadFailed:
        return R::LibraryMissing;
    case providers::HelperExitCode::SymbolsMissing:
        return R::Incompatible;
    default:
        return R::HelperFailed;
    }
}

obs::UnavailableReason reasonForFailure(providers::HelperFailure failure) {
    using R = obs::UnavailableReason;
    switch (failure) {
    case providers::HelperFailure::SteamNotRunning:
        return R::SteamNotRunning;
    case providers::HelperFailure::NotLoggedOn:
        return R::NotLoggedOn;
    case providers::HelperFailure::LibraryLoadFailed:
        return R::LibraryMissing;
    case providers::HelperFailure::SymbolsMissing:
        return R::Incompatible;
    case providers::HelperFailure::InvalidArguments:
        return R::HelperFailed;
    case providers::HelperFailure::None:
    case providers::HelperFailure::InitFailed:
        return R::InitFailed;
    }
    return R::InitFailed;
}

ProviderStatus statusFor(obs::UnavailableReason reason) {
    using R = obs::UnavailableReason;
    switch (reason) {
    case R::SteamNotRunning:
        return ProviderStatus::SteamNotRunning;
    case R::NotLoggedOn:
        return ProviderStatus::NotLoggedOn;
    default:
        return ProviderStatus::Unavailable;
    }
}

std::string messageFor(obs::UnavailableReason reason, const std::string& gameName) {
    using R = obs::UnavailableReason;
    switch (reason) {
    case R::SteamNotRunning:
        return "Steam is not running. Start Steam, sign in, and retry.";
    case R::NotLoggedOn:
        return "Steam is not signed in. Sign in to Steam and retry.";
    case R::InitFailed:
        return "Steam refused a Workshop session for " + gameName +
               ". Make sure Steam is running and signed in to the account that owns the game.";
    case R::LibraryMissing:
        return "The Steam API library of " + gameName +
               " is missing or could not be loaded. Verify the game files in Steam, or use the "
               "manual Workshop page.";
    case R::Incompatible:
        return "The Steam API library of " + gameName +
               " does not support automatic Workshop downloads. Use the manual Workshop page, "
               "then rescan.";
    case R::HelperFailed:
        return "The Steam helper stopped unexpectedly. Downloads are retried automatically.";
    }
    return {};
}

} // namespace

const char* toString(ProviderStatus status) noexcept {
    switch (status) {
    case ProviderStatus::Unknown:
        return "unknown";
    case ProviderStatus::Ready:
        return "ready";
    case ProviderStatus::SteamNotRunning:
        return "steam_not_running";
    case ProviderStatus::NotLoggedOn:
        return "not_logged_on";
    case ProviderStatus::NotOwned:
        return "not_owned";
    case ProviderStatus::Unavailable:
        return "unavailable";
    }
    return "unknown";
}

AcquisitionCoordinator::AcquisitionCoordinator(const ServiceContext& context, GameService& games,
                                               WorkshopSessionHost& sessions,
                                               WorkshopMetadataHost* metadata,
                                               CoordinatorOptions options)
    : m_ctx(context), m_games(games), m_sessions(sessions), m_metadata(metadata),
      m_options(std::move(options)) {
    if (!m_options.verifyContent)
        m_options.verifyContent = [](const std::filesystem::path& path) {
            return providers::verifyWorkshopContent(path);
        };
}

// --- Event loop ----------------------------------------------------------------

CoordinatorUpdate AcquisitionCoordinator::run(const std::function<void()>& work) {
    if (m_draining) {
        // Re-entrant host callback: the outer call processes what it queues.
        work();
        return {};
    }
    m_draining = true;
    ++m_pass;
    CoordinatorUpdate update;
    work();
    do {
        while (!m_queue.empty()) {
            auto [jobId, observation] = std::move(m_queue.front());
            m_queue.pop_front();
            process(jobId, observation);
        }
        flushOutbox();
        if (m_queue.empty() && !m_rescanPending.empty()) {
            std::set<GameKey> games;
            games.swap(m_rescanPending);
            for (const GameKey& game : games) {
                auto rescanned = m_games.rescanMods(game);
                if (rescanned)
                    update.rescanned.push_back(std::move(rescanned).value());
                else
                    logWarning(kComponent, "inventory rescan after install failed",
                               {{"game", game}, {"error", rescanned.error().message}});
            }
        }
        if (m_queue.empty())
            closeIdleSessions();
    } while (!m_queue.empty());
    for (auto it = m_jobs.begin(); it != m_jobs.end();)
        it = it->second.stopped ? m_jobs.erase(it) : std::next(it);
    m_draining = false;
    update.changed = m_changed || !update.rescanned.empty();
    m_changed = false;
    return update;
}

void AcquisitionCoordinator::enqueue(std::int64_t jobId, providers::Observation observation) {
    m_queue.emplace_back(jobId, std::move(observation));
}

void AcquisitionCoordinator::process(std::int64_t jobId,
                                     const providers::Observation& observation) {
    const auto it = m_jobs.find(jobId);
    if (it == m_jobs.end() || it->second.stopped)
        return; // parked, forgotten or stopped since the observation was queued
    apply(jobId, providers::reduceAcquisition(it->second.context, observation, m_options.policy));
}

void AcquisitionCoordinator::apply(std::int64_t jobId, providers::Transition transition) {
    Runtime& runtime = m_jobs.at(jobId);
    const AcquisitionState before = runtime.context.job.state;
    runtime.context = std::move(transition.context);
    if (transition.persist) {
        m_changed = true;
        if (!persist(runtime, false))
            return; // stopped as a duplicate: none of its actions run
    }
    if (before != AcquisitionState::Installed &&
        runtime.context.job.state == AcquisitionState::Installed) {
        logInfo(kComponent, "workshop item installed and verified",
                {{"mod", toString(runtime.context.job.identity)}});
        m_rescanPending.insert(runtime.game);
    }
    for (const providers::Action& action : transition.actions)
        perform(jobId, action);
}

void AcquisitionCoordinator::perform(std::int64_t jobId, const providers::Action& action) {
    const GameKey game = m_jobs.at(jobId).game;
    switch (action.kind) {
    case ActionKind::QuerySession:
        ensureSession(game, jobId);
        return;
    case ActionKind::QueryState:
    case ActionKind::Subscribe:
    case ActionKind::RequestDownload: {
        auto command = providers::helperCommandFor(action);
        if (!command)
            return;
        auto& outbox = m_outbox[game];
        if (const auto* state = std::get_if<providers::StateCommand>(&*command)) {
            for (providers::HelperCommand& queued : outbox) {
                auto* batch = std::get_if<providers::StateCommand>(&queued);
                if (!batch || batch->ids.size() >= providers::kMaxIdsPerStateCommand)
                    continue;
                for (const std::string& id : state->ids) {
                    if (std::find(batch->ids.begin(), batch->ids.end(), id) == batch->ids.end())
                        batch->ids.push_back(id);
                }
                return;
            }
        }
        outbox.push_back(std::move(*command));
        return;
    }
    case ActionKind::VerifyContent: {
        const Status verified = m_options.verifyContent(platform::pathFromUtf8(action.installPath));
        enqueue(jobId,
                obs::ContentVerified{verified.ok(),
                                     verified.ok() ? std::string() : verified.error().message});
        return;
    }
    case ActionKind::OpenFallbackPage: // openFallback() hands the links to the caller
    case ActionKind::None:
        return;
    }
}

void AcquisitionCoordinator::flushOutbox() {
    std::map<GameKey, std::vector<providers::HelperCommand>> outbox;
    outbox.swap(m_outbox);
    for (auto& [game, commands] : outbox) {
        const auto session = m_sessionState.find(game);
        if (session == m_sessionState.end() || session->second.state != SessionState::Ready) {
            // The jobs time out and retry; nothing is lost.
            logDebug(kComponent, "dropped helper commands for a closed session", {{"game", game}});
            continue;
        }
        for (const providers::HelperCommand& command : commands)
            m_sessions.send(game, command);
    }
}

bool AcquisitionCoordinator::persist(Runtime& runtime, bool force) {
    const AcquisitionJob& job = runtime.context.job;
    const AcquisitionJob& last = runtime.persisted;
    const bool significant = force || job.state != last.state || job.errorCode != last.errorCode ||
                             job.errorMessage != last.errorMessage ||
                             job.attempts != last.attempts || job.playsetId != last.playsetId;
    const std::int64_t now = m_ctx.now();
    if (!significant && now - runtime.persistedAtMs < m_options.progressPersistIntervalMs) {
        runtime.dirty = true;
        return true;
    }
    persistence::AcquisitionJobRepository repo(m_ctx.database);
    if (Status saved = repo.update(job); !saved) {
        if (saved.error().code == ErrorCode::AlreadyExists) {
            // Another unfinished job for this mod owns the work (the database
            // allows one per mod): stop this one at its last saved state and
            // forget it after this pass, instead of retrying the write on
            // every tick.
            logWarning(kComponent, "stopped a duplicate acquisition job",
                       {{"job", std::to_string(job.id)}, {"mod", toString(job.identity)}});
            runtime.context.job = runtime.persisted;
            runtime.context.awaiting = AwaitStep::Nothing;
            runtime.context.retryAtMs = 0;
            runtime.dirty = false;
            runtime.stopped = true;
            m_changed = true;
            return false;
        }
        logWarning(kComponent, "saving an acquisition job failed",
                   {{"job", std::to_string(job.id)}, {"error", saved.error().message}});
        runtime.dirty = true;
        return true;
    }
    runtime.persisted = job;
    runtime.persistedAtMs = now;
    runtime.dirty = false;
    return true;
}

// --- Sessions ------------------------------------------------------------------

std::optional<GameInstallation>
AcquisitionCoordinator::storedInstallation(const GameKey& game) const {
    persistence::InstallationRepository installations(m_ctx.database);
    auto stored = installations.get(game);
    if (!stored)
        return std::nullopt;
    return std::move(stored).value();
}

std::string AcquisitionCoordinator::appIdFor(const GameKey& game) const {
    if (auto installation = storedInstallation(game);
        installation && !installation->steamAppId.empty())
        return installation->steamAppId;
    const GameCatalogEntry* entry = findGame(game);
    return entry ? std::string(entry->steamAppId) : std::string();
}

void AcquisitionCoordinator::setProvider(const GameKey& game, ProviderStatus status,
                                         std::string message) {
    Provider& provider = m_providers[game];
    if (provider.status == status && provider.message == message)
        return;
    provider.status = status;
    provider.message = std::move(message);
    m_changed = true;
}

void AcquisitionCoordinator::failSession(const GameKey& game, SessionFailure failure,
                                         ProviderStatus status) {
    Session& session = m_sessionState[game];
    session.state = SessionState::Failed;
    session.failedInPass = m_pass;
    setProvider(game, status, failure.message);
    session.failure = std::move(failure);
}

void AcquisitionCoordinator::applyFailure(std::int64_t jobId, const SessionFailure& failure) {
    if (failure.reason) {
        enqueue(jobId, obs::ProviderUnavailable{*failure.reason, failure.message});
        return;
    }
    // Not a Steam condition the reducer knows (game not located, helper
    // missing): the job waits for the user, exactly like a reducer needsUser().
    Runtime& runtime = m_jobs.at(jobId);
    providers::AcquisitionContext& context = runtime.context;
    if (context.awaiting != AwaitStep::Session)
        return;
    context.awaiting = AwaitStep::Nothing;
    context.retryAtMs = 0;
    context.sessionKnown = false;
    context.job.state = AcquisitionState::NeedsUserAction;
    context.job.errorCode = std::string(errorCodeName(failure.code));
    context.job.errorMessage = failure.message;
    context.job.updatedAt = context.clockMs;
    m_changed = true;
    persist(runtime, true);
}

void AcquisitionCoordinator::ensureSession(const GameKey& game, std::int64_t jobId) {
    Session& session = m_sessionState[game];
    const std::int64_t now = m_ctx.now();
    switch (session.state) {
    case SessionState::Ready:
        enqueue(jobId, obs::ProviderReady{session.ready.loggedOn, session.ready.ownsApp});
        return;
    case SessionState::Opening:
        if (now - session.openedAtMs < m_options.policy.responseTimeoutMs)
            return;         // the ready event will answer every waiting job
        closeSession(game); // start-up hangs: start over
        break;
    case SessionState::Failed:
        if (session.failure && session.failedInPass == m_pass) {
            applyFailure(jobId, *session.failure);
            return;
        }
        closeSession(game); // a later attempt (retry): start over
        break;
    case SessionState::Closed:
        break;
    }

    const std::string gameName = gameDisplayName(game);
    const auto fail = [&](std::optional<obs::UnavailableReason> reason, std::string message) {
        failSession(game,
                    SessionFailure{reason, ErrorCode::ProviderUnavailable, std::move(message)},
                    ProviderStatus::Unavailable);
        applyFailure(jobId, *m_sessionState[game].failure);
    };

    const std::optional<GameInstallation> installation = storedInstallation(game);
    if (!installation) {
        fail(std::nullopt, gameName + " has not been found on this computer, so its Workshop items "
                                      "cannot be downloaded. Locate or install the game, then "
                                      "retry.");
        return;
    }
    const std::string appId = appIdFor(game);
    if (!providers::isValidSteamAppId(appId)) {
        fail(std::nullopt, "This installation of " + gameName +
                               " is not linked to Steam, so Workshop items cannot be downloaded "
                               "automatically. Use the manual Workshop page, then rescan.");
        return;
    }
    const auto library = platform::steam::findSteamApiLibrary(installation->installPath);
    if (!library) {
        fail(obs::UnavailableReason::LibraryMissing,
             "The Steam API library of " + gameName + " (" +
                 platform::steam::steamApiLibraryName() +
                 ") was not found in the game folder. Verify the game files in Steam, or use the "
                 "manual Workshop page.");
        return;
    }
    if (!m_options.helperExecutable.empty()) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(m_options.helperExecutable, ec)) {
            fail(std::nullopt, "The Steam helper (pmm-steam-helper) is missing from this "
                               "installation of Paradox Mod Manager. Reinstall the application, "
                               "or use the manual Workshop page.");
            return;
        }
    }

    session = Session{};
    session.state = SessionState::Opening;
    session.openedAtMs = now;
    session.hostOpen = true; // set first: open() may deliver events re-entrantly
    setProvider(game, ProviderStatus::Unknown, "Connecting to Steam.");
    logInfo(kComponent, "opening steam helper session",
            {{"game", game}, {"library", platform::pathToUtf8(*library)}});
    if (!m_sessions.open(game, appId, *library)) {
        m_sessionState[game].hostOpen = false;
        fail(std::nullopt, "The Steam helper could not be started. Reinstall Paradox Mod Manager "
                           "if the problem persists, or use the manual Workshop page.");
    }
}

void AcquisitionCoordinator::closeSession(const GameKey& game) {
    const auto it = m_sessionState.find(game);
    if (it == m_sessionState.end() || it->second.state == SessionState::Closed)
        return;
    const bool hostOpen = it->second.hostOpen;
    it->second = Session{};
    m_outbox.erase(game);
    for (auto& [id, runtime] : m_jobs) {
        if (runtime.game == game)
            runtime.context.sessionKnown = false; // the next run asks for a session again
    }
    if (hostOpen) {
        logInfo(kComponent, "closing steam helper session", {{"game", game}});
        m_sessions.close(game);
    }
}

bool AcquisitionCoordinator::busy(const Runtime& runtime) const noexcept {
    return runtime.context.awaiting != AwaitStep::Nothing;
}

bool AcquisitionCoordinator::inProgress(const Runtime& runtime) const noexcept {
    return !isTerminal(runtime.context.job.state) || runtime.context.retryAtMs > 0;
}

void AcquisitionCoordinator::closeIdleSessions() {
    std::vector<GameKey> idle;
    for (const auto& [game, session] : m_sessionState) {
        if (session.state == SessionState::Closed)
            continue;
        const bool working = std::any_of(m_jobs.begin(), m_jobs.end(), [&](const auto& item) {
            return item.second.game == game && busy(item.second);
        });
        if (!working)
            idle.push_back(game);
    }
    for (const GameKey& game : idle)
        closeSession(game);
}

// --- Jobs ------------------------------------------------------------------------

AcquisitionCoordinator::Runtime* AcquisitionCoordinator::adopt(const AcquisitionJob& job,
                                                               bool resume) {
    const std::int64_t now = m_ctx.now();
    Runtime runtime;
    runtime.game = job.identity.game;
    runtime.persisted = job;
    runtime.persistedAtMs = now;
    runtime.context.job = job;
    runtime.context.clockMs = now;
    runtime.context.awaitingSinceMs = now;
    Runtime* adopted = &m_jobs.insert_or_assign(job.id, std::move(runtime)).first->second;
    if (resume) {
        dropSuperseded(job.id);
        apply(job.id, providers::startAcquisition(job, now, m_options.policy));
    }
    return adopted;
}

// A job that starts running supersedes the idle, finished in-memory jobs for
// the same mod (their database rows remain, and snapshots show the newest job
// per mod). Kept in memory, an automatic retry or a state report for the mod
// could revive them next to it.
void AcquisitionCoordinator::dropSuperseded(std::int64_t jobId) {
    const ModIdentity identity = m_jobs.at(jobId).context.job.identity;
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
        Runtime& other = it->second;
        if (it->first == jobId || other.context.job.identity != identity ||
            !isTerminal(other.context.job.state)) {
            ++it;
            continue;
        }
        if (other.dirty)
            persist(other, true);
        logInfo(kComponent, "older acquisition job superseded",
                {{"job", std::to_string(it->first)}, {"by", std::to_string(jobId)}});
        it = m_jobs.erase(it);
        m_changed = true;
    }
}

void AcquisitionCoordinator::rememberNames(const std::vector<AcquisitionJob>& jobs) {
    persistence::PlaysetRepository playsets(m_ctx.database);
    std::map<std::int64_t, std::optional<Playset>> loaded;
    for (const AcquisitionJob& job : jobs) {
        if (m_names.count(job.identity) || !job.playsetId)
            continue;
        auto [it, inserted] = loaded.try_emplace(*job.playsetId);
        if (inserted) {
            auto playset = playsets.get(*job.playsetId);
            if (playset)
                it->second = std::move(playset).value();
        }
        if (!it->second)
            continue;
        for (const PlaysetEntry& entry : it->second->entries) {
            if (entry.identity.source == job.identity.source &&
                entry.identity.id == job.identity.id && !entry.nameHint.empty()) {
                m_names[job.identity] = entry.nameHint;
                break;
            }
        }
    }
}

std::string AcquisitionCoordinator::nameFor(const AcquisitionJob& job) const {
    const auto it = m_names.find(job.identity);
    return it != m_names.end() ? it->second : job.identity.id;
}

void AcquisitionCoordinator::requestMetadata(const GameKey& game,
                                             const std::vector<std::string>& ids) {
    if (!m_metadata || ids.empty())
        return;
    std::vector<std::string> batch;
    for (const std::string& id : ids) {
        if (!providers::isValidPublishedFileId(id))
            continue;
        batch.push_back(id);
        if (batch.size() == providers::kMaxPublishedFileDetailsItems) {
            m_metadata->request(game, batch);
            batch.clear();
        }
    }
    if (!batch.empty())
        m_metadata->request(game, batch);
}

CoordinatorUpdate AcquisitionCoordinator::start(const GameKey& game) {
    return run([&] {
        persistence::AcquisitionJobRepository repo(m_ctx.database);
        auto unfinished = repo.listUnfinished();
        if (!unfinished) {
            logWarning(kComponent, "loading unfinished jobs failed",
                       {{"error", unfinished.error().message}});
            return;
        }
        std::vector<AcquisitionJob> fresh;
        for (const AcquisitionJob& job : unfinished.value()) {
            if (job.identity.game == game && !m_jobs.count(job.id))
                fresh.push_back(job);
        }
        if (fresh.empty())
            return;
        rememberNames(fresh);
        std::vector<std::string> ids;
        for (const AcquisitionJob& job : fresh) {
            logInfo(kComponent, "resuming acquisition job",
                    {{"job", std::to_string(job.id)},
                     {"mod", toString(job.identity)},
                     {"state", pmm::toString(job.state)}});
            adopt(job, true);
            if (job.identity.source == source::SteamWorkshop &&
                m_metadataRequested.insert(job.identity).second)
                ids.push_back(job.identity.id);
        }
        m_changed = true;
        requestMetadata(game, ids);
    });
}

Result<CoordinatorUpdate> AcquisitionCoordinator::acquireMissing(std::int64_t playsetId) {
    persistence::PlaysetRepository playsets(m_ctx.database);
    auto playset = playsets.get(playsetId);
    if (!playset)
        return std::move(playset).error();
    const GameKey game = playset.value().game;
    persistence::InventoryRepository inventory(m_ctx.database);
    auto mods = inventory.list(game);
    if (!mods)
        return std::move(mods).error();
    persistence::AcquisitionJobRepository repo(m_ctx.database);
    auto stored = repo.listForGame(game);
    if (!stored)
        return std::move(stored).error();
    const std::vector<AcquisitionJob> jobs =
        detail::overlayJobs(std::move(stored).value(), liveJobs(game));

    std::vector<PlaysetEntry> entries = playset.value().entries;
    for (PlaysetEntry& entry : entries)
        entry.identity.game = game;
    const std::vector<EntryReadiness> readiness =
        computePlaysetReadiness(entries, detail::toMap(mods.value()), jobs);
    std::vector<ModIdentity> missing;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].identity.source == source::SteamWorkshop &&
            readiness[i].status == EntryStatus::Missing)
            missing.push_back(entries[i].identity);
    }
    PMM_TRY(m_ctx.database.transaction([&]() -> Status {
        for (const ModIdentity& identity : missing) {
            auto job = repo.createOrReuse(identity, playsetId);
            if (!job)
                return std::move(job).error();
        }
        return {};
    }));
    return start(game);
}

CoordinatorUpdate AcquisitionCoordinator::onHelperEvent(const GameKey& game,
                                                        const providers::HelperEvent& event) {
    return run([&] {
        Session& session = m_sessionState[game];
        if (session.state == SessionState::Closed) {
            logDebug(kComponent, "event for a closed helper session ignored", {{"game", game}});
            return;
        }
        if (const auto* ready = std::get_if<providers::ReadyEvent>(&event)) {
            session.ready = *ready;
            const std::string gameName = gameDisplayName(game);
            if (ready->ok) {
                session.state = SessionState::Ready;
                session.failure.reset();
                if (!ready->loggedOn)
                    setProvider(game, ProviderStatus::NotLoggedOn,
                                "Steam is offline or not signed in. Installed items can be "
                                "checked, but nothing can be downloaded until Steam is online "
                                "and signed in.");
                else if (!ready->ownsApp)
                    setProvider(game, ProviderStatus::NotOwned,
                                "The signed-in Steam account does not own " + gameName +
                                    ". Sign in with the account that owns it.");
                else
                    setProvider(game, ProviderStatus::Ready, "Steam is running and signed in.");
            } else {
                const obs::UnavailableReason reason = reasonForFailure(ready->error);
                logWarning(kComponent, "steam helper session refused",
                           {{"game", game},
                            {"error", providers::toString(ready->error)},
                            {"detail", ready->message}});
                failSession(game,
                            SessionFailure{reason, ErrorCode::ProviderUnavailable,
                                           messageFor(reason, gameName)},
                            statusFor(reason));
            }
        } else if (const auto* error = std::get_if<providers::ErrorEvent>(&event)) {
            logWarning(kComponent, "steam helper rejected a command",
                       {{"game", game}, {"code", error->code}, {"cmd", error->cmd}});
            return;
        }
        for (const auto& [id, runtime] : m_jobs) {
            if (runtime.game != game)
                continue;
            if (auto observation =
                    providers::observationFromHelperEvent(event, runtime.context.job.identity.id))
                enqueue(id, std::move(*observation));
        }
    });
}

CoordinatorUpdate AcquisitionCoordinator::onHelperExited(const GameKey& game, int exitCode) {
    return run([&] {
        Session& session = m_sessionState[game];
        const SessionState state = session.state;
        if (state == SessionState::Closed)
            return;
        session = Session{};
        if (state == SessionState::Failed)
            return; // expected after a refused "ready"
        const obs::UnavailableReason reason = state == SessionState::Opening
                                                  ? reasonForExitCode(exitCode)
                                                  : obs::UnavailableReason::HelperFailed;
        logWarning(kComponent, "steam helper exited",
                   {{"game", game}, {"exitCode", std::to_string(exitCode)}});
        setProvider(game, statusFor(reason), messageFor(reason, gameDisplayName(game)));
        for (const auto& [id, runtime] : m_jobs) {
            if (runtime.game == game)
                enqueue(id, obs::ProviderUnavailable{reason, "helper exited with code " +
                                                                 std::to_string(exitCode)});
        }
    });
}

CoordinatorUpdate
AcquisitionCoordinator::onMetadata(const GameKey& game,
                                   const std::vector<providers::WorkshopItemMetadata>& items) {
    return run([&] {
        const std::string appId = appIdFor(game);
        for (const providers::WorkshopItemMetadata& item : items) {
            const ModIdentity identity{game, source::SteamWorkshop, item.id};
            if (item.found && !item.title.empty()) {
                const std::string title = detail::trimmed(item.title);
                if (!title.empty() && m_names[identity] != title) {
                    m_names[identity] = title;
                    m_changed = true;
                }
            }
            if (appId.empty())
                continue;
            const Status valid = providers::validateWorkshopItemForGame(item, appId);
            if (valid)
                continue;
            for (const auto& [id, runtime] : m_jobs) {
                if (runtime.context.job.identity == identity)
                    enqueue(id, obs::ItemRejected{valid.error().code, valid.error().message});
            }
        }
    });
}

CoordinatorUpdate AcquisitionCoordinator::tick(std::int64_t nowMs) {
    return run([&] {
        const std::int64_t now = m_ctx.now();
        for (auto& [id, runtime] : m_jobs) {
            if (runtime.dirty && now - runtime.persistedAtMs >= m_options.progressPersistIntervalMs)
                persist(runtime, true);
            if (busy(runtime) || runtime.context.retryAtMs > 0)
                enqueue(id, obs::Tick{nowMs});
        }
    });
}

Result<CoordinatorUpdate> AcquisitionCoordinator::retry(std::int64_t jobId) {
    persistence::AcquisitionJobRepository repo(m_ctx.database);
    const auto it = m_jobs.find(jobId);
    AcquisitionJob job;
    if (it != m_jobs.end()) {
        job = it->second.context.job;
    } else {
        auto stored = repo.get(jobId);
        if (!stored)
            return std::move(stored).error();
        if (!isTerminal(stored.value().state))
            return start(stored.value().identity.game); // parked: resume it
        job = std::move(stored).value();
    }
    if (!isTerminal(job.state))
        return CoordinatorUpdate{}; // already in progress
    if (!retryable(job.state))
        return makeError(ErrorCode::InvalidArgument,
                         "Workshop item " + job.identity.id + " is already installed.");
    // A pending automatic retry counts as in progress.
    for (const auto& [id, other] : m_jobs) {
        if (id != jobId && other.context.job.identity == job.identity && inProgress(other))
            return makeError(ErrorCode::AlreadyExists,
                             "Workshop item " + job.identity.id + " is already being acquired.");
    }
    auto unfinished = repo.listUnfinished();
    if (!unfinished)
        return std::move(unfinished).error();
    for (const AcquisitionJob& other : unfinished.value()) {
        if (other.id != jobId && other.identity == job.identity)
            return makeError(ErrorCode::AlreadyExists,
                             "Workshop item " + job.identity.id + " is already being acquired.");
    }
    if (it == m_jobs.end())
        adopt(job, false);
    dropSuperseded(jobId);
    return run([&] { enqueue(jobId, obs::Retry{}); });
}

CoordinatorUpdate AcquisitionCoordinator::retryAllFailed(const GameKey& game) {
    persistence::AcquisitionJobRepository repo(m_ctx.database);
    auto stored = repo.listForGame(game);
    std::vector<AcquisitionJob> jobs = detail::overlayJobs(
        stored ? std::move(stored).value() : std::vector<AcquisitionJob>{}, liveJobs(game));
    CoordinatorUpdate update;
    for (const AcquisitionJob& job : detail::latestPerIdentity(jobs)) {
        if (job.state != AcquisitionState::Failed && job.state != AcquisitionState::NeedsUserAction)
            continue;
        auto retried = retry(job.id);
        if (!retried) {
            logWarning(kComponent, "retry skipped",
                       {{"job", std::to_string(job.id)}, {"error", retried.error().message}});
            continue;
        }
        update.changed = update.changed || retried.value().changed;
        for (GameState& state : retried.value().rescanned)
            update.rescanned.push_back(std::move(state));
    }
    return update;
}

CancelOutcome AcquisitionCoordinator::cancelAll(const GameKey& game) {
    CancelOutcome outcome;
    outcome.update = run([&] {
        persistence::AcquisitionJobRepository repo(m_ctx.database);
        if (auto unfinished = repo.listUnfinished()) {
            for (const AcquisitionJob& job : unfinished.value()) {
                if (job.identity.game == game && !m_jobs.count(job.id))
                    adopt(job, false); // parked job: cancel it too
            }
        }
        for (const auto& [id, runtime] : m_jobs) {
            if (runtime.game == game && inProgress(runtime)) {
                enqueue(id, obs::Cancel{});
                ++outcome.cancelled;
            }
        }
    });
    if (outcome.cancelled == 0) {
        outcome.message = "There was nothing to cancel.";
    } else {
        outcome.message = "Cancelled " + std::to_string(outcome.cancelled) +
                          (outcome.cancelled == 1 ? " download" : " downloads") +
                          ". Nothing was unsubscribed or deleted: subscriptions and downloads "
                          "that Steam already completed remain in Steam.";
    }
    return outcome;
}

Result<FallbackOutcome> AcquisitionCoordinator::openFallback(std::int64_t jobId) {
    if (!m_jobs.count(jobId)) {
        persistence::AcquisitionJobRepository repo(m_ctx.database);
        auto stored = repo.get(jobId);
        if (!stored)
            return std::move(stored).error();
        adopt(stored.value(), false);
    }
    const AcquisitionJob job = m_jobs.at(jobId).context.job;
    if (job.identity.source != source::SteamWorkshop)
        return makeError(ErrorCode::InvalidArgument,
                         "Local mods have no Workshop page; install the mod folder yourself.");
    if (job.state == AcquisitionState::Installed)
        return makeError(ErrorCode::InvalidArgument,
                         "Workshop item " + job.identity.id + " is already installed.");
    auto links = providers::manualFallbackLinks(job.identity.id);
    if (!links)
        return std::move(links).error();
    FallbackOutcome outcome;
    outcome.links = std::move(links).value();
    outcome.label = std::string(providers::kManualFallbackLabel);
    outcome.update = run([&] { enqueue(jobId, obs::ManualFallback{}); });
    return outcome;
}

// listUnfinished() skips Failed jobs, so a Failed job whose automatic retry is
// due is saved as the Pending run that the retry would have started (as the
// reducer does when the retry fires); start(game) then resumes it.
void AcquisitionCoordinator::park(Runtime& runtime) {
    providers::AcquisitionContext& context = runtime.context;
    if (context.job.state == AcquisitionState::Failed && context.retryAtMs > 0) {
        context.retryAtMs = 0;
        ++context.job.attempts;
        context.job.state = AcquisitionState::Pending;
        context.job.errorCode.clear();
        context.job.errorMessage.clear();
        context.job.updatedAt = std::max(context.clockMs, m_ctx.now());
    }
    persist(runtime, true);
}

CoordinatorUpdate AcquisitionCoordinator::closeSessionsForLaunch(const GameKey& game) {
    return run([&] {
        closeSession(game);
        for (auto it = m_jobs.begin(); it != m_jobs.end();) {
            Runtime& runtime = it->second;
            if (runtime.game != game) {
                ++it;
                continue;
            }
            if (!inProgress(runtime)) {
                if (runtime.dirty)
                    persist(runtime, true);
                ++it;
                continue;
            }
            park(runtime);
            it = m_jobs.erase(it);
            m_changed = true;
        }
    });
}

void AcquisitionCoordinator::shutdown() {
    run([&] {
        for (auto it = m_jobs.begin(); it != m_jobs.end();) {
            if (it->second.context.retryAtMs > 0) {
                park(it->second);
                it = m_jobs.erase(it);
                continue;
            }
            if (it->second.dirty)
                persist(it->second, true);
            ++it;
        }
        std::vector<GameKey> open;
        for (const auto& [game, session] : m_sessionState) {
            if (session.state != SessionState::Closed)
                open.push_back(game);
        }
        for (const GameKey& game : open)
            closeSession(game);
    });
}

// --- Queries -----------------------------------------------------------------------

AcquisitionSnapshot AcquisitionCoordinator::snapshot(const GameKey& game) const {
    AcquisitionSnapshot snapshot;
    snapshot.game = game;
    if (const auto provider = m_providers.find(game); provider != m_providers.end()) {
        snapshot.providerStatus = provider->second.status;
        snapshot.providerMessage = provider->second.message;
    }
    persistence::AcquisitionJobRepository repo(m_ctx.database);
    auto stored = repo.listForGame(game);
    const std::vector<AcquisitionJob> jobs = detail::latestPerIdentity(detail::overlayJobs(
        stored ? std::move(stored).value() : std::vector<AcquisitionJob>{}, liveJobs(game)));
    // Usable installed content wins over job state (as in core/readiness).
    std::set<ModIdentity> usable;
    persistence::InventoryRepository inventory(m_ctx.database);
    if (auto mods = inventory.list(game)) {
        for (const InstalledMod& mod : mods.value()) {
            if (mod.state == InstallState::Ready || mod.state == InstallState::NeedsUpdate)
                usable.insert(mod.identity);
        }
    }

    std::size_t downloading = 0, queued = 0, failed = 0, attention = 0, unavailable = 0;
    for (const AcquisitionJob& job : jobs) {
        const auto runtime = m_jobs.find(job.id);
        const bool inMemory = runtime != m_jobs.end();
        if (!inMemory &&
            (job.state == AcquisitionState::Installed || job.state == AcquisitionState::Cancelled))
            continue; // finished in an earlier session
        const bool working =
            inMemory && (busy(runtime->second) || runtime->second.context.retryAtMs > 0);
        if (!working && retryable(job.state) && usable.count(job.identity))
            continue; // resolved outside the job, e.g. manual fallback followed by a rescan
        JobSnapshot item;
        item.job = job;
        item.name = nameFor(job);
        item.progress = progressOf(job);
        item.active = inMemory && busy(runtime->second);
        item.stateText =
            (!inMemory && !isTerminal(job.state)) ? "Waiting to resume" : stateTextOf(job);
        item.canRetry = retryable(job.state);
        item.canOpenFallback = job.identity.source == source::SteamWorkshop &&
                               providers::isValidPublishedFileId(job.identity.id) &&
                               job.state != AcquisitionState::Installed;
        if (item.canOpenFallback) {
            if (auto links = providers::manualFallbackLinks(job.identity.id))
                item.fallback = std::move(links).value();
        }
        switch (job.state) {
        case AcquisitionState::Downloading:
            ++downloading;
            break;
        case AcquisitionState::Pending:
        case AcquisitionState::Subscribing:
        case AcquisitionState::Subscribed:
        case AcquisitionState::DownloadRequested:
            ++queued;
            break;
        case AcquisitionState::Failed:
            ++failed;
            break;
        case AcquisitionState::NeedsUserAction:
            ++attention;
            break;
        case AcquisitionState::Unavailable:
            ++unavailable;
            break;
        case AcquisitionState::Installed:
        case AcquisitionState::Cancelled:
            break;
        }
        snapshot.active = snapshot.active || item.active;
        snapshot.jobs.push_back(std::move(item));
    }
    std::vector<std::string> parts;
    const auto add = [&parts](std::size_t count, const char* text) {
        if (count > 0)
            parts.push_back(std::to_string(count) + " " + text);
    };
    add(downloading, "downloading");
    add(queued, "queued");
    add(failed, "failed");
    add(attention, attention == 1 ? "needs attention" : "need attention");
    add(unavailable, "unavailable");
    for (const std::string& part : parts)
        snapshot.summary += (snapshot.summary.empty() ? "" : ", ") + part;
    return snapshot;
}

std::vector<AcquisitionJob> AcquisitionCoordinator::liveJobs(const GameKey& game) const {
    std::vector<AcquisitionJob> jobs;
    for (const auto& [id, runtime] : m_jobs) {
        if (runtime.game == game)
            jobs.push_back(runtime.context.job);
    }
    return jobs;
}

bool AcquisitionCoordinator::hasActiveWork(const GameKey& game) const {
    return std::any_of(m_jobs.begin(), m_jobs.end(), [&](const auto& item) {
        return item.second.game == game && (busy(item.second) || item.second.context.retryAtMs > 0);
    });
}

bool AcquisitionCoordinator::sessionOpen(const GameKey& game) const {
    const auto it = m_sessionState.find(game);
    return it != m_sessionState.end() && it->second.state != SessionState::Closed;
}

} // namespace pmm::services
