#include "providers/acquisition.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::providers;
namespace fs = std::filesystem;
using S = AcquisitionState;
using K = ActionKind;

namespace {

const std::string kId = "1200000023";
const std::string kPath = "D:/Steam Library/\xC3\x9C"
                          "n\xC3\xAF"
                          "c\xC3\xB8"
                          "d\xC3\xA9/steamapps/workshop/content/281990/1200000023";
constexpr std::int64_t kT0 = 1'000'000;

constexpr std::uint32_t kSub = item_flag::Subscribed;
constexpr std::uint32_t kInst = item_flag::Installed;
constexpr std::uint32_t kUpd = item_flag::NeedsUpdate;
constexpr std::uint32_t kDl = item_flag::Downloading;
constexpr std::uint32_t kPend = item_flag::DownloadPending;

AcquisitionJob makeJob(S state = S::Pending, std::string id = kId,
                       std::string source = pmm::source::SteamWorkshop) {
    AcquisitionJob job;
    job.id = 7;
    job.identity = ModIdentity{"stellaris", std::move(source), std::move(id)};
    job.state = state;
    job.createdAt = kT0 - 5000;
    job.updatedAt = kT0 - 5000;
    return job;
}

obs::ItemState item(std::uint32_t flags, std::string path = {}, std::uint64_t done = 0,
                    std::uint64_t total = 0, std::string id = kId) {
    obs::ItemState o;
    o.status.id = std::move(id);
    o.status.flags = flags;
    o.status.installPath = std::move(path);
    o.status.bytesDownloaded = done;
    o.status.bytesTotal = total;
    return o;
}

// An installed item as Steam reports it, with the installed version's time.
obs::ItemState installedAt(std::uint32_t flags, std::uint32_t timestamp) {
    obs::ItemState o = item(flags, kPath);
    o.status.timestamp = timestamp;
    return o;
}

constexpr std::uint32_t kOld = 1'700'000'000;
constexpr std::uint32_t kNew = 1'700'000'500;

AcquisitionJob updateJob(std::int64_t target = kNew) {
    AcquisitionJob job = makeJob();
    job.kind = AcquisitionKind::Update;
    job.targetTimeUpdated = target;
    job.targetManifest = target ? "1100000000000000601" : "";
    return job;
}

AcquisitionJob reinstallJob(ReinstallStep step = ReinstallStep::Start) {
    AcquisitionJob job = makeJob();
    job.kind = AcquisitionKind::Reinstall;
    job.reinstallStep = step;
    return job;
}

// Feeds observations one at a time, like the application driver.
struct Machine {
    AcquisitionPolicy policy;
    AcquisitionContext ctx;
    Transition last;

    explicit Machine(const AcquisitionJob& job = makeJob(), AcquisitionPolicy p = {}) : policy(p) {
        last = startAcquisition(job, kT0, policy);
        ctx = last.context;
    }

    Transition& feed(const Observation& o) {
        last = reduceAcquisition(ctx, o, policy);
        ctx = last.context;
        return last;
    }

    Transition& tickTo(std::int64_t offsetMs) { return feed(obs::Tick{kT0 + offsetMs}); }

    S state() const { return ctx.job.state; }
    AwaitStep awaiting() const { return ctx.awaiting; }

    std::vector<K> actions() const {
        std::vector<K> out;
        for (const auto& a : last.actions)
            out.push_back(a.kind);
        return out;
    }
    bool emitted(K kind) const { return actions() == std::vector<K>{kind}; }
    bool quiet() const { return last.actions.empty(); }
};

// Brings a fresh machine to the point where the session is ready and the
// state query has been sent.
Machine readyMachine(bool loggedOn = true, bool ownsApp = true, AcquisitionPolicy p = {}) {
    Machine m(makeJob(), p);
    m.feed(obs::ProviderReady{loggedOn, ownsApp});
    return m;
}

} // namespace

TEST_CASE("A fresh job establishes the session first", "[providers][acquisition]") {
    Machine m;
    CHECK(m.emitted(K::QuerySession));
    CHECK(m.awaiting() == AwaitStep::Session);
    CHECK(m.state() == S::Pending);
    CHECK(m.ctx.job.attempts == 1);
    CHECK(m.last.persist);
    CHECK(m.last.actions[0].itemId == kId);

    m.feed(obs::ProviderReady{true, true});
    CHECK(m.emitted(K::QueryState));
    CHECK(m.awaiting() == AwaitStep::State);
    CHECK(m.ctx.sessionKnown);
}

TEST_CASE("Missing item: subscribe, download, verify, then installed", "[providers][acquisition]") {
    Machine m = readyMachine();

    m.feed(item(0));
    CHECK(m.emitted(K::Subscribe));
    CHECK(m.state() == S::Subscribing);

    m.feed(obs::SubscribeResult{1});
    CHECK(m.emitted(K::QueryState));
    CHECK(m.state() == S::Subscribed);

    m.feed(item(kSub));
    CHECK(m.emitted(K::RequestDownload));
    CHECK(m.last.actions[0].highPriority);
    CHECK(m.state() == S::Subscribed); // subscribed is not "download requested"

    m.feed(obs::DownloadAccepted{true});
    CHECK(m.emitted(K::QueryState));
    CHECK(m.state() == S::DownloadRequested);
    CHECK(m.awaiting() == AwaitStep::Progress);

    m.feed(item(kSub | kPend));
    CHECK(m.quiet());
    CHECK(m.state() == S::DownloadRequested);

    m.tickTo(500);
    CHECK(m.quiet()); // poll interval (1 s) not reached
    m.tickTo(1000);
    CHECK(m.emitted(K::QueryState));

    m.feed(item(kSub | kDl, {}, 0, 1000));
    CHECK(m.state() == S::Downloading);
    m.feed(item(kSub | kDl, {}, 500, 1000));
    CHECK(m.ctx.job.bytesDownloaded == 500);
    CHECK(m.ctx.job.bytesTotal == 1000);
    CHECK(m.last.persist);

    m.feed(item(kSub | kInst, kPath, 1000, 1000));
    CHECK(m.emitted(K::VerifyContent));
    CHECK(m.last.actions[0].installPath == kPath);
    CHECK(m.state() != S::Installed); // never before the content check

    m.feed(obs::ContentVerified{true, {}});
    CHECK(m.quiet());
    CHECK(m.state() == S::Installed);
    CHECK(m.awaiting() == AwaitStep::Nothing);
    CHECK(m.ctx.job.errorCode.empty());
    CHECK(m.ctx.job.bytesDownloaded == m.ctx.job.bytesTotal);
}

TEST_CASE("State query decides the first provider operation", "[providers][acquisition]") {
    struct Row {
        const char* name;
        std::uint32_t flags;
        bool withPath;
        K action;
        S state;
        AwaitStep awaiting;
    };
    const Row rows[] = {
        {"unknown item", 0, false, K::Subscribe, S::Subscribing, AwaitStep::SubscribeResult},
        {"installed, not subscribed", kInst, true, K::Subscribe, S::Subscribing,
         AwaitStep::SubscribeResult},
        {"subscribed only", kSub, false, K::RequestDownload, S::Subscribed,
         AwaitStep::DownloadAccepted},
        {"subscribed, needs update", kSub | kInst | kUpd, true, K::RequestDownload, S::Subscribed,
         AwaitStep::DownloadAccepted},
        {"installed without install path", kSub | kInst, false, K::RequestDownload, S::Subscribed,
         AwaitStep::DownloadAccepted},
        {"already installed", kSub | kInst, true, K::VerifyContent, S::Subscribed,
         AwaitStep::Verification},
        {"legacy installed", kSub | kInst | item_flag::LegacyItem, true, K::VerifyContent,
         S::Subscribed, AwaitStep::Verification},
        {"already downloading", kSub | kDl, false, K::None, S::Downloading, AwaitStep::Progress},
        {"queued", kSub | kPend, false, K::None, S::DownloadRequested, AwaitStep::Progress},
        {"updating", kSub | kInst | kUpd | kDl, true, K::None, S::Downloading, AwaitStep::Progress},
    };
    for (const Row& row : rows) {
        INFO(row.name);
        Machine m = readyMachine();
        m.feed(item(row.flags, row.withPath ? kPath : std::string()));
        if (row.action == K::None)
            CHECK(m.quiet());
        else
            CHECK(m.emitted(row.action));
        CHECK(m.state() == row.state);
        CHECK(m.awaiting() == row.awaiting);
    }
}

TEST_CASE("Installed content is verified, never re-downloaded", "[providers][acquisition]") {
    Machine m = readyMachine();
    m.feed(item(kSub | kInst, kPath));
    CHECK(m.emitted(K::VerifyContent));
    m.feed(obs::ContentVerified{true, {}});
    CHECK(m.state() == S::Installed);

    // Later reports for an installed job change nothing.
    for (const Observation& o :
         {Observation{item(0)}, Observation{obs::SubscribeResult{9}}, Observation{obs::Cancel{}},
          Observation{obs::Retry{}}, Observation{obs::ProviderUnavailable{}},
          Observation{obs::Tick{kT0 + 10'000'000}},
          Observation{obs::ItemRejected{ErrorCode::ItemUnavailable, "gone"}},
          Observation{obs::ManualFallback{}}}) {
        m.feed(o);
        CHECK(m.quiet());
        CHECK(m.state() == S::Installed);
    }
}

TEST_CASE("Unusable installed content fails without automatic retry", "[providers][acquisition]") {
    Machine m = readyMachine();
    m.feed(item(kSub | kInst, kPath));
    m.feed(obs::ContentVerified{false, "descriptor.mod is missing from the downloaded content"});
    CHECK(m.state() == S::Failed);
    CHECK(m.ctx.job.errorCode == "validation_failed");
    CHECK(m.ctx.job.errorMessage.find("descriptor.mod is missing") != std::string::npos);
    CHECK_FALSE(m.last.retryAfterMs.has_value());
    m.tickTo(10'000'000);
    CHECK(m.quiet());
    CHECK(m.state() == S::Failed);
}

TEST_CASE("Provider unavailability maps to a concrete recovery state", "[providers][acquisition]") {
    using R = obs::UnavailableReason;
    struct Row {
        R reason;
        S state;
        const char* hint;
    };
    const Row rows[] = {
        {R::SteamNotRunning, S::NeedsUserAction, "Start Steam"},
        {R::NotLoggedOn, S::NeedsUserAction, "Sign in to Steam"},
        {R::InitFailed, S::NeedsUserAction, "owns the game"},
        {R::LibraryMissing, S::NeedsUserAction, "Verify"},
        {R::Incompatible, S::NeedsUserAction, "manual Workshop page"},
        {R::HelperFailed, S::Failed, "Retrying automatically"},
    };
    for (const Row& row : rows) {
        INFO(row.hint);
        Machine m;
        m.feed(obs::ProviderUnavailable{row.reason, "technical"});
        CHECK(m.quiet());
        CHECK(m.state() == row.state);
        CHECK(m.awaiting() == AwaitStep::Nothing);
        CHECK(m.ctx.job.errorCode == "provider_unavailable");
        CHECK(m.ctx.job.errorMessage.find(row.hint) != std::string::npos);
        CHECK(m.ctx.job.errorMessage.find("technical") == std::string::npos);
        CHECK_FALSE(m.ctx.sessionKnown);
    }
}

TEST_CASE("Network operations require a signed-in owner", "[providers][acquisition]") {
    SECTION("offline mode blocks subscribe") {
        Machine m = readyMachine(false, true);
        m.feed(item(0));
        CHECK(m.quiet());
        CHECK(m.state() == S::NeedsUserAction);
        CHECK(m.ctx.job.errorMessage.find("offline") != std::string::npos);
    }
    SECTION("offline mode blocks download") {
        Machine m = readyMachine(false, true);
        m.feed(item(kSub));
        CHECK(m.state() == S::NeedsUserAction);
    }
    SECTION("not owning the game blocks download") {
        Machine m = readyMachine(true, false);
        m.feed(item(kSub));
        CHECK(m.state() == S::NeedsUserAction);
        CHECK(m.ctx.job.errorCode == "access_denied");
        CHECK(m.ctx.job.errorMessage.find("does not own") != std::string::npos);
    }
    SECTION("offline mode still accepts already installed content") {
        Machine m = readyMachine(false, false);
        m.feed(item(kSub | kInst, kPath));
        CHECK(m.emitted(K::VerifyContent));
        m.feed(obs::ContentVerified{true, {}});
        CHECK(m.state() == S::Installed);
    }
}

TEST_CASE("Subscribe results are classified", "[providers][acquisition]") {
    struct Row {
        int eresult;
        S state;
        const char* code;
        bool retryScheduled;
    };
    const Row rows[] = {
        {9, S::Unavailable, "item_unavailable", false},
        {8, S::Unavailable, "item_unavailable", false},
        {15, S::Unavailable, "access_denied", false},
        {17, S::Unavailable, "access_denied", false},
        {86, S::Unavailable, "item_unavailable", false},
        {21, S::NeedsUserAction, "provider_unavailable", false},
        {25, S::NeedsUserAction, "limit_exceeded", false},
        {54, S::NeedsUserAction, "io_error", false},
        {2, S::Failed, "provider_unavailable", true},
        {3, S::Failed, "offline", true},
        {10, S::Failed, "busy", true},
        {16, S::Failed, "timeout", true},
        {20, S::Failed, "provider_unavailable", true},
        {12345, S::Failed, "provider_unavailable", true},
    };
    for (const Row& row : rows) {
        INFO("EResult " << row.eresult);
        Machine m = readyMachine();
        m.feed(item(0));
        m.feed(obs::SubscribeResult{row.eresult});
        CHECK(m.quiet());
        CHECK(m.state() == row.state);
        CHECK(m.ctx.job.errorCode == row.code);
        CHECK(m.ctx.job.errorMessage.find("Subscribing to Workshop item " + kId) == 0);
        CHECK(m.last.retryAfterMs.has_value() == row.retryScheduled);
        CHECK(m.awaiting() == AwaitStep::Nothing);
    }
}

TEST_CASE("A rejected download request is a retryable failure", "[providers][acquisition]") {
    Machine m = readyMachine();
    m.feed(item(kSub));
    m.feed(obs::DownloadAccepted{false});
    CHECK(m.state() == S::Failed);
    CHECK(m.last.retryAfterMs == std::int64_t{5000});
}

TEST_CASE("Automatic retries back off and are capped", "[providers][acquisition]") {
    Machine m = readyMachine();
    m.feed(item(0));
    m.feed(obs::SubscribeResult{3}); // offline
    REQUIRE(m.state() == S::Failed);
    CHECK(m.last.retryAfterMs == std::int64_t{5000});
    CHECK(m.ctx.job.errorMessage.find("attempt 2 of 3") != std::string::npos);

    m.tickTo(4999);
    CHECK(m.quiet());
    CHECK(m.state() == S::Failed);
    m.tickTo(5000);
    CHECK(m.emitted(K::QueryState)); // session already known: straight to the state query
    CHECK(m.state() == S::Pending);
    CHECK(m.ctx.job.attempts == 2);
    CHECK(m.ctx.job.errorMessage.empty());

    m.feed(item(0));
    m.feed(obs::SubscribeResult{16});
    CHECK(m.last.retryAfterMs == std::int64_t{10'000});
    m.tickTo(5000 + 10'000);
    CHECK(m.emitted(K::QueryState));
    CHECK(m.ctx.job.attempts == 3);

    m.feed(item(0));
    m.feed(obs::SubscribeResult{16});
    CHECK(m.state() == S::Failed);
    CHECK_FALSE(m.last.retryAfterMs.has_value());
    CHECK(m.ctx.job.errorMessage.find("exhausted") != std::string::npos);
    m.tickTo(100'000'000);
    CHECK(m.quiet());
    CHECK(m.state() == S::Failed);

    // A user retry starts a fresh cycle.
    m.feed(obs::Retry{});
    CHECK(m.emitted(K::QueryState));
    CHECK(m.state() == S::Pending);
    CHECK(m.ctx.job.attempts == 1);
}

TEST_CASE("Retry delay doubles up to the cap", "[providers][acquisition]") {
    AcquisitionPolicy p;
    CHECK(retryDelayMs(1, p) == 5'000);
    CHECK(retryDelayMs(2, p) == 10'000);
    CHECK(retryDelayMs(3, p) == 20'000);
    CHECK(retryDelayMs(5, p) == 80'000);
    CHECK(retryDelayMs(6, p) == 120'000);
    CHECK(retryDelayMs(60, p) == 120'000);
    p.retryBaseDelayMs = 0;
    CHECK(retryDelayMs(4, p) == 0);
}

TEST_CASE("Stalled downloads fail with a retry hint", "[providers][acquisition]") {
    AcquisitionPolicy p;
    p.stallTimeoutMs = 60'000;
    Machine m = readyMachine(true, true, p);
    m.feed(item(kSub));
    m.feed(obs::DownloadAccepted{true});
    m.tickTo(30'000);
    m.feed(item(kSub | kDl, {}, 10, 100)); // progress at t=30 s resets the stall timer
    m.tickTo(60'000);
    CHECK(m.state() == S::Downloading);
    m.feed(item(kSub | kDl, {}, 10, 100)); // no change
    m.tickTo(89'999);
    CHECK(m.state() == S::Downloading);
    m.tickTo(90'000);
    CHECK(m.state() == S::Failed);
    CHECK(m.ctx.job.errorCode == "timeout");
    CHECK(m.ctx.job.errorMessage.find("no progress for 1 minute") != std::string::npos);
    CHECK(m.ctx.job.errorMessage.find("Downloads page") != std::string::npos);
    CHECK(m.last.retryAfterMs.has_value());
}

TEST_CASE("Unanswered requests time out", "[providers][acquisition]") {
    SECTION("helper never becomes ready") {
        Machine m;
        m.tickTo(29'999);
        CHECK(m.state() == S::Pending);
        m.tickTo(30'000);
        CHECK(m.state() == S::Failed);
        CHECK(m.ctx.job.errorCode == "timeout");
    }
    SECTION("subscribe result never arrives") {
        Machine m = readyMachine();
        m.feed(item(0));
        m.tickTo(59'999);
        CHECK(m.state() == S::Subscribing);
        m.tickTo(60'000);
        CHECK(m.state() == S::Failed);
    }
    SECTION("verification never reports back") {
        Machine m = readyMachine();
        m.feed(item(kSub | kInst, kPath));
        m.tickTo(30'000);
        CHECK(m.state() == S::Failed);
    }
    SECTION("time going backwards is ignored") {
        Machine m;
        m.tickTo(20'000);
        m.feed(obs::Tick{0});
        CHECK(m.ctx.clockMs == kT0 + 20'000);
        m.tickTo(29'000);
        CHECK(m.state() == S::Pending);
    }
}

TEST_CASE("Cancel explains what remains in Steam", "[providers][acquisition]") {
    struct Row {
        const char* phase;
        int steps; // how far to drive the happy path before cancelling
        const char* expected;
    };
    const Row rows[] = {
        {"before any request", 0, "before anything was requested"},
        {"subscribe in flight", 2, "may still complete"},
        {"subscribed", 3, "subscription made for this item remains"},
        {"download requested", 5, "may finish the download"},
        {"downloading", 6, "may finish the download"},
    };
    for (const Row& row : rows) {
        INFO(row.phase);
        Machine m;
        const Observation path[] = {obs::ProviderReady{true, true}, item(0),
                                    obs::SubscribeResult{1},        item(kSub),
                                    obs::DownloadAccepted{true},    item(kSub | kDl, {}, 1, 10)};
        for (int i = 0; i < row.steps; ++i)
            m.feed(path[i]);
        m.feed(obs::Cancel{});
        CHECK(m.quiet());
        CHECK(m.state() == S::Cancelled);
        CHECK(m.awaiting() == AwaitStep::Nothing);
        CHECK(m.ctx.job.errorCode == "cancelled");
        CHECK(m.ctx.job.errorMessage.find(row.expected) != std::string::npos);

        // Late answers to cancelled work are ignored.
        m.feed(obs::SubscribeResult{1});
        m.feed(obs::DownloadAccepted{true});
        m.feed(item(kSub | kInst, kPath));
        m.tickTo(10'000'000);
        CHECK(m.quiet());
        CHECK(m.state() == S::Cancelled);
    }
}

TEST_CASE("Restored jobs are reconciled with a state query, never re-subscribed blindly",
          "[providers][acquisition]") {
    for (S persisted :
         {S::Pending, S::Subscribing, S::Subscribed, S::DownloadRequested, S::Downloading}) {
        INFO(toString(persisted));
        AcquisitionJob job = makeJob(persisted);
        job.attempts = 2;
        Machine m(job);
        CHECK(m.emitted(K::QuerySession));
        CHECK(m.ctx.job.attempts == 2); // resuming is not a new attempt
        CHECK(m.state() == persisted);

        m.feed(obs::ProviderReady{true, true});
        CHECK(m.emitted(K::QueryState));

        m.feed(item(kSub | kInst, kPath)); // finished while the app was closed
        CHECK(m.emitted(K::VerifyContent));
        m.feed(obs::ContentVerified{true, {}});
        CHECK(m.state() == S::Installed);
    }
}

TEST_CASE("Restored terminal jobs stay put", "[providers][acquisition]") {
    for (S persisted :
         {S::Installed, S::Failed, S::Unavailable, S::NeedsUserAction, S::Cancelled}) {
        INFO(toString(persisted));
        Machine m(makeJob(persisted));
        CHECK(m.quiet());
        CHECK(m.state() == persisted);
        CHECK(m.awaiting() == AwaitStep::Nothing);
        CHECK_FALSE(m.last.persist);
    }
}

TEST_CASE("Jobs that cannot be acquired automatically stop immediately",
          "[providers][acquisition]") {
    Machine invalid(makeJob(S::Pending, "12abc"));
    CHECK(invalid.quiet());
    CHECK(invalid.state() == S::Unavailable);
    CHECK(invalid.ctx.job.errorCode == "invalid_argument");

    Machine local(makeJob(S::Pending, "sha256:abcd", pmm::source::Local));
    CHECK(local.quiet());
    CHECK(local.state() == S::NeedsUserAction);
    CHECK(local.ctx.job.errorMessage.find("Local mods") != std::string::npos);

    // Retry re-validates instead of reaching the provider.
    invalid.feed(obs::Retry{});
    CHECK(invalid.quiet());
    CHECK(invalid.state() == S::Unavailable);
}

TEST_CASE("Metadata rejection makes the job unavailable", "[providers][acquisition]") {
    Machine m;
    m.feed(obs::ItemRejected{ErrorCode::WrongGame, "belongs to a different game"});
    CHECK(m.quiet());
    CHECK(m.state() == S::Unavailable);
    CHECK(m.ctx.job.errorCode == "wrong_game");
    CHECK(m.ctx.job.errorMessage == "belongs to a different game");
}

TEST_CASE("Manual fallback opens the page and a rescan completes the job",
          "[providers][acquisition]") {
    SECTION("rescan finds the item installed") {
        Machine m = readyMachine(false, true);
        m.feed(item(0)); // offline: cannot subscribe automatically
        REQUIRE(m.state() == S::NeedsUserAction);

        m.feed(obs::ManualFallback{});
        CHECK(m.emitted(K::OpenFallbackPage));
        CHECK(m.state() == S::NeedsUserAction);
        CHECK(m.ctx.job.errorMessage.find("Manual fallback") != std::string::npos);

        m.feed(item(kInst, kPath)); // installed but not subscribed: not accepted
        CHECK(m.quiet());
        m.feed(item(kSub | kInst, kPath));
        CHECK(m.emitted(K::VerifyContent));
        CHECK(m.state() == S::Subscribed);
        m.feed(obs::ContentVerified{true, {}});
        CHECK(m.state() == S::Installed);
    }
    SECTION("rescan finds Steam downloading it") {
        Machine m;
        m.feed(obs::ManualFallback{});
        m.feed(item(kSub | kDl, {}, 5, 50));
        CHECK(m.state() == S::Downloading);
        CHECK(m.awaiting() == AwaitStep::Progress);
        CHECK(m.ctx.job.bytesDownloaded == 5);
    }
}

TEST_CASE("A helper restart mid-request re-queries the state", "[providers][acquisition]") {
    Machine m = readyMachine();
    m.feed(item(0));
    REQUIRE(m.awaiting() == AwaitStep::SubscribeResult);
    m.feed(obs::ProviderReady{true, true});
    CHECK(m.emitted(K::QueryState));
    m.feed(item(kSub)); // the earlier subscribe went through
    CHECK(m.emitted(K::RequestDownload));
}

TEST_CASE("Unsubscribing in Steam during a download needs the user", "[providers][acquisition]") {
    Machine m = readyMachine();
    m.feed(item(kSub));
    m.feed(obs::DownloadAccepted{true});
    m.feed(item(0));
    CHECK(m.state() == S::NeedsUserAction);
    CHECK(m.ctx.job.errorMessage.find("no longer subscribed") != std::string::npos);
}

TEST_CASE("Stale and foreign observations are ignored", "[providers][acquisition]") {
    Machine m = readyMachine();
    const AcquisitionContext before = m.ctx;

    m.feed(item(kSub | kInst, kPath, 0, 0, "999"));
    CHECK(m.quiet());
    m.feed(obs::SubscribeResult{1});
    CHECK(m.quiet());
    m.feed(obs::DownloadAccepted{true});
    CHECK(m.quiet());
    m.feed(obs::ContentVerified{true, {}});
    CHECK(m.quiet());
    CHECK(m.state() == before.job.state);
    CHECK(m.awaiting() == AwaitStep::State);
}

TEST_CASE("Every transition keeps the machine's invariants", "[providers][acquisition]") {
    const S nonTerminal[] = {S::Pending,    S::Unsubscribing,     S::Subscribing,
                             S::Subscribed, S::DownloadRequested, S::Downloading};
    const S terminal[] = {S::Installed, S::Failed, S::Unavailable, S::NeedsUserAction,
                          S::Cancelled};
    const AwaitStep busy[] = {AwaitStep::Session,           AwaitStep::State,
                              AwaitStep::SubscribeResult,   AwaitStep::DownloadAccepted,
                              AwaitStep::Progress,          AwaitStep::Verification,
                              AwaitStep::UnsubscribeResult, AwaitStep::Removal};
    struct Kind {
        AcquisitionKind kind;
        ReinstallStep step;
    };
    const Kind kinds[] = {{AcquisitionKind::Acquire, ReinstallStep::Start},
                          {AcquisitionKind::Update, ReinstallStep::Start},
                          {AcquisitionKind::Reinstall, ReinstallStep::Start},
                          {AcquisitionKind::Reinstall, ReinstallStep::Unsubscribing},
                          {AcquisitionKind::Reinstall, ReinstallStep::Unsubscribed},
                          {AcquisitionKind::Reinstall, ReinstallStep::Resubscribed}};
    const std::vector<Observation> observations{
        obs::ProviderReady{true, true},
        obs::ProviderReady{false, false},
        obs::ProviderUnavailable{obs::UnavailableReason::SteamNotRunning, {}},
        obs::ProviderUnavailable{obs::UnavailableReason::HelperFailed, {}},
        item(0),
        item(kInst, kPath),
        item(kSub),
        item(kSub | kPend),
        item(kSub | kDl, {}, 3, 9),
        item(kSub | kInst, kPath),
        item(kSub | kInst | kUpd, kPath),
        installedAt(kSub | kInst, kNew),
        installedAt(kInst, kOld),
        obs::SubscribeResult{1},
        obs::UnsubscribeResult{1},
        obs::UnsubscribeResult{9},
        obs::Tick{kT0 + 30'000},
        obs::SubscribeResult{9},
        obs::SubscribeResult{3},
        obs::DownloadAccepted{true},
        obs::DownloadAccepted{false},
        obs::ContentVerified{true, {}},
        obs::ContentVerified{false, "bad"},
        obs::ItemRejected{ErrorCode::WrongGame, "wrong"},
        obs::Tick{kT0 + 1},
        obs::Tick{kT0 + 100'000'000},
        obs::Cancel{},
        obs::Retry{},
        obs::ManualFallback{},
    };

    std::vector<AcquisitionContext> contexts;
    for (const Kind& kind : kinds) {
        for (S s : nonTerminal) {
            for (AwaitStep step : busy) {
                for (bool session : {false, true}) {
                    AcquisitionContext c;
                    c.job = makeJob(s);
                    c.job.kind = kind.kind;
                    c.job.reinstallStep = kind.step;
                    c.job.targetTimeUpdated = kind.kind == AcquisitionKind::Acquire ? 0 : kNew;
                    c.job.attempts = 1;
                    c.awaiting = step;
                    c.clockMs = kT0;
                    c.awaitingSinceMs = kT0;
                    c.lastProgressAtMs = kT0;
                    c.lastPollAtMs = kT0;
                    c.sessionKnown = session;
                    c.loggedOn = session;
                    c.ownsApp = session;
                    c.installPath = step == AwaitStep::Verification ? kPath : std::string();
                    // Update and reinstall: Steam calls the old version current.
                    c.currentButOld = kind.kind != AcquisitionKind::Acquire && session;
                    c.downloadRequested = c.currentButOld;
                    contexts.push_back(c);
                }
            }
        }
        for (S s : terminal) {
            AcquisitionContext c;
            c.job = makeJob(s);
            c.job.kind = kind.kind;
            c.job.reinstallStep = kind.step;
            c.job.attempts = 3;
            c.clockMs = kT0;
            contexts.push_back(c);
        }
    }

    std::size_t checked = 0;
    for (const auto& c : contexts) {
        for (std::size_t i = 0; i < observations.size(); ++i) {
            INFO("kind " << toString(c.job.kind) << ", step " << toString(c.job.reinstallStep)
                         << ", state " << toString(c.job.state) << ", awaiting "
                         << toString(c.awaiting) << ", session " << c.sessionKnown
                         << ", observation #" << i);
            const Transition t = reduceAcquisition(c, observations[i]);
            const S next = t.context.job.state;

            CHECK(t.actions.size() <= 1);
            CHECK(isTerminal(next) == (t.context.awaiting == AwaitStep::Nothing));

            const bool verifiedOk = std::holds_alternative<obs::ContentVerified>(observations[i]) &&
                                    std::get<obs::ContentVerified>(observations[i]).ok &&
                                    c.awaiting == AwaitStep::Verification;
            if (next == S::Installed)
                CHECK((verifiedOk || c.job.state == S::Installed));

            const bool tick = std::holds_alternative<obs::Tick>(observations[i]);
            for (const Action& a : t.actions) {
                CHECK(a.itemId == kId);
                CHECK(a.kind != K::None);
                if (a.kind == K::Subscribe || a.kind == K::VerifyContent ||
                    a.kind == K::Unsubscribe ||
                    (a.kind == K::RequestDownload &&
                     !(tick && c.job.kind != AcquisitionKind::Acquire && c.currentButOld))) {
                    // Decided only from a fresh state report (an update's one
                    // request more comes from the clock).
                    CHECK(std::holds_alternative<obs::ItemState>(observations[i]));
                }
                // Only a reinstall unsubscribes, only before Steam confirmed
                // it, and an update never subscribes.
                if (a.kind == K::Unsubscribe) {
                    CHECK(c.job.kind == AcquisitionKind::Reinstall);
                    CHECK((c.job.reinstallStep == ReinstallStep::Start ||
                           c.job.reinstallStep == ReinstallStep::Unsubscribing));
                    CHECK(t.context.job.reinstallStep == ReinstallStep::Unsubscribing);
                }
                if (a.kind == K::Subscribe)
                    CHECK(c.job.kind != AcquisitionKind::Update);
                if (a.kind == K::VerifyContent)
                    CHECK(a.installPath == kPath);
                if (c.job.state == S::Installed)
                    CHECK(false); // installed jobs never act again
            }
            if (t.persist)
                CHECK(t.context.job.updatedAt >= c.clockMs);
            ++checked;
        }
    }
    CHECK(checked == contexts.size() * observations.size());
}

TEST_CASE("An update downloads installed content even when Steam calls it current",
          "[providers][acquisition]") {
    Machine m(updateJob());
    m.feed(obs::ProviderReady{true, true});
    m.feed(installedAt(kSub | kInst, kOld)); // Steam has not noticed the new version
    CHECK(m.emitted(K::RequestDownload));
    CHECK(m.last.actions[0].highPriority);
    CHECK(m.state() == S::Pending);
    m.feed(obs::DownloadAccepted{true});
    CHECK(m.state() == S::DownloadRequested);

    // Steam fetches the new version.
    m.feed(item(kSub | kInst | kPend, kPath));
    m.feed(item(kSub | kDl, {}, 10, 20));
    CHECK(m.state() == S::Downloading);
    m.feed(installedAt(kSub | kInst, kNew));
    CHECK(m.emitted(K::VerifyContent));
    m.feed(obs::ContentVerified{true, {}});
    CHECK(m.state() == S::Installed);
    CHECK(m.ctx.job.errorCode.empty());
}

TEST_CASE("An update Steam flagged runs through Steam's own download", "[providers][acquisition]") {
    for (std::int64_t target : {std::int64_t{kNew}, std::int64_t{0}}) {
        INFO("target " << target);
        Machine m(updateJob(target));
        m.feed(obs::ProviderReady{true, true});
        m.feed(installedAt(kSub | kInst | kUpd, kOld));
        CHECK(m.emitted(K::RequestDownload));
        m.feed(obs::DownloadAccepted{true});
        m.feed(item(kSub | kInst | kUpd | kDl, kPath, 5, 10));
        CHECK(m.state() == S::Downloading);
        m.feed(installedAt(kSub | kInst, kNew));
        CHECK(m.emitted(K::VerifyContent)); // without a target: current after the request
        m.feed(obs::ContentVerified{true, {}});
        CHECK(m.state() == S::Installed);
    }
}

TEST_CASE("An update finds the installed version already at its target",
          "[providers][acquisition]") {
    Machine m(updateJob());
    m.feed(obs::ProviderReady{true, true});
    m.feed(installedAt(kSub | kInst, kNew)); // Steam updated it meanwhile
    CHECK(m.emitted(K::VerifyContent));
    m.feed(obs::ContentVerified{true, {}});
    CHECK(m.state() == S::Installed);

    // A download already running is followed, not requested again.
    Machine running(updateJob());
    running.feed(obs::ProviderReady{true, true});
    running.feed(item(kSub | kInst | kDl, kPath, 1, 4));
    CHECK(running.quiet());
    CHECK(running.state() == S::Downloading);
    CHECK(running.awaiting() == AwaitStep::Progress);
}

TEST_CASE("An update asks once more, then fails with update_not_seen", "[providers][acquisition]") {
    Machine m(updateJob());
    m.feed(obs::ProviderReady{true, true});
    m.feed(installedAt(kSub | kInst, kOld));
    m.feed(obs::DownloadAccepted{true});
    // Steam calls the old version current ("installed nothing").
    for (int t = 1000; t < 30'000; t += 1000) {
        m.tickTo(t);
        if (m.emitted(K::QueryState))
            m.feed(installedAt(kSub | kInst, kOld));
        CHECK(m.state() == S::DownloadRequested);
        CHECK(m.ctx.job.errorCode.empty());
    }
    m.tickTo(30'000);
    CHECK(m.emitted(K::RequestDownload)); // the second and last request
    CHECK(m.awaiting() == AwaitStep::DownloadAccepted);
    m.feed(obs::DownloadAccepted{true});
    for (int t = 31'000; t < 60'000; t += 1000) {
        m.tickTo(t);
        if (m.emitted(K::QueryState))
            m.feed(installedAt(kSub | kInst, kOld));
        CHECK(m.state() == S::DownloadRequested);
    }
    m.tickTo(61'000);
    CHECK(m.quiet());
    CHECK(m.state() == S::Failed);
    CHECK(m.awaiting() == AwaitStep::Nothing);
    CHECK(m.ctx.job.errorCode == "update_not_seen");
    CHECK(m.ctx.job.errorMessage ==
          "Steam has not picked up this update yet. Steam checks for updates about every 45 "
          "minutes; restarting Steam makes it check right away.");
    CHECK_FALSE(m.last.retryAfterMs.has_value()); // no automatic retry: the user retries
    m.tickTo(10'000'000);
    CHECK(m.quiet());

    // Retry starts over, with one more re-request in the new run.
    m.feed(obs::Retry{});
    CHECK(m.emitted(K::QueryState));
    m.feed(installedAt(kSub | kInst, kOld));
    CHECK(m.emitted(K::RequestDownload));
    m.feed(obs::DownloadAccepted{true});
    m.feed(installedAt(kSub | kInst, kNew));
    CHECK(m.emitted(K::VerifyContent));
}

TEST_CASE("An update never subscribes", "[providers][acquisition]") {
    for (std::uint32_t flags : {std::uint32_t{0}, kInst, kInst | kUpd}) {
        INFO("flags " << flags);
        Machine m(updateJob());
        m.feed(obs::ProviderReady{true, true});
        m.feed(installedAt(flags, kOld));
        CHECK(m.quiet());
        CHECK(m.state() == S::NeedsUserAction);
        CHECK(m.ctx.job.errorMessage.find("not subscribed in Steam") != std::string::npos);
        m.feed(obs::Retry{});
        m.feed(installedAt(flags, kOld));
        CHECK(m.quiet()); // a retry does not subscribe either
    }
    SECTION("unsubscribed while it downloads") {
        Machine m(updateJob());
        m.feed(obs::ProviderReady{true, true});
        m.feed(installedAt(kSub | kInst, kOld));
        m.feed(obs::DownloadAccepted{true});
        m.feed(installedAt(kInst, kOld));
        CHECK(m.state() == S::NeedsUserAction);
        CHECK(m.ctx.job.errorMessage.find("no longer subscribed") != std::string::npos);
        // A re-download does not help: it never subscribes an unsubscribed item either.
        CHECK(m.ctx.job.errorMessage.find("Subscribe to it on its Workshop page, then retry.") !=
              std::string::npos);
        CHECK(m.ctx.job.errorMessage.find("Re-download") == std::string::npos);
    }
    SECTION("offline: nothing is requested") {
        Machine m(updateJob());
        m.feed(obs::ProviderReady{false, true});
        m.feed(installedAt(kSub | kInst | kUpd, kOld));
        CHECK(m.quiet());
        CHECK(m.state() == S::NeedsUserAction);
    }
}

TEST_CASE("A re-download unsubscribes, waits, subscribes again and downloads",
          "[providers][acquisition]") {
    AcquisitionPolicy policy;
    policy.removalGraceMs = 5000;
    Machine m(reinstallJob(), policy);
    m.feed(obs::ProviderReady{true, true});
    m.feed(installedAt(kSub | kInst, kOld));
    CHECK(m.emitted(K::Unsubscribe));
    CHECK(m.state() == S::Unsubscribing);
    CHECK(m.ctx.job.reinstallStep == ReinstallStep::Unsubscribing); // stored before it is sent
    CHECK(m.last.persist);

    m.feed(obs::UnsubscribeResult{1});
    CHECK(m.emitted(K::QueryState));
    CHECK(m.awaiting() == AwaitStep::Removal);
    CHECK(m.ctx.job.reinstallStep == ReinstallStep::Unsubscribed); // persisted at once
    CHECK(m.last.persist);

    // Still subscribed: keep asking.
    m.feed(installedAt(kSub | kInst, kOld));
    CHECK(m.quiet());
    m.tickTo(1000);
    CHECK(m.emitted(K::QueryState));
    // Dropped, files still there: a short grace period for Steam to remove them.
    m.feed(installedAt(kInst, kOld));
    CHECK(m.quiet());
    m.tickTo(2000);
    m.feed(installedAt(kInst, kOld));
    CHECK(m.quiet());
    m.tickTo(6000);
    CHECK(m.emitted(K::QueryState));
    m.feed(installedAt(kInst, kOld));
    CHECK(m.emitted(K::Subscribe)); // removed or not, subscribe again
    CHECK(m.state() == S::Subscribing);

    m.feed(obs::SubscribeResult{1});
    CHECK(m.ctx.job.reinstallStep == ReinstallStep::Resubscribed);
    CHECK(m.emitted(K::QueryState));
    m.feed(installedAt(kSub | kInst, kOld));
    CHECK(m.emitted(K::RequestDownload)); // even though Steam calls it installed
    m.feed(obs::DownloadAccepted{true});
    m.feed(item(kSub | kDl, {}, 3, 9));
    CHECK(m.state() == S::Downloading);
    m.feed(installedAt(kSub | kInst, kOld));
    CHECK(m.emitted(K::VerifyContent)); // no target: the version Steam has
    m.feed(obs::ContentVerified{true, {}});
    CHECK(m.state() == S::Installed);
    CHECK(m.ctx.job.reinstallStep == ReinstallStep::Resubscribed);
}

TEST_CASE("A re-download whose files Steam removes subscribes at once",
          "[providers][acquisition]") {
    Machine m(reinstallJob());
    m.feed(obs::ProviderReady{true, true});
    m.feed(installedAt(kSub | kInst, kOld));
    m.feed(obs::UnsubscribeResult{1});
    m.feed(item(0));
    CHECK(m.emitted(K::Subscribe));
}

TEST_CASE("A re-download never subscribes an item that was not subscribed",
          "[providers][acquisition]") {
    Machine m(reinstallJob());
    m.feed(obs::ProviderReady{true, true});
    m.feed(installedAt(kInst, kOld)); // installed, but the account is not subscribed
    CHECK(m.quiet());
    CHECK(m.state() == S::NeedsUserAction);
    CHECK(m.ctx.job.reinstallStep == ReinstallStep::Start);
    CHECK(m.ctx.job.errorMessage ==
          "Workshop item " + kId +
              " is not subscribed in Steam, so it was not downloaded again. Subscribe to it on "
              "its Workshop page, then retry.");
    m.feed(obs::Retry{});
    m.feed(installedAt(kInst, kOld));
    CHECK(m.quiet()); // a retry does not subscribe either
    // Subscribed on its Workshop page meanwhile: Retry re-downloads it.
    m.feed(obs::Retry{});
    m.feed(installedAt(kSub | kInst, kOld));
    CHECK(m.emitted(K::Unsubscribe));
}

TEST_CASE("A re-download that fails after unsubscribing says so, and Retry subscribes again",
          "[providers][acquisition]") {
    const std::string note = " is now unsubscribed in Steam: Retry subscribes to it again";
    SECTION("subscribing again fails") {
        Machine m(reinstallJob());
        m.feed(obs::ProviderReady{true, true});
        m.feed(installedAt(kSub | kInst, kOld));
        m.feed(obs::UnsubscribeResult{1});
        m.feed(item(0));
        m.feed(obs::SubscribeResult{16}); // timeout
        CHECK(m.state() == S::Failed);
        CHECK(m.ctx.job.errorMessage.find(kId + note) != std::string::npos);
        CHECK(m.last.retryAfterMs.has_value()); // retried automatically, too
        m.tickTo(5000);
        CHECK(m.emitted(K::QueryState));
        m.feed(item(0));
        CHECK(m.emitted(K::Subscribe)); // never a second unsubscribe
    }
    SECTION("Steam stops running") {
        Machine m(reinstallJob());
        m.feed(obs::ProviderReady{true, true});
        m.feed(installedAt(kSub | kInst, kOld));
        m.feed(obs::UnsubscribeResult{1});
        m.feed(obs::ProviderUnavailable{obs::UnavailableReason::SteamNotRunning, {}});
        CHECK(m.state() == S::NeedsUserAction);
        CHECK(m.ctx.job.errorMessage.find("Steam is not running") == 0);
        CHECK(m.ctx.job.errorMessage.find(note) != std::string::npos);
        m.feed(obs::Retry{});
        CHECK(m.emitted(K::QuerySession));
        m.feed(obs::ProviderReady{true, true});
        m.feed(item(kInst, kPath));
        CHECK(m.emitted(K::Subscribe));
    }
    SECTION("Steam does not drop the subscription in time") {
        Machine m(reinstallJob());
        m.feed(obs::ProviderReady{true, true});
        m.feed(installedAt(kSub | kInst, kOld));
        m.feed(obs::UnsubscribeResult{1});
        for (int t = 1000; t <= 60'000; t += 1000) {
            m.tickTo(t);
            if (m.emitted(K::QueryState))
                m.feed(installedAt(kSub | kInst, kOld));
        }
        CHECK(m.state() == S::Failed);
        CHECK(m.ctx.job.errorCode == "timeout");
        CHECK(m.ctx.job.errorMessage.find(note) != std::string::npos);
        REQUIRE(m.ctx.retryAtMs > 0);

        // The automatic retry subscribes again although Steam still lists
        // the subscription: it never takes that for "subscribed again".
        m.tickTo(m.ctx.retryAtMs - kT0);
        CHECK(m.emitted(K::QueryState));
        m.feed(installedAt(kSub | kInst, kOld));
        CHECK(m.emitted(K::Subscribe));
        CHECK(m.ctx.job.reinstallStep == ReinstallStep::Unsubscribed);
        m.feed(obs::SubscribeResult{1});
        CHECK(m.ctx.job.reinstallStep == ReinstallStep::Resubscribed);
    }
    SECTION("cancelled after unsubscribing") {
        Machine m(reinstallJob());
        m.feed(obs::ProviderReady{true, true});
        m.feed(installedAt(kSub | kInst, kOld));
        m.feed(obs::UnsubscribeResult{1});
        m.feed(obs::Cancel{});
        CHECK(m.state() == S::Cancelled);
        CHECK(m.ctx.job.errorMessage.find("was unsubscribed for the re-download") !=
              std::string::npos);
        m.feed(obs::Retry{});
        m.feed(installedAt(kInst, kOld));
        CHECK(m.emitted(K::Subscribe));
    }
    SECTION("the download fails after subscribing again: Retry does not unsubscribe") {
        Machine m(reinstallJob(ReinstallStep::Resubscribed));
        m.feed(obs::ProviderReady{true, true});
        m.feed(installedAt(kSub | kInst, kOld));
        CHECK(m.emitted(K::RequestDownload));
        m.feed(obs::DownloadAccepted{false});
        CHECK(m.state() == S::Failed);
        CHECK(m.ctx.job.errorMessage.find(note) == std::string::npos); // subscribed again
        m.feed(obs::Retry{});
        m.feed(installedAt(kSub | kInst, kOld));
        CHECK(m.emitted(K::RequestDownload));
    }
}

TEST_CASE("A re-download whose unsubscribe was not confirmed carries on from there",
          "[providers][acquisition]") {
    const std::string maybe =
        "Workshop item " + kId +
        " may be unsubscribed now: Retry subscribes to it again in that case and downloads it.";
    SECTION("the result times out; Steam did unsubscribe") {
        Machine m(reinstallJob());
        m.feed(obs::ProviderReady{true, true});
        m.feed(installedAt(kSub | kInst, kOld));
        m.tickTo(60'000);
        CHECK(m.state() == S::Failed);
        CHECK(m.ctx.job.reinstallStep == ReinstallStep::Unsubscribing);
        CHECK(m.ctx.job.errorMessage.find(maybe) != std::string::npos);
        m.tickTo(m.ctx.retryAtMs - kT0);
        CHECK(m.emitted(K::QueryState));
        m.feed(installedAt(kInst, kOld));
        CHECK(m.emitted(K::Subscribe)); // not "not subscribed in Steam"
        CHECK(m.ctx.job.reinstallStep == ReinstallStep::Unsubscribed);
    }
    SECTION("the result times out; Steam did not unsubscribe") {
        Machine m(reinstallJob());
        m.feed(obs::ProviderReady{true, true});
        m.feed(installedAt(kSub | kInst, kOld));
        m.tickTo(60'000);
        m.feed(obs::Retry{});
        m.feed(installedAt(kSub | kInst, kOld));
        CHECK(m.emitted(K::Unsubscribe)); // asked again
        // A refusal now may follow an earlier request that went through.
        m.feed(obs::UnsubscribeResult{9});
        CHECK(m.ctx.job.reinstallStep == ReinstallStep::Unsubscribing);
        CHECK(m.ctx.job.errorMessage.find(maybe) != std::string::npos);
    }
    SECTION("cancelled while unsubscribing, then retried") {
        Machine m(reinstallJob());
        m.feed(obs::ProviderReady{true, true});
        m.feed(installedAt(kSub | kInst, kOld));
        m.feed(obs::Cancel{});
        CHECK(m.state() == S::Cancelled);
        CHECK(m.ctx.job.errorMessage.find("Cancelled. An unsubscribe request was already sent") ==
              0);
        m.feed(obs::Retry{});
        m.feed(installedAt(kInst, kOld));
        CHECK(m.emitted(K::Subscribe));
    }
    SECTION("restored after a restart") {
        AcquisitionJob job = reinstallJob(ReinstallStep::Unsubscribing);
        job.state = S::Unsubscribing;
        Machine m(job);
        m.feed(obs::ProviderReady{true, true});
        m.feed(item(0));
        CHECK(m.emitted(K::Subscribe));
    }
}

TEST_CASE("A re-download that Steam does not carry out says that the files stayed",
          "[providers][acquisition]") {
    AcquisitionPolicy policy;
    policy.removalGraceMs = 2000;
    Machine m(reinstallJob(), policy);
    m.feed(obs::ProviderReady{true, true});
    m.feed(installedAt(kSub | kInst, kOld));
    m.feed(obs::UnsubscribeResult{1});
    m.feed(installedAt(kInst, kOld)); // dropped; the files stay
    m.tickTo(3000);
    m.feed(installedAt(kInst, kOld));
    CHECK(m.emitted(K::Subscribe));
    m.feed(obs::SubscribeResult{1});
    m.feed(installedAt(kSub | kInst, kOld));
    CHECK(m.emitted(K::RequestDownload));
    m.feed(obs::DownloadAccepted{true});
    // Steam calls the same files current: nothing was fetched.
    int requests = 1;
    for (int t = 4000; t <= 70'000 && m.state() != S::Failed; t += 1000) {
        m.tickTo(t);
        if (m.emitted(K::QueryState)) {
            m.feed(installedAt(kSub | kInst, kOld));
            CHECK_FALSE(m.emitted(K::VerifyContent));
        } else if (m.emitted(K::RequestDownload)) {
            ++requests;
            m.feed(obs::DownloadAccepted{true});
        }
    }
    CHECK(requests == 2);
    CHECK(m.state() == S::Failed);
    CHECK(m.ctx.job.errorCode == "update_not_seen");
    CHECK(m.ctx.job.errorMessage == kRedownloadNotSeenMessage);
    CHECK(m.ctx.job.reinstallStep == ReinstallStep::Resubscribed);
    CHECK_FALSE(m.last.retryAfterMs.has_value());

    // A changed install time counts as a fresh download.
    Machine fresh(reinstallJob(ReinstallStep::Resubscribed));
    fresh.feed(obs::ProviderReady{true, true});
    fresh.feed(installedAt(kSub | kInst, kOld));
    CHECK(fresh.emitted(K::RequestDownload));
    fresh.feed(obs::DownloadAccepted{true});
    fresh.feed(installedAt(kSub | kInst, kNew));
    CHECK(fresh.emitted(K::VerifyContent));
}

TEST_CASE("A download Steam ran by itself does not count as the update's request",
          "[providers][acquisition]") {
    SECTION("with a target: it ends short of it, and two requests follow") {
        Machine m(updateJob());
        m.feed(obs::ProviderReady{true, true});
        m.feed(item(kSub | kInst | kDl, kPath, 1, 4));
        CHECK(m.state() == S::Downloading);
        m.feed(installedAt(kSub | kInst, kOld)); // Steam's own download: the old version
        CHECK(m.quiet());
        m.tickTo(1000);
        CHECK(m.emitted(K::RequestDownload)); // the first request, at once
        m.feed(obs::DownloadAccepted{true});
        CHECK_FALSE(m.ctx.reRequested);
        int requests = 1;
        for (int t = 2000; t <= 70'000 && m.state() != S::Failed; t += 1000) {
            m.tickTo(t);
            if (m.emitted(K::QueryState)) {
                m.feed(installedAt(kSub | kInst, kOld));
            } else if (m.emitted(K::RequestDownload)) {
                ++requests;
                CHECK(t >= 31'000); // 30 s after the accepted first request
                m.feed(obs::DownloadAccepted{true});
            }
        }
        CHECK(requests == 2);
        CHECK(m.ctx.job.errorCode == "update_not_seen");
        CHECK(m.ctx.job.errorMessage == kUpdateNotSeenMessage);
    }
    SECTION("without a target: the download seen is the update") {
        Machine m(updateJob(0));
        m.feed(obs::ProviderReady{true, true});
        m.feed(item(kSub | kInst | kDl, kPath, 1, 4));
        m.feed(installedAt(kSub | kInst, kNew));
        CHECK(m.emitted(K::VerifyContent)); // no pointless request after Steam finished
        m.feed(obs::ContentVerified{true, {}});
        CHECK(m.state() == S::Installed);
    }
}

TEST_CASE("A refused unsubscribe changes nothing", "[providers][acquisition]") {
    Machine m(reinstallJob());
    m.feed(obs::ProviderReady{true, true});
    m.feed(installedAt(kSub | kInst, kOld));
    m.feed(obs::UnsubscribeResult{9});
    CHECK(m.state() == S::Unavailable);
    CHECK(m.ctx.job.reinstallStep == ReinstallStep::Start);
    CHECK(m.ctx.job.errorMessage.find("Unsubscribing from Workshop item " + kId) == 0);
    CHECK(m.ctx.job.errorMessage.find("now unsubscribed") == std::string::npos);

    Machine slow(reinstallJob());
    slow.feed(obs::ProviderReady{true, true});
    slow.feed(installedAt(kSub | kInst, kOld));
    slow.tickTo(60'000);
    CHECK(slow.state() == S::Failed);
    CHECK(slow.ctx.job.errorMessage.find("may be unsubscribed now") != std::string::npos);

    Machine offline(reinstallJob());
    offline.feed(obs::ProviderReady{false, true});
    offline.feed(installedAt(kSub | kInst, kOld));
    CHECK(offline.quiet());
    CHECK(offline.state() == S::NeedsUserAction);
}

TEST_CASE("A restored re-download carries on from its step", "[providers][acquisition]") {
    Machine unsubscribed(reinstallJob(ReinstallStep::Unsubscribed));
    unsubscribed.feed(obs::ProviderReady{true, true});
    unsubscribed.feed(item(0));
    CHECK(unsubscribed.emitted(K::Subscribe));

    Machine resubscribed(reinstallJob(ReinstallStep::Resubscribed));
    resubscribed.feed(obs::ProviderReady{true, true});
    resubscribed.feed(item(kSub | kDl, {}, 1, 2));
    CHECK(resubscribed.state() == S::Downloading);
    resubscribed.feed(installedAt(kSub | kInst, kNew));
    CHECK(resubscribed.emitted(K::VerifyContent));
}

TEST_CASE("The update transcript drives an update and a re-download to the end",
          "[providers][acquisition][fixture]") {
    // tests/fixtures/providers/helper_transcript_update.jsonl, event by event.
    std::ifstream in(fs::path(PMM_FIXTURES_DIR) / "providers/helper_transcript_update.jsonl",
                     std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    LineSplitter splitter;
    std::vector<HelperEvent> events;
    for (const std::string& line : splitter.push(text.str()))
        events.push_back(*decodeEvent(line));
    REQUIRE(events.size() == 15);

    AcquisitionJob update = updateJob(1'710'000'000);
    update.identity.id = "1200000024";
    AcquisitionPolicy policy;
    policy.removalGraceMs = 0;
    Machine u(update, policy);
    AcquisitionJob redo = reinstallJob();
    redo.identity.id = "1200000021";
    Machine r(redo, policy);
    for (const HelperEvent& event : events) {
        for (Machine* m : {&u, &r}) {
            if (auto o = observationFromHelperEvent(event, m->ctx.job.identity.id))
                m->feed(*o);
            if (m->emitted(K::VerifyContent))
                m->feed(obs::ContentVerified{true, {}});
        }
    }
    CHECK(u.state() == S::Installed);
    CHECK(r.state() == S::Installed);
    CHECK(r.ctx.job.reinstallStep == ReinstallStep::Resubscribed);
}

TEST_CASE("Helper events map to observations for the right item", "[providers][acquisition]") {
    ReadyEvent ready;
    ready.ok = true;
    ready.loggedOn = true;
    ready.ownsApp = false;
    auto o = observationFromHelperEvent(ready, kId);
    REQUIRE(o.has_value());
    REQUIRE(std::holds_alternative<obs::ProviderReady>(*o));
    CHECK(std::get<obs::ProviderReady>(*o).loggedOn);
    CHECK_FALSE(std::get<obs::ProviderReady>(*o).ownsApp);

    using R = obs::UnavailableReason;
    const std::pair<HelperFailure, R> failures[] = {
        {HelperFailure::SteamNotRunning, R::SteamNotRunning},
        {HelperFailure::NotLoggedOn, R::NotLoggedOn},
        {HelperFailure::InitFailed, R::InitFailed},
        {HelperFailure::LibraryLoadFailed, R::LibraryMissing},
        {HelperFailure::SymbolsMissing, R::Incompatible},
        {HelperFailure::InvalidArguments, R::HelperFailed},
    };
    for (const auto& [failure, reason] : failures) {
        ReadyEvent failed;
        failed.ok = false;
        failed.error = failure;
        failed.message = "detail";
        auto f = observationFromHelperEvent(failed, kId);
        REQUIRE(f.has_value());
        REQUIRE(std::holds_alternative<obs::ProviderUnavailable>(*f));
        CHECK(std::get<obs::ProviderUnavailable>(*f).reason == reason);
        CHECK(std::get<obs::ProviderUnavailable>(*f).detail == "detail");
    }

    ItemStateEvent mine;
    mine.status.id = kId;
    mine.status.flags = 5;
    ItemStateEvent other;
    other.status.id = "1";
    CHECK(observationFromHelperEvent(mine, kId).has_value());
    CHECK_FALSE(observationFromHelperEvent(other, kId).has_value());

    auto sub = observationFromHelperEvent(SubscribeResultEvent{kId, false, 9}, kId);
    REQUIRE(sub.has_value());
    CHECK(std::get<obs::SubscribeResult>(*sub).result == 9);
    CHECK_FALSE(observationFromHelperEvent(SubscribeResultEvent{"1", true, 1}, kId).has_value());

    auto dl = observationFromHelperEvent(DownloadRequestedEvent{kId, true}, kId);
    REQUIRE(dl.has_value());
    CHECK(std::get<obs::DownloadAccepted>(*dl).accepted);

    auto unsub = observationFromHelperEvent(UnsubscribeResultEvent{kId, false, 9}, kId);
    REQUIRE(unsub.has_value());
    CHECK(std::get<obs::UnsubscribeResult>(*unsub).result == 9);
    CHECK_FALSE(observationFromHelperEvent(UnsubscribeResultEvent{"1", true, 1}, kId).has_value());

    CHECK_FALSE(observationFromHelperEvent(PongEvent{}, kId).has_value());
    CHECK_FALSE(observationFromHelperEvent(ErrorEvent{"bad_request", "x", ""}, kId).has_value());
}

TEST_CASE("Actions map to helper commands only where the helper is involved",
          "[providers][acquisition]") {
    Action a;
    a.itemId = kId;
    a.kind = K::QueryState;
    CHECK(helperCommandFor(a) == std::optional<HelperCommand>(StateCommand{{kId}}));
    a.kind = K::Subscribe;
    CHECK(helperCommandFor(a) == std::optional<HelperCommand>(SubscribeCommand{kId}));
    a.kind = K::RequestDownload;
    a.highPriority = false;
    CHECK(helperCommandFor(a) == std::optional<HelperCommand>(DownloadCommand{kId, false}));
    a.kind = K::Unsubscribe;
    CHECK(helperCommandFor(a) == std::optional<HelperCommand>(UnsubscribeCommand{kId}));
    for (K kind : {K::None, K::QuerySession, K::VerifyContent, K::OpenFallbackPage}) {
        a.kind = kind;
        CHECK_FALSE(helperCommandFor(a).has_value());
    }
}
