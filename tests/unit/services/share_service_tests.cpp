#include "persistence/acquisition_job_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/settings_repository.hpp"
#include "providers/local_fingerprint.hpp"
#include "services/playset_service.hpp"
#include "services/playset_view.hpp"
#include "services/share_service.hpp"
#include "sharing/share_code.hpp"
#include "sharing/utf8.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <fstream>
#include <sstream>

using namespace pmm;
using namespace pmm::services;
using namespace pmm::services::test;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::EndsWith;

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
    // NTFS; restoring it makes that deterministic here.
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

TEST_CASE("A code whose playset name is at the length limit imports again under a unique name",
          "[services][share]") {
    constexpr std::size_t kMax = sharing::kMaxPlaysetNameBytes;
    std::string name;
    SECTION("ASCII") {
        name = std::string(kMax, 'n');
    }
    SECTION("multi-byte, where a plain byte cut would split a character") {
        name = "a";
        while (name.size() + 3 <= kMax)
            name += "\xE6\x97\xA5"; // U+65E5
    }
    REQUIRE(name.size() == kMax);
    sharing::Manifest manifest;
    manifest.game = kGame;
    manifest.playsetName = name;
    manifest.mods.push_back({source::SteamWorkshop, "2000000001", "UI", true, {}, {}});
    const std::string code = require(sharing::encodeShareCode(manifest));

    World w;
    w.refresh();
    ShareService share(w.ctx);
    CHECK(require(share.applyImport(share.previewImport(code), ImportMode::New)).playsetName ==
          name);

    const ImportPreview again = share.previewImport(code);
    REQUIRE(again.ok());
    const std::string proposed = again.plan->proposedName;
    CHECK(proposed.size() <= kMax);
    CHECK(sharing::isValidUtf8(proposed));
    CHECK_THAT(proposed, EndsWith(" (2)"));
    CHECK(require(share.applyImport(again, ImportMode::New)).playsetName == proposed);

    // A typed name that collides is shortened the same way; a typed name that
    // is too long is refused rather than cut.
    const std::string typed =
        require(share.applyImport(share.previewImport(code), ImportMode::New, std::nullopt, name))
            .playsetName;
    CHECK(typed.size() <= kMax);
    CHECK(sharing::isValidUtf8(typed));
    CHECK_THAT(typed, EndsWith(" (3)"));
    const auto tooLong =
        share.applyImport(share.previewImport(code), ImportMode::New, std::nullopt, name + "x");
    REQUIRE_FALSE(tooLong);
    CHECK(tooLong.error().code == ErrorCode::InvalidArgument);
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

TEST_CASE("An import that queues no acquisition creates and reuses no job", "[services][share]") {
    const std::string code = senderCode();
    // Two identical computers; only the first import queues downloads. On both,
    // 2000000002 is already downloading and 2000000003 has no job.
    World queued;
    World notQueued;
    std::int64_t target = 0;
    AcquisitionJob downloading;
    for (World* w : {&queued, &notQueued}) {
        w->addWorkshopMod("2000000001", "UI Overhaul Example");
        w->refresh();
        target = makePlayset(*w, {workshopEntry("2000000050", "Old A")}, "Target");
        persistence::AcquisitionJobRepository jobs(*w->db);
        downloading =
            require(jobs.createOrReuse({kGame, source::SteamWorkshop, "2000000002"}, target));
        downloading.state = AcquisitionState::Downloading;
        downloading.attempts = 1;
        downloading.bytesDownloaded = 1000;
        downloading.bytesTotal = 4000;
        requireOk(jobs.update(downloading));
        downloading = require(jobs.get(downloading.id));
    }
    const auto apply = [&](World& w, ImportMode mode, bool queue) {
        ShareService share(w.ctx);
        ImportOptions options;
        options.queueAcquisition = queue;
        const std::optional<std::int64_t> replaced =
            mode == ImportMode::Replace ? std::optional<std::int64_t>(target) : std::nullopt;
        return require(share.applyImport(share.previewImport(code), mode, replaced, {}, options));
    };
    // Everything an import writes except its jobs.
    const auto exceptJobs = [](const std::string& state) {
        std::istringstream in(state);
        std::string out;
        for (std::string line; std::getline(in, line);) {
            if (line.rfind("job ", 0) != 0)
                out += line + "\n";
        }
        return out;
    };

    for (const ImportMode mode : {ImportMode::New, ImportMode::Replace}) {
        INFO((mode == ImportMode::New ? "as a new playset" : "replacing a playset"));
        const AppliedImport withJobs = apply(queued, mode, true);
        // A job insert would fail the whole import.
        injectJobInsertFailure(notQueued);
        const AppliedImport applied = apply(notQueued, mode, false);
        removeJobInsertFailure(notQueued);

        // Queued as before: the running download is reused, 2000000003 gets one.
        REQUIRE(withJobs.jobs.size() == 2);
        CHECK(withJobs.jobs[0].id == downloading.id);
        CHECK(withJobs.jobs[1].identity.id == "2000000003");

        // Not queued: no job, and the running download is untouched.
        CHECK(applied.jobs.empty());
        persistence::AcquisitionJobRepository jobs(*notQueued.db);
        const std::vector<AcquisitionJob> stored = require(jobs.listForGame(kGame));
        REQUIRE(stored.size() == 1);
        CHECK(stored[0].id == downloading.id);
        CHECK(stored[0].state == AcquisitionState::Downloading);
        CHECK(stored[0].playsetId == std::optional<std::int64_t>(target));
        CHECK(stored[0].bytesDownloaded == 1000);
        CHECK(stored[0].updatedAt == downloading.updatedAt);

        // Everything else is identical: playsets, entries, history, selection.
        CHECK(applied.playsetId == withJobs.playsetId);
        CHECK(applied.playsetName == withJobs.playsetName);
        CHECK(applied.replacedHistoryId == withJobs.replacedHistoryId);
        CHECK(applied.plan.toAcquire == withJobs.plan.toAcquire);
        CHECK(exceptJobs(dumpState(notQueued)) == exceptJobs(dumpState(queued)));
        if (mode == ImportMode::Replace)
            CHECK(applied.replacedHistoryId.has_value());

        // The entry without a job reads "Not installed", not "cancelled".
        const PlaysetView view = require(loadPlaysetView(notQueued.ctx, applied.playsetId));
        REQUIRE(view.rows.size() == 4);
        CHECK(view.rows[0].readiness.status == EntryStatus::Downloading);
        CHECK(view.rows[1].entry.identity.id == "2000000003");
        CHECK(view.rows[1].readiness.status == EntryStatus::Missing);
        CHECK(view.rows[1].readiness.detail == "Not installed. Download it from Steam Workshop.");
    }
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

// --- Workshop ID lists and Workshop names in the preview ---------------------

namespace {

providers::WorkshopItemMetadata steamItem(const std::string& id, const std::string& title,
                                          const std::string& appId = kAppId) {
    providers::WorkshopItemMetadata item;
    item.id = id;
    item.found = true;
    item.resultCode = 1;
    item.title = title;
    item.consumerAppId = appId;
    return item;
}

providers::WorkshopItemMetadata unansweredItem(const std::string& id, int resultCode) {
    providers::WorkshopItemMetadata item;
    item.id = id;
    item.resultCode = resultCode;
    return item;
}

std::vector<std::string> previewIds(const ImportPreview& preview) {
    std::vector<std::string> out;
    for (const ImportPlanEntry& entry : preview.plan->entries)
        out.push_back(entry.entry.identity.id);
    return out;
}

// The warnings withWorkshopMetadata() put before the standing note.
std::vector<std::string> addedWarnings(const ImportPreview& preview) {
    const std::vector<std::string>& all = preview.plan->warnings;
    REQUIRE_FALSE(all.empty());
    CHECK(all.back() == kIdenticalContentNotGuaranteed);
    REQUIRE(preview.workshopWarnings < all.size());
    return {all.end() - 1 - static_cast<std::ptrdiff_t>(preview.workshopWarnings), all.end() - 1};
}

} // namespace

TEST_CASE("A Workshop ID list is previewed and imported for the selected game",
          "[services][share][idlist]") {
    World w;
    w.addWorkshopMod("2000000001", "UI Overhaul Example");
    w.refresh();
    const std::string before = dumpState(w);
    ShareService share(w.ctx);

    const ImportPreview preview =
        share.previewImport(" 2000000003|2000000001\r\n2000000002\n", kGame);
    REQUIRE(preview.ok());
    CHECK(preview.format == sharing::ImportTextFormat::WorkshopIdList);
    CHECK(preview.gameName == "Stellaris");
    CHECK(preview.gameInstalled);
    CHECK(preview.steamAppId == kAppId);
    REQUIRE(preview.manifest);
    CHECK(preview.manifest->game == kGame);
    CHECK(preview.manifest->playsetName == "Imported mod list");
    REQUIRE(preview.plan);
    CHECK(preview.plan->proposedName == "Imported mod list");
    CHECK(previewIds(preview) ==
          std::vector<std::string>{"2000000003", "2000000001", "2000000002"});
    const ImportCounts& counts = preview.plan->counts;
    CHECK(counts.total == 3);
    CHECK(counts.enabled == 3);
    CHECK(counts.installed == 1);
    CHECK(counts.missingEnabled == 2);
    CHECK(counts.missingDisabled == 0);
    CHECK(preview.plan->entries[0].status == ImportEntryStatus::Missing);
    CHECK(preview.plan->entries[1].status == ImportEntryStatus::Installed);
    CHECK(preview.plan->warnings == std::vector<std::string>{kIdenticalContentNotGuaranteed});
    CHECK(workshopIdsToLookUp(preview) == std::vector<std::string>{"2000000003", "2000000002"});
    CHECK(dumpState(w) == before); // previewing writes nothing

    const AppliedImport applied = require(share.applyImport(preview, ImportMode::New));
    CHECK(applied.playsetName == "Imported mod list");
    PlaysetService playsets(w.ctx);
    const Playset imported = require(playsets.playset(applied.playsetId));
    CHECK(shape(imported.entries) == std::vector<Identity>{{"steam-workshop", "2000000003", true},
                                                           {"steam-workshop", "2000000001", true},
                                                           {"steam-workshop", "2000000002", true}});
    REQUIRE(applied.jobs.size() == 2);
    CHECK(applied.jobs[0].identity.id == "2000000003");
    CHECK(applied.jobs[1].identity.id == "2000000002");
    CHECK(require(playsets.activePlaysetId(kGame)) == applied.playsetId);

    // A link imports the same item; the name is made unique, or typed.
    const ImportPreview again = share.previewImport(
        "https://steamcommunity.com/sharedfiles/filedetails/?id=2000000002", kGame);
    REQUIRE(again.ok());
    CHECK(again.plan->proposedName == "Imported mod list (2)");
    CHECK(require(share.applyImport(again, ImportMode::New, std::nullopt, "Friday list"))
              .playsetName == "Friday list");
}

TEST_CASE("Share codes and Workshop ID lists are told apart, and errors say which",
          "[services][share][idlist]") {
    World w;
    w.refresh();
    ShareService share(w.ctx);
    const auto failure = [&](std::string_view text, const GameKey& game) {
        ImportPreview preview = share.previewImport(text, game);
        REQUIRE_FALSE(preview.ok());
        CHECK_FALSE(preview.manifest);
        CHECK_FALSE(preview.plan);
        CHECK(workshopIdsToLookUp(preview).empty());
        return preview;
    };

    ImportPreview p = failure("PMM9.abc.def", kGame);
    CHECK(p.format == sharing::ImportTextFormat::ShareCode);
    CHECK(p.error->code == ErrorCode::UnsupportedVersion);
    p = failure("  PMM1.abc", kGame);
    CHECK(p.format == sharing::ImportTextFormat::ShareCode);
    CHECK(p.error->code == ErrorCode::ParseError);

    p = failure("hello", kGame);
    CHECK(p.format == sharing::ImportTextFormat::WorkshopIdList);
    CHECK(p.error->code == ErrorCode::ParseError);
    CHECK_THAT(p.error->message, Catch::Matchers::StartsWith("This text is neither a share code"));
    // A text that is no list fails as such, game or not.
    CHECK_THAT(failure("hello", {}).error->message,
               Catch::Matchers::StartsWith("This text is neither a share code"));

    p = failure("2000000001|2000000002", {});
    CHECK(p.error->code == ErrorCode::InvalidArgument);
    CHECK(p.error->message ==
          "Select a game first: a Workshop ID list does not say which game it is for.");
    p = failure("2000000001|2000000002", "eu4"); // no adapter in this world
    CHECK(p.error->code == ErrorCode::UnsupportedGame);
    CHECK(p.error->message == "This build of Paradox Mod Manager cannot manage Europa "
                              "Universalis IV, so the Workshop ID list cannot be imported for it.");
    p = failure("2000000001|2000000002|2000000001", kGame);
    CHECK(p.error->code == ErrorCode::ValidationFailed);
    p = failure("2000000001;1", kGame);
    CHECK_THAT(p.error->message, ContainsSubstring("Paradox games load mods in list order"));
    CHECK_FALSE(share.applyImport(p, ImportMode::New));
}

TEST_CASE("Workshop names replace the hints of missing entries and are stored",
          "[services][share][idlist]") {
    const std::string code = senderCode();
    World w;
    w.addWorkshopMod("2000000001", "UI Overhaul Example");
    w.refresh();
    ShareService share(w.ctx);
    const ImportPreview base = share.previewImport(code);
    REQUIRE(base.ok());
    CHECK(base.format == sharing::ImportTextFormat::ShareCode);
    CHECK(base.steamAppId == kAppId);
    // Missing Workshop entries only, the disabled one included; never the
    // installed one or a local mod.
    CHECK(workshopIdsToLookUp(base) == std::vector<std::string>{"2000000002", "2000000003"});
    const std::string codeHint = base.manifest->mods[0].nameHint;
    CHECK_FALSE(codeHint.empty());

    const std::vector<providers::WorkshopItemMetadata> items{
        steamItem("2000000002", std::string("Steam\x01Patch ") + std::string(300, 'x')),
        unansweredItem("2000000003", 9),
        steamItem("2000000001", "Not asked for"), // installed: ignored
        steamItem("9999", "Not in the preview")};
    const ImportPreview named = withWorkshopMetadata(base, items);
    REQUIRE(named.ok());
    CHECK(named.workshop.size() == 2);
    const PreviewWorkshopItem& patch = named.workshop.at("2000000002");
    CHECK(patch.check == WorkshopItemCheck::Ok);
    CHECK(patch.message.empty());
    // Fitted like any hint: control characters become spaces, cut to the limit.
    CHECK(patch.title.size() == sharing::kMaxNameHintBytes);
    CHECK(patch.title.rfind("Steam Patch xxx", 0) == 0);
    const PreviewWorkshopItem& gone = named.workshop.at("2000000003");
    CHECK(gone.check == WorkshopItemCheck::NotFound);
    CHECK(gone.title.empty());
    CHECK_THAT(gone.message, ContainsSubstring("was not found"));

    // The Steam title replaces the code's hint in the manifest and the plan;
    // without a title the code's hint stays.
    CHECK(named.manifest->mods[0].nameHint == patch.title);
    CHECK(named.plan->entries[0].entry.nameHint == patch.title);
    CHECK(named.manifest->mods[1].nameHint == base.manifest->mods[1].nameHint);
    CHECK(named.manifest->mods[2].nameHint == base.manifest->mods[2].nameHint);
    CHECK(sharing::validateManifest(*named.manifest).ok());
    CHECK(addedWarnings(named) ==
          std::vector<std::string>{"1 missing Workshop mod was not found on the Steam Workshop. "
                                   "It may have been deleted, or it is private or friends-only."});
    // The input preview is not changed.
    CHECK(base.manifest->mods[0].nameHint == codeHint);
    CHECK(base.workshop.empty());

    // A later answer replaces the earlier one and its warnings.
    const ImportPreview later =
        withWorkshopMetadata(named, std::vector{steamItem("2000000003", "Old Era on Steam")});
    CHECK(later.workshop.at("2000000003").check == WorkshopItemCheck::Ok);
    CHECK(later.manifest->mods[1].nameHint == "Old Era on Steam");
    CHECK(later.manifest->mods[0].nameHint == patch.title);
    CHECK(addedWarnings(later).empty());
    CHECK(later.plan->warnings.size() == base.plan->warnings.size());

    // The titles are stored with the playset.
    const AppliedImport applied = require(share.applyImport(later, ImportMode::New));
    PlaysetService playsets(w.ctx);
    const Playset imported = require(playsets.playset(applied.playsetId));
    CHECK(imported.entries[0].nameHint == patch.title);
    CHECK(imported.entries[1].nameHint == "Old Era on Steam");
    CHECK(imported.entries[2].nameHint == base.manifest->mods[2].nameHint);
    CHECK(shape(imported.entries) == shape(senderEntries()));
}

TEST_CASE("Workshop answers warn about banned, unavailable and other games' items",
          "[services][share][idlist]") {
    World w;
    w.addWorkshopMod("2000000001", "UI Overhaul Example");
    w.refresh();
    ShareService share(w.ctx);
    const ImportPreview list =
        share.previewImport("5550001|5550002|5550003|5550004|5550005", kGame);
    REQUIRE(list.ok());

    const ImportPreview mixed = withWorkshopMetadata(
        list, std::vector{unansweredItem("5550002", 15), unansweredItem("5550003", 15),
                          steamItem("5550004", "EU4 mod", "236850"), steamItem("5550005", "Fine")});
    CHECK(mixed.workshop.count("5550001") == 0);
    CHECK(mixed.workshop.at("5550002").check == WorkshopItemCheck::Unavailable);
    CHECK(mixed.workshop.at("5550004").check == WorkshopItemCheck::WrongGame);
    CHECK(mixed.workshop.at("5550004").otherGame == "eu4");
    CHECK(mixed.workshop.at("5550004").title == "EU4 mod");
    CHECK(mixed.workshop.at("5550005").check == WorkshopItemCheck::Ok);
    CHECK(addedWarnings(mixed) ==
          std::vector<std::string>{
              "Steam gave no details for 2 missing Workshop mods, so they may not download.",
              "1 missing Workshop mod is for another game, not Stellaris."});

    providers::WorkshopItemMetadata banned = steamItem("5550001", "Banned");
    banned.banned = true;
    const ImportPreview withBan = withWorkshopMetadata(list, std::vector{banned});
    CHECK(withBan.workshop.at("5550001").check == WorkshopItemCheck::Banned);
    CHECK(withBan.workshop.at("5550001").title == "Banned");
    CHECK(addedWarnings(withBan) ==
          std::vector<std::string>{"1 missing Workshop mod is banned on the Steam Workshop and "
                                   "cannot be downloaded."});

    SECTION("a list of another game's mods names that game") {
        const std::vector<providers::WorkshopItemMetadata> eu4{steamItem("5550001", "A", "236850"),
                                                               steamItem("5550002", "B", "236850"),
                                                               unansweredItem("5550003", 9)};
        const std::vector<std::string> warnings = addedWarnings(withWorkshopMetadata(list, eu4));
        REQUIRE(warnings.size() == 2);
        CHECK(warnings[1] == "These are Europa Universalis IV mods, not Stellaris mods: select "
                             "Europa Universalis IV and paste the list again.");
        // Not when another found item is this game's, another game's, or of
        // a game the catalog does not know.
        for (const providers::WorkshopItemMetadata& extra :
             {steamItem("5550004", "Mine"), steamItem("5550004", "C", "1158310"),
              steamItem("5550004", "D", "12345")}) {
            std::vector<providers::WorkshopItemMetadata> answers = eu4;
            answers.push_back(extra);
            CHECK(addedWarnings(withWorkshopMetadata(list, answers)).back().find("These are") ==
                  std::string::npos);
        }
        // Not when a listed mod is installed for this game.
        const ImportPreview partly = share.previewImport("2000000001|5550001|5550002", kGame);
        CHECK(addedWarnings(withWorkshopMetadata(partly, eu4)).back() ==
              "2 missing Workshop mods are for another game, not Stellaris.");
        // A share code names its game itself.
        World sender;
        sender.refresh();
        const std::int64_t id = makePlayset(
            sender, {workshopEntry("5550001", "A"), workshopEntry("5550002", "B")}, "Code");
        const std::string code = require(ShareService(sender.ctx).exportCode(id));
        CHECK(addedWarnings(withWorkshopMetadata(share.previewImport(code), eu4)).back() ==
              "2 missing Workshop mods are for another game, not Stellaris.");
    }
    SECTION("a failed preview stays as it is") {
        const ImportPreview failed = share.previewImport("x", kGame);
        const ImportPreview same = withWorkshopMetadata(failed, std::vector{steamItem("5", "A")});
        CHECK_FALSE(same.ok());
        CHECK(same.workshop.empty());
    }
}

TEST_CASE("A playset's Workshop ID list leaves out local and disabled entries",
          "[services][share][idlist]") {
    World w;
    w.installFixtureMods();
    w.refresh();
    const std::int64_t id = makePlayset(w, senderEntries(), "Mixed");
    ShareService share(w.ctx);
    const sharing::WorkshopIdListExport list = require(share.exportWorkshopIdList(id));
    CHECK(list.text == "2000000002|2000000001");
    CHECK(list.ids == std::vector<std::string>{"2000000002", "2000000001"});
    CHECK(list.leftOutLocal == 1);
    CHECK(list.leftOutDisabled == 1);
    CHECK(share.exportWorkshopIdList(id + 100).error().code == ErrorCode::NotFound);

    // Imported again, the list gives the same enabled Workshop mods in order.
    const ImportPreview preview = share.previewImport(list.text, kGame);
    REQUIRE(preview.ok());
    CHECK(previewIds(preview) == list.ids);
}
