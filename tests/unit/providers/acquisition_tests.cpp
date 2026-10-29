#include "providers/acquisition.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::providers;
namespace fs = std::filesystem;
using S = AcquisitionState;
using K = ActionKind;

namespace {

const std::string kId = "1121692237";
const std::string kPath = "D:/Steam Library/\xC3\x9C"
                          "n\xC3\xAF"
                          "c\xC3\xB8"
                          "d\xC3\xA9/steamapps/workshop/content/281990/1121692237";
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

// Feeds observations one at a time, like the application driver.
struct Machine {
    AcquisitionPolicy policy;
    AcquisitionContext ctx;
    Transition last;

    explicit Machine(AcquisitionJob job = makeJob(), AcquisitionPolicy p = {}) : policy(p) {
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
    const S nonTerminal[] = {S::Pending, S::Subscribing, S::Subscribed, S::DownloadRequested,
                             S::Downloading};
    const S terminal[] = {S::Installed, S::Failed, S::Unavailable, S::NeedsUserAction,
                          S::Cancelled};
    const AwaitStep busy[] = {AwaitStep::Session,         AwaitStep::State,
                              AwaitStep::SubscribeResult, AwaitStep::DownloadAccepted,
                              AwaitStep::Progress,        AwaitStep::Verification};
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
        obs::SubscribeResult{1},
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
    for (S s : nonTerminal) {
        for (AwaitStep step : busy) {
            for (bool session : {false, true}) {
                AcquisitionContext c;
                c.job = makeJob(s);
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
                contexts.push_back(c);
            }
        }
    }
    for (S s : terminal) {
        AcquisitionContext c;
        c.job = makeJob(s);
        c.job.attempts = 3;
        c.clockMs = kT0;
        contexts.push_back(c);
    }

    std::size_t checked = 0;
    for (const auto& c : contexts) {
        for (std::size_t i = 0; i < observations.size(); ++i) {
            INFO("state " << toString(c.job.state) << ", awaiting " << toString(c.awaiting)
                          << ", session " << c.sessionKnown << ", observation #" << i);
            const Transition t = reduceAcquisition(c, observations[i]);
            const S next = t.context.job.state;

            CHECK(t.actions.size() <= 1);
            CHECK(isTerminal(next) == (t.context.awaiting == AwaitStep::Nothing));

            const bool verifiedOk = std::holds_alternative<obs::ContentVerified>(observations[i]) &&
                                    std::get<obs::ContentVerified>(observations[i]).ok &&
                                    c.awaiting == AwaitStep::Verification;
            if (next == S::Installed)
                CHECK((verifiedOk || c.job.state == S::Installed));

            for (const Action& a : t.actions) {
                CHECK(a.itemId == kId);
                CHECK(a.kind != K::None);
                if (a.kind == K::Subscribe || a.kind == K::RequestDownload ||
                    a.kind == K::VerifyContent) {
                    // Decided only from a fresh state report.
                    CHECK(std::holds_alternative<obs::ItemState>(observations[i]));
                }
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
    for (K kind : {K::None, K::QuerySession, K::VerifyContent, K::OpenFallbackPage}) {
        a.kind = kind;
        CHECK_FALSE(helperCommandFor(a).has_value());
    }
}

TEST_CASE("Workshop content check requires a usable descriptor", "[providers][acquisition]") {
    pmm::test::TempDir dir("verify");
    const fs::path base =
        dir.path() / pmm::test::u8path("Ünïcødé dir") / pmm::test::u8path("日本語");

    const fs::path good = base / "703156866";
    pmm::test::writeFile(good / "descriptor.mod", "name=\"Cinematic Camera\"\n");
    CHECK(verifyWorkshopContent(good).ok());

    const fs::path noDescriptor = base / "2";
    pmm::test::writeFile(noDescriptor / "common" / "x.txt", "x");
    auto missing = verifyWorkshopContent(noDescriptor);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::ValidationFailed);
    CHECK(missing.error().message.find("descriptor.mod") != std::string::npos);

    const fs::path emptyDescriptor = base / "3";
    pmm::test::writeFile(emptyDescriptor / "descriptor.mod", "");
    CHECK_FALSE(verifyWorkshopContent(emptyDescriptor).ok());

    const fs::path descriptorDir = base / "4";
    fs::create_directories(descriptorDir / "descriptor.mod");
    CHECK_FALSE(verifyWorkshopContent(descriptorDir).ok());

    auto gone = verifyWorkshopContent(base / "5");
    REQUIRE_FALSE(gone.ok());
    CHECK(gone.error().code == ErrorCode::NotFound);

    pmm::test::writeFile(base / "legacy_item.zip", "PK\x03\x04");
    CHECK(verifyWorkshopContent(base / "legacy_item.zip").ok());
    pmm::test::writeFile(base / "empty_legacy.zip", "");
    CHECK_FALSE(verifyWorkshopContent(base / "empty_legacy.zip").ok());
}
