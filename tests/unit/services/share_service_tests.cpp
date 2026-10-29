#include "persistence/acquisition_job_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/settings_repository.hpp"
#include "providers/local_fingerprint.hpp"
#include "services/playset_service.hpp"
#include "services/playset_view.hpp"
#include "services/share_service.hpp"
#include "sharing/share_code.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>
#include <sstream>

using namespace pmm;
using namespace pmm::services;
using namespace pmm::services::test;
using Catch::Matchers::ContainsSubstring;

namespace {

std::int64_t makePlayset(World& w, std::vector<PlaysetEntry> entries, const std::string& name,
                         const GameKey& game = kGame) {
    persistence::PlaysetRepository repo(*w.db);
    Playset p;
    p.game = game;
    p.name = name;
    p.entries = std::move(entries);
    return require(repo.create(p));
}

// Everything an import may touch, as text, for before/after comparisons.
std::string dumpState(World& w) {
    std::ostringstream out;
    persistence::PlaysetRepository playsets(*w.db);
    persistence::AcquisitionJobRepository jobs(*w.db);
    persistence::SettingsRepository settings(*w.db);
    for (const GameKey game : {"stellaris", "eu4"}) {
        for (const PlaysetSummary& summary : require(playsets.listForGame(game))) {
            const Playset p = require(playsets.get(summary.id));
            out << "playset " << p.id << " " << p.name << " updated=" << p.updatedAt << "\n";
            for (const PlaysetEntry& e : p.entries)
                out << "  " << toString(e.identity) << " " << e.enabled << " " << e.nameHint
                    << "\n";
        }
        out << "history " << require(playsets.historyForGame(game)).size() << "\n";
        for (const AcquisitionJob& job : require(jobs.listForGame(game)))
            out << "job " << job.id << " " << toString(job.identity) << " " << toString(job.state)
                << "\n";
        out << "active " << require(settings.get("game." + game + ".activePlaysetId")).value_or("-")
            << "\n";
    }
    out << "last " << require(settings.get("app.lastGameKey")).value_or("-") << "\n";
    return out.str();
}

struct Identity {
    std::string source;
    std::string id;
    bool enabled;
    bool operator==(const Identity&) const = default;
};

std::vector<Identity> shape(const std::vector<PlaysetEntry>& entries) {
    std::vector<Identity> out;
    for (const PlaysetEntry& e : entries)
        out.push_back({e.identity.source, e.identity.id, e.enabled});
    return out;
}

// The sender's setup: deliberately non-default order, a disabled entry, a local mod.
std::vector<PlaysetEntry> senderEntries() {
    return {workshopEntry("2000000002", "Patch"), workshopEntry("2000000003", "Old Era", false),
            workshopEntry("2000000001", "UI Overhaul"),
            localEntry("example_local_tweaks", "Tweaks")};
}

std::string senderCode() {
    World sender;
    sender.installFixtureMods();
    sender.refresh();
    const std::int64_t id = makePlayset(sender, senderEntries(), "Friday night");
    ShareService share(sender.ctx);
    return require(share.exportCode(id));
}

// Rewrites a file in place: no new directory entry, unlike writeFileAtomic().
void rewriteInPlace(const fs::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out);
    out << text;
    out.close();
    REQUIRE(out);
}

void injectJobInsertFailure(World& w) {
    auto access = w.db->access();
    requireOk(access.connection().exec(
        "CREATE TEMP TRIGGER fail_job_insert BEFORE INSERT ON acquisition_jobs "
        "BEGIN SELECT RAISE(ABORT, 'injected failure'); END;"));
}

void removeJobInsertFailure(World& w) {
    auto access = w.db->access();
    requireOk(access.connection().exec("DROP TRIGGER fail_job_insert;"));
}

} // namespace

TEST_CASE("Export carries every entry in order with installed hints and local fingerprints",
          "[services][share]") {
    World w;
    w.installFixtureMods();
    w.addWorkshopMod("2000000555", std::string(300, 'n'));
    w.refresh();
    PlaysetEntry foreign = workshopEntry("2000000777", "Their mod");
    foreign.versionHint = "2.0";
    const std::int64_t id = makePlayset(w,
                                        {workshopEntry("2000000001", "stale hint"),
                                         localEntry("example_local_tweaks", "", false), foreign,
                                         workshopEntry("2000000555", "")},
                                        "Export me");
    ShareService share(w.ctx);

    const std::string code = require(share.exportCode(id));
    CHECK(code.rfind("PMM1.", 0) == 0);
    CHECK(w.fingerprintCalls == 1);
    const sharing::Manifest manifest = require(sharing::decodeShareCode(code));
    CHECK(manifest.game == kGame);
    CHECK(manifest.playsetName == "Export me");
    CHECK(manifest.gameVersionHint == "v4.5.2");
    REQUIRE(manifest.mods.size() == 4);
    CHECK(manifest.mods[0].id == "2000000001");
    CHECK(manifest.mods[0].nameHint == "UI Overhaul Example");
    CHECK(manifest.mods[0].versionHint == "4.5.*");
    CHECK(manifest.mods[0].enabled);
    CHECK(manifest.mods[1].source == source::Local);
    CHECK_FALSE(manifest.mods[1].enabled);
    const std::string expectedFingerprint =
        require(providers::fingerprintDirectory(w.userData / "mod" / "example_local_tweaks"));
    CHECK(manifest.mods[1].fingerprint == expectedFingerprint);
    CHECK(manifest.mods[2].nameHint == "Their mod");
    CHECK(manifest.mods[2].versionHint == "2.0");
    CHECK(manifest.mods[3].nameHint.size() == sharing::kMaxNameHintBytes);

    // The fingerprint is cached in the inventory for readiness comparisons.
    // Exporting again recomputes it (a cached value cannot be trusted) and is
    // deterministic.
    persistence::InventoryRepository inventory(*w.db);
    const auto cached = require(inventory.find({kGame, source::Local, "example_local_tweaks"}));
    REQUIRE(cached);
    CHECK(cached->fingerprint == expectedFingerprint);
    CHECK(require(share.exportCode(id)) == code);
    CHECK(w.fingerprintCalls == 2);
}

TEST_CASE("Exported local fingerprints follow edits that leave the mod folder's time unchanged",
          "[services][share]") {
    World w;
    w.installFixtureMods();
    w.refresh();
    const fs::path modDir = w.userData / "mod" / "example_local_tweaks";
    const std::int64_t id =
        makePlayset(w, {localEntry("example_local_tweaks", "Tweaks")}, "Local only");
    ShareService share(w.ctx);
    GameService games(w.ctx);
    const auto exported = [&] {
        const sharing::Manifest manifest =
            require(sharing::decodeShareCode(require(share.exportCode(id))));
        REQUIRE(manifest.mods.size() == 1);
        return manifest.mods[0].fingerprint;
    };
    const auto cached = [&] {
        persistence::InventoryRepository inventory(*w.db);
        const auto mod = require(inventory.find({kGame, source::Local, "example_local_tweaks"}));
        REQUIRE(mod);
        return mod->fingerprint;
    };
    const std::string original = exported();
    CHECK(original == require(providers::fingerprintDirectory(modDir)));

    // Editing files inside a folder does not change the folder's own time on
    // NTFS, ext4 or APFS; restoring it makes that deterministic here.
    std::error_code ec;
    const fs::file_time_type folderTime = fs::last_write_time(modDir, ec);
    REQUIRE_FALSE(ec);
    const std::int64_t folderSeconds = platform::lastWriteUnixSeconds(modDir);
    const auto keepFolderTime = [&] {
        fs::last_write_time(modDir, folderTime, ec);
        REQUIRE_FALSE(ec);
        REQUIRE(platform::lastWriteUnixSeconds(modDir) == folderSeconds);
    };

    SECTION("a nested file edited in place and a file added to a subfolder") {
        rewriteInPlace(modDir / "common" / "example_tweaks.txt", "example_value = 43\n");
        writeText(modDir / "common" / "added.txt", "added = yes\n");
        keepFolderTime();
        require(games.refreshGame(kGame));
    }
    SECTION("a file at the folder root edited in place") {
        rewriteInPlace(modDir / "descriptor.mod",
                       readText(modDir / "descriptor.mod") + "picture=\"thumbnail.png\"\n");
        keepFolderTime();
        require(games.rescanMods(kGame));
    }
    const std::string current = require(providers::fingerprintDirectory(modDir));
    REQUIRE(current != original);
    CHECK(exported() == current);
    CHECK(cached() == current);
}

TEST_CASE("Previewing an import is read-only", "[services][share]") {
    const std::string code = senderCode();
    World w;
    w.addWorkshopMod("2000000001", "UI Overhaul Example");
    w.refresh();
    makePlayset(w, {workshopEntry("2000000050", "Mine")}, "Friday night");
    const std::string before = dumpState(w);
    ShareService share(w.ctx);

    const ImportPreview preview = share.previewImport("  \n" + code + "\n ");
    REQUIRE(preview.ok());
    CHECK(preview.gameInstalled);
    CHECK(preview.gameName == "Stellaris");
    REQUIRE(preview.plan);
    const ImportCounts& counts = preview.plan->counts;
    CHECK(counts.total == 4);
    CHECK(counts.enabled == 3);
    CHECK(counts.installed == 1);
    CHECK(counts.missingEnabled == 1);
    CHECK(counts.missingDisabled == 1);
    CHECK(counts.localUnmapped == 1);
    CHECK(preview.plan->proposedName == "Friday night (2)");
    CHECK(preview.plan->warnings.back() == kIdenticalContentNotGuaranteed);
    CHECK(dumpState(w) == before);

    const ImportPreview bad = share.previewImport(code.substr(0, code.size() - 3));
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error->code == ErrorCode::IntegrityCheckFailed); // truncated digest
    CHECK_FALSE(bad.plan);
    const ImportPreview garbage = share.previewImport("hello");
    REQUIRE_FALSE(garbage.ok());
    CHECK_FALSE(share.applyImport(garbage, ImportMode::New));
    CHECK(dumpState(w) == before);
}

TEST_CASE("Importing as a new playset reproduces the exact setup and queues missing mods",
          "[services][share]") {
    const std::string code = senderCode();
    World w;
    w.addWorkshopMod("2000000001", "UI Overhaul Example");
    w.addWorkshopMod("2000000050", "Extra Installed Mod");
    w.refresh();
    const std::int64_t existing = makePlayset(w, {workshopEntry("2000000050", "Mine")}, "Mine");
    const std::string existingBefore = [&] {
        persistence::PlaysetRepository repo(*w.db);
        std::ostringstream out;
        for (const PlaysetEntry& e : require(repo.get(existing)).entries)
            out << toString(e.identity) << e.enabled;
        return out.str();
    }();
    ShareService share(w.ctx);

    const ImportPreview preview = share.previewImport(code);
    REQUIRE(preview.ok());
    const AppliedImport applied = require(share.applyImport(preview, ImportMode::New));
    CHECK(applied.playsetName == "Friday night");

    PlaysetService playsets(w.ctx);
    const Playset imported = require(playsets.playset(applied.playsetId));
    CHECK(shape(imported.entries) == shape(senderEntries()));
    CHECK(imported.entries[0].nameHint == "Example Compatibility Patch");
    for (const PlaysetEntry& e : imported.entries)
        CHECK(e.identity.id != "2000000050"); // extra installed mods stay out
    CHECK(require(playsets.activePlaysetId(kGame)) == applied.playsetId);

    // Jobs for every missing Workshop entry, the disabled one included; none for
    // installed content or local mods.
    REQUIRE(applied.jobs.size() == 2);
    CHECK(applied.jobs[0].identity.id == "2000000002");
    CHECK(applied.jobs[1].identity.id == "2000000003");
    for (const AcquisitionJob& job : applied.jobs) {
        CHECK(job.state == AcquisitionState::Pending);
        CHECK(job.playsetId == std::optional<std::int64_t>(applied.playsetId));
    }
    persistence::AcquisitionJobRepository jobs(*w.db);
    CHECK(require(jobs.listUnfinished()).size() == 2);

    // The other playset is untouched.
    persistence::PlaysetRepository repo(*w.db);
    std::ostringstream after;
    for (const PlaysetEntry& e : require(repo.get(existing)).entries)
        after << toString(e.identity) << e.enabled;
    CHECK(after.str() == existingBefore);

    // Nothing claims readiness that does not exist.
    const PlaysetView view = require(loadPlaysetView(w.ctx, applied.playsetId));
    CHECK(view.rows[0].readiness.status == EntryStatus::Pending);
    CHECK(view.rows[1].readiness.status == EntryStatus::Pending);
    CHECK(view.rows[2].readiness.status == EntryStatus::Ready);
    CHECK(view.rows[3].readiness.status == EntryStatus::NeedsUserAction);
    CHECK_FALSE(view.launch.canLaunch);

    // Importing the same code again reuses the unfinished jobs.
    const AppliedImport again =
        require(share.applyImport(share.previewImport(code), ImportMode::New));
    CHECK(again.playsetName == "Friday night (2)");
    REQUIRE(again.jobs.size() == 2);
    CHECK(again.jobs[0].id == applied.jobs[0].id);
    CHECK(require(jobs.listUnfinished()).size() == 2);
}

TEST_CASE("Replacing a playset is explicit, exact and recoverable", "[services][share]") {
    const std::string code = senderCode();
    World w;
    w.refresh();
    const std::vector<PlaysetEntry> old = {workshopEntry("2000000050", "Old A"),
                                           workshopEntry("2000000060", "Old B", false)};
    const std::int64_t target = makePlayset(w, old, "Target");
    const std::int64_t eu4 = makePlayset(w, {}, "EU4 set", "eu4");
    ShareService share(w.ctx);
    const ImportPreview preview = share.previewImport(code);
    REQUIRE(preview.ok());
    const std::string before = dumpState(w);

    const auto noTarget = share.applyImport(preview, ImportMode::Replace);
    REQUIRE_FALSE(noTarget);
    CHECK(noTarget.error().code == ErrorCode::InvalidArgument);
    const auto wrongGame = share.applyImport(preview, ImportMode::Replace, eu4);
    REQUIRE_FALSE(wrongGame);
    CHECK(wrongGame.error().code == ErrorCode::WrongGame);
    CHECK_FALSE(share.applyImport(preview, ImportMode::Replace, 987654));
    CHECK(dumpState(w) == before);

    const AppliedImport applied = require(share.applyImport(preview, ImportMode::Replace, target));
    CHECK(applied.playsetId == target);
    CHECK(applied.playsetName == "Target");
    REQUIRE(applied.replacedHistoryId);
    PlaysetService playsets(w.ctx);
    CHECK(shape(require(playsets.playset(target)).entries) == shape(senderEntries()));
    CHECK(applied.jobs.size() == 3); // nothing installed here

    // The previous state is one restore away.
    CHECK(require(playsets.restoreHistory(*applied.replacedHistoryId)) == target);
    CHECK(shape(require(playsets.playset(target)).entries) == shape(old));
}

TEST_CASE("A failed import leaves existing playsets and the database untouched",
          "[services][share]") {
    const std::string code = senderCode();
    World w;
    w.refresh();
    const std::int64_t target = makePlayset(w, {workshopEntry("2000000050", "Old A")}, "Target");
    PlaysetService playsets(w.ctx);
    requireOk(playsets.setActivePlayset(kGame, target));
    ShareService share(w.ctx);
    const ImportPreview preview = share.previewImport(code);
    REQUIRE(preview.ok());
    const std::string before = dumpState(w);

    injectJobInsertFailure(w);
    const auto asNew = share.applyImport(preview, ImportMode::New);
    REQUIRE_FALSE(asNew);
    CHECK_THAT(asNew.error().message, ContainsSubstring("nothing was changed"));
    CHECK(dumpState(w) == before);
    const auto asReplace = share.applyImport(preview, ImportMode::Replace, target);
    REQUIRE_FALSE(asReplace);
    CHECK(dumpState(w) == before);
    CHECK(require(playsets.activePlaysetId(kGame)) == target);

    // Nothing reports readiness for the never-saved import, and the target still launches as
    // before.
    const PlaysetView view = require(loadPlaysetView(w.ctx, target));
    REQUIRE(view.rows.size() == 1);
    CHECK(view.rows[0].entry.identity.id == "2000000050");

    removeJobInsertFailure(w);
    CHECK(share.applyImport(preview, ImportMode::New).ok());
}
