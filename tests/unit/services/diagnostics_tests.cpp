#include "persistence/acquisition_job_repository.hpp"
#include "persistence/launch_history_repository.hpp"
#include "services/diagnostics.hpp"
#include "services/playset_service.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

using namespace pmm;
using namespace pmm::services;
using namespace pmm::services::test;
using Catch::Matchers::ContainsSubstring;

namespace {

constexpr const char* kUser = "PlaceholderUser";

} // namespace

TEST_CASE("Diagnostics summarise installations, jobs and launches with personal data redacted",
          "[services][diagnostics]") {
    World w;
    w.installFixtureMods();
    w.refresh();

    PlaysetService playsets(w.ctx);
    const std::int64_t id = require(playsets.create(kGame, "Main"));
    const ModIdentity overhaul{kGame, source::SteamWorkshop, "2000000001"};
    const ModIdentity addList[] = {overhaul};
    requireOk(playsets.addMods(id, addList));
    requireOk(playsets.setActivePlayset(kGame, id));

    persistence::AcquisitionJobRepository jobs(*w.db);
    AcquisitionJob job =
        require(jobs.createOrReuse({kGame, source::SteamWorkshop, "2000000777"}, id));
    job.state = AcquisitionState::Failed;
    job.attempts = 3;
    job.errorCode = "offline";
    job.errorMessage = "Steam is offline (cache in " + platform::pathToUtf8(w.root) + ").";
    requireOk(jobs.update(job));

    persistence::LaunchHistoryRepository history(*w.db);
    persistence::LaunchRecord launch;
    launch.game = kGame;
    launch.playsetId = id;
    launch.pid = 4242;
    launch.outcome = "started";
    launch.detail =
        platform::pathToUtf8(w.executable) + " started by " + kUser + " token=abc123secret";
    launch.transactionId = "20261029T101112123Z-0001";
    require(history.record(launch));

    // An interrupted configuration change is listed too.
    platform::FileTransactionFaults crash;
    crash.crashAfterWrites = std::size_t{1};
    w.transactions.setFaultsForTesting(crash);
    CHECK_FALSE(w.transactions.apply("launch stellaris playset 1 (Main)",
                                     {{w.userData / "dlc_load.json", "{}"}}));

    AcquisitionSnapshot snapshot;
    snapshot.game = kGame;
    snapshot.providerStatus = ProviderStatus::SteamNotRunning;
    snapshot.providerMessage = "Steam is not running. Start Steam, sign in, and retry.";
    snapshot.summary = "1 failed";

    DiagnosticsOptions options;
    options.appVersion = "1.2.3";
    options.acquisition = {snapshot};
    options.redaction = RedactionContext{platform::pathToUtf8(w.root), kUser};
    const std::string text = diagnosticsText(w.ctx, options);

    CHECK_THAT(text, ContainsSubstring("Paradox Mod Manager diagnostics"));
    CHECK_THAT(text, ContainsSubstring("App version: 1.2.3"));
    CHECK_THAT(text, ContainsSubstring("Share-code protocol: 1"));
    CHECK_THAT(text, ContainsSubstring("OS: windows"));
    CHECK_THAT(text, ContainsSubstring("Interrupted configuration transactions: 1"));
    CHECK_THAT(text, ContainsSubstring("== Stellaris (stellaris) =="));
    CHECK_THAT(text, ContainsSubstring("Installation: steam, "));
    CHECK_THAT(text, ContainsSubstring("Fake v4.5.2 (1234)"));
    CHECK_THAT(text, ContainsSubstring("Steam app id: 281990"));
    CHECK_THAT(text, ContainsSubstring("Inventory: 4 mods (3 Workshop, 1 local, 0 broken"));
    CHECK_THAT(text, ContainsSubstring("Playsets: 1 (active: #" + std::to_string(id)));
    CHECK_THAT(text, ContainsSubstring("Workshop provider: steam_not_running"));
    CHECK_THAT(text,
               ContainsSubstring("steam-workshop:2000000777 failed attempts=3 error=offline"));
    CHECK_THAT(text, ContainsSubstring("Recent launches:"));
    CHECK_THAT(text, ContainsSubstring("started pid=4242"));
    CHECK_THAT(text, ContainsSubstring("== Europa Universalis IV (eu4) =="));
    CHECK_THAT(text, ContainsSubstring("Installation: not found"));

    // Personal paths, the user name and credentials never leave the machine.
    CHECK_THAT(text, !ContainsSubstring(platform::pathToUtf8(w.root)));
    CHECK_THAT(text, ContainsSubstring("~"));
    CHECK_THAT(text, !ContainsSubstring(kUser));
    CHECK_THAT(text, ContainsSubstring("<user>"));
    CHECK_THAT(text, !ContainsSubstring("abc123secret"));
    CHECK_THAT(text, ContainsSubstring("token=<redacted>"));
}

TEST_CASE("Diagnostics work on an empty profile", "[services][diagnostics]") {
    World w;
    w.ctx.adapters.clear();
    DiagnosticsOptions options;
    options.redaction = RedactionContext{platform::pathToUtf8(w.root), kUser};
    const std::string text = diagnosticsText(w.ctx, options);
    CHECK_THAT(text, ContainsSubstring("App version: unknown"));
    CHECK_THAT(text, ContainsSubstring("Database: schema"));
    CHECK_THAT(text, ContainsSubstring("in memory"));
    CHECK_THAT(text, ContainsSubstring("Interrupted configuration transactions: 0"));
    CHECK_THAT(text, ContainsSubstring("Adapter: not loaded"));
    CHECK_THAT(text, ContainsSubstring("Adapter: not supported yet"));
    CHECK_THAT(text, !ContainsSubstring("Recent launches"));
}
