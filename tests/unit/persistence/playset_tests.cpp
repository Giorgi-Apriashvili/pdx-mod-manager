#include "persistence/playset_repository.hpp"
#include "persistence/testing.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::persistence;
using namespace pmm::persistence::test;

namespace {

// In-memory database with a manual clock, so timestamps are deterministic.
struct Fixture {
    std::unique_ptr<Database> db = openMemory();
    std::shared_ptr<std::int64_t> clock = std::make_shared<std::int64_t>(1'000'000);
    PlaysetRepository repo{*db};

    Fixture() {
        testing::setClock(*db, [c = clock] { return *c; });
    }
    void advance(std::int64_t ms = 1000) { *clock += ms; }

    std::int64_t create(const std::string& name, std::vector<PlaysetEntry> entries,
                        const GameKey& game = "stellaris") {
        Playset p;
        p.game = game;
        p.name = name;
        p.entries = std::move(entries);
        return require(repo.create(p));
    }
};

// Entries with tricky provider ids: max uint64, leading zeros, local ids.
std::vector<PlaysetEntry> trickyEntries() {
    std::vector<PlaysetEntry> out = workshopEntries(5);
    auto big = entry(source::SteamWorkshop, "18446744073709551615", false, "Max id");
    big.identity.game = "stellaris";
    big.versionHint = "1.2.3";
    auto zeros = entry(source::SteamWorkshop, "000123", true, "Leading zeros");
    zeros.identity.game = "stellaris";
    auto local = entry(source::Local, "Ünïcødé local", true, "日本語 mod");
    local.identity.game = "stellaris";
    local.fingerprint = "sha256:" + std::string(64, 'f');
    out.insert(out.begin() + 2, big);
    out.push_back(zeros);
    out.insert(out.begin(), local);
    return out;
}

} // namespace

TEST_CASE("Playsets preserve exact order, flags and textual provider ids",
          "[persistence][playsets]") {
    Fixture f;
    auto entries = trickyEntries();
    const auto many = workshopEntries(200, "stellaris", 2000000000);
    entries.insert(entries.end(), many.begin(), many.end());
    const auto id = f.create("Big playset", entries);

    auto loaded = require(f.repo.get(id));
    CHECK(loaded.id == id);
    CHECK(loaded.game == "stellaris");
    CHECK(loaded.name == "Big playset");
    CHECK(loaded.createdAt == 1'000'000);
    CHECK(loaded.updatedAt == 1'000'000);
    REQUIRE(loaded.entries.size() == entries.size());
    CHECK(loaded.entries == entries);

    CHECK(queryText(*f.db, "SELECT group_concat(DISTINCT typeof(provider_id)) "
                           "FROM playset_entries") == "text");
    CHECK(queryText(*f.db, "SELECT provider_id FROM playset_entries WHERE position = 3") ==
          "18446744073709551615");
    CHECK(queryInt(*f.db,
                   "SELECT count(*) FROM playset_entries WHERE playset_id = " + std::to_string(id) +
                       " AND position BETWEEN 0 AND " + std::to_string(entries.size() - 1)) ==
          static_cast<std::int64_t>(entries.size()));
}

TEST_CASE("Entries without a game are stored under the playset's game", "[persistence][playsets]") {
    Fixture f;
    const auto id = f.create("No game on entries", {entry(source::SteamWorkshop, "1")});
    auto loaded = require(f.repo.get(id));
    REQUIRE(loaded.entries.size() == 1);
    CHECK(loaded.entries[0].identity.game == "stellaris");
}

TEST_CASE("listForGame summarises live playsets of one game", "[persistence][playsets]") {
    Fixture f;
    const auto beta = f.create("beta", workshopEntries(6));
    const auto alpha = f.create("Alpha", workshopEntries(3));
    f.create("EU4 set", workshopEntries(2, "eu4"), "eu4");
    const auto gone = f.create("Deleted", {});
    require(f.repo.remove(gone));

    auto list = require(f.repo.listForGame("stellaris"));
    REQUIRE(list.size() == 2);
    CHECK(list[0].id == alpha);
    CHECK(list[0].name == "Alpha");
    CHECK(list[0].entryCount == 3);
    CHECK(list[0].enabledCount == 2);
    CHECK(list[1].id == beta);
    CHECK(list[1].entryCount == 6);
    CHECK(list[1].enabledCount == 4);
    CHECK(require(f.repo.listForGame("eu4")).size() == 1);
    CHECK(require(f.repo.listForGame("hoi4")).empty());
}

TEST_CASE("saveEntries replaces all entries and bumps updated_at", "[persistence][playsets]") {
    Fixture f;
    const auto id = f.create("Edit me", workshopEntries(5));
    f.advance();
    auto reordered = workshopEntries(5);
    std::swap(reordered[0], reordered[4]);
    reordered[1].enabled = !reordered[1].enabled;
    reordered.pop_back();
    requireOk(f.repo.saveEntries(id, reordered));

    auto loaded = require(f.repo.get(id));
    CHECK(loaded.entries == reordered);
    CHECK(loaded.createdAt == 1'000'000);
    CHECK(loaded.updatedAt == 1'001'000);
    CHECK(require(f.repo.historyForPlayset(id)).empty()); // ordinary edits keep no history

    requireOk(f.repo.saveEntries(id, {}));
    CHECK(require(f.repo.get(id)).entries.empty());
}

TEST_CASE("Invalid entry lists are rejected and change nothing", "[persistence][playsets]") {
    Fixture f;
    const auto original = workshopEntries(3);
    const auto id = f.create("Valid", original);
    f.advance();

    auto duplicates = workshopEntries(3);
    duplicates.push_back(duplicates[0]);
    auto dup = f.repo.saveEntries(id, duplicates);
    REQUIRE_FALSE(dup.ok());
    CHECK(dup.error().code == ErrorCode::InvalidArgument);
    CHECK(dup.error().message.find("more than once") != std::string::npos);

    auto otherGame = workshopEntries(1, "eu4");
    auto wrong = f.repo.saveEntries(id, otherGame);
    REQUIRE_FALSE(wrong.ok());
    CHECK(wrong.error().code == ErrorCode::InvalidArgument);

    auto noId = f.repo.saveEntries(id, {entry(source::SteamWorkshop, "")});
    REQUIRE_FALSE(noId.ok());

    auto loaded = require(f.repo.get(id));
    CHECK(loaded.entries == original);
    CHECK(loaded.updatedAt == 1'000'000);

    Playset unnamed;
    unnamed.game = "stellaris";
    unnamed.name = "   ";
    auto created = f.repo.create(unnamed);
    REQUIRE_FALSE(created.ok());
    CHECK(created.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("A failure part-way through saveEntries keeps the previous state",
          "[persistence][playsets]") {
    Fixture f;
    const auto original = workshopEntries(4);
    const auto id = f.create("Atomic", original);
    f.advance();
    execRaw(*f.db, "CREATE TEMP TRIGGER fail_entries BEFORE INSERT ON playset_entries "
                   "WHEN NEW.provider_id = 'boom' BEGIN SELECT RAISE(ABORT, 'injected'); END");

    auto replacement = workshopEntries(3, "stellaris", 5000);
    replacement.push_back(entry(source::SteamWorkshop, "boom"));
    auto status = f.repo.saveEntries(id, replacement);
    REQUIRE_FALSE(status.ok());

    auto loaded = require(f.repo.get(id));
    CHECK(loaded.entries == original);
    CHECK(loaded.updatedAt == 1'000'000);

    // replaceWithSnapshot must not leave a history entry behind either.
    auto replaced = f.repo.replaceWithSnapshot(id, replacement, history_reason::ImportReplace);
    REQUIRE_FALSE(replaced.ok());
    CHECK(require(f.repo.historyForPlayset(id)).empty());
    CHECK(require(f.repo.get(id)).entries == original);
}

TEST_CASE("A failing outer transaction rolls back repository changes", "[persistence][playsets]") {
    Fixture f;
    const auto original = workshopEntries(3);
    const auto id = f.create("Outer", original);
    auto status = f.db->transaction([&]() -> Status {
        PMM_TRY(f.repo.saveEntries(id, workshopEntries(1)));
        PMM_TRY(f.repo.rename(id, "Renamed"));
        return makeError(ErrorCode::Cancelled, "user cancelled the import");
    });
    REQUIRE_FALSE(status.ok());
    auto loaded = require(f.repo.get(id));
    CHECK(loaded.name == "Outer");
    CHECK(loaded.entries == original);
}

TEST_CASE("Rename and duplicate", "[persistence][playsets]") {
    Fixture f;
    const auto original = trickyEntries();
    const auto id = f.create("Original", original);
    f.advance();
    requireOk(f.repo.rename(id, "Renamed ü"));
    auto renamed = require(f.repo.get(id));
    CHECK(renamed.name == "Renamed ü");
    CHECK(renamed.updatedAt == 1'001'000);

    f.advance();
    const auto copyId = require(f.repo.duplicate(id, "Copy"));
    CHECK(copyId != id);
    auto copy = require(f.repo.get(copyId));
    CHECK(copy.name == "Copy");
    CHECK(copy.game == "stellaris");
    CHECK(copy.entries == original);
    CHECK(copy.createdAt == 1'002'000);

    // The copy is independent of the original.
    requireOk(f.repo.saveEntries(copyId, {}));
    CHECK(require(f.repo.get(id)).entries == original);

    auto missing = f.repo.rename(424242, "x");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::NotFound);
    CHECK_FALSE(f.repo.rename(id, "").ok());
}

TEST_CASE("Deleting snapshots the playset and restore brings it back exactly",
          "[persistence][playsets]") {
    Fixture f;
    const auto original = trickyEntries();
    const auto id = f.create("To delete", original);
    f.advance();
    const auto historyId = require(f.repo.remove(id));

    auto gone = f.repo.get(id);
    REQUIRE_FALSE(gone.ok());
    CHECK(gone.error().code == ErrorCode::NotFound);
    CHECK(require(f.repo.listForGame("stellaris")).empty());
    CHECK_FALSE(f.repo.saveEntries(id, {}).ok());
    CHECK_FALSE(f.repo.remove(id).ok());

    auto history = require(f.repo.historyForGame("stellaris"));
    REQUIRE(history.size() == 1);
    CHECK(history[0].id == historyId);
    CHECK(history[0].playsetId == id);
    CHECK(history[0].reason == history_reason::Deleted);
    CHECK(history[0].createdAt == 1'001'000);
    CHECK(history[0].snapshot.name == "To delete");
    CHECK(history[0].snapshot.entries == original);

    f.advance();
    const auto restoredId = require(f.repo.restore(historyId));
    CHECK(restoredId == id);
    auto restored = require(f.repo.get(id));
    CHECK(restored.name == "To delete");
    CHECK(restored.entries == original);
    CHECK(restored.createdAt == 1'000'000);
    CHECK(require(f.repo.listForGame("stellaris")).size() == 1);
    // Restoring a deleted playset does not need an extra snapshot.
    CHECK(require(f.repo.historyForPlayset(id)).size() == 1);
}

TEST_CASE("replaceWithSnapshot keeps the previous state recoverable", "[persistence][playsets]") {
    Fixture f;
    const auto before = trickyEntries();
    const auto id = f.create("Imported over", before);
    f.advance();
    const auto after = workshopEntries(2, "stellaris", 7000);
    const auto historyId =
        require(f.repo.replaceWithSnapshot(id, after, history_reason::ImportReplace));
    auto current = require(f.repo.get(id));
    CHECK(current.entries == after);
    CHECK(current.updatedAt == 1'001'000);

    auto entryFromHistory = require(f.repo.historyEntry(historyId));
    CHECK(entryFromHistory.reason == history_reason::ImportReplace);
    CHECK(entryFromHistory.snapshot.entries == before);

    // Undo the import; the restore itself snapshots the imported state.
    f.advance();
    requireOk(f.repo.rename(id, "Renamed after import"));
    CHECK(require(f.repo.restore(historyId)) == id);
    auto undone = require(f.repo.get(id));
    CHECK(undone.name == "Imported over");
    CHECK(undone.entries == before);

    auto history = require(f.repo.historyForPlayset(id));
    REQUIRE(history.size() == 2);
    CHECK(history[0].reason == history_reason::BeforeRestore); // newest first
    CHECK(history[0].snapshot.name == "Renamed after import");
    CHECK(history[0].snapshot.entries == after);
    CHECK(history[1].id == historyId);

    // ...and the undo can be undone.
    CHECK(require(f.repo.restore(history[0].id)) == id);
    CHECK(require(f.repo.get(id)).entries == after);

    CHECK_FALSE(f.repo.replaceWithSnapshot(id, after, "").ok());
    auto unknown = f.repo.restore(999999);
    REQUIRE_FALSE(unknown.ok());
    CHECK(unknown.error().code == ErrorCode::NotFound);
}

TEST_CASE("Purging history keeps the newest entries and drops orphaned playsets",
          "[persistence][playsets]") {
    Fixture f;
    const auto kept = f.create("Kept", workshopEntries(2));
    for (int i = 0; i < 4; ++i) {
        f.advance();
        require(f.repo.replaceWithSnapshot(kept, workshopEntries(i + 1), history_reason::BulkEdit));
    }
    const auto deleted = f.create("Deleted long ago", workshopEntries(3));
    f.advance();
    const auto deletionHistory = require(f.repo.remove(deleted));
    f.advance(100'000);
    const std::int64_t cutoff = *f.clock;

    // Keep two entries per playset: removes the two oldest of "Kept" only.
    CHECK(require(f.repo.purgeHistoryOlderThan(cutoff, 2)) == 2);
    CHECK(require(f.repo.historyForPlayset(kept)).size() == 2);
    CHECK(require(f.repo.historyForPlayset(deleted)).size() == 1);
    CHECK(queryInt(*f.db, "SELECT count(*) FROM playsets") == 2);

    // Without a minimum, everything older than the cutoff goes, and the
    // deleted playset (now without any history) is removed for good.
    CHECK(require(f.repo.purgeHistoryOlderThan(cutoff, 0)) == 3);
    CHECK(require(f.repo.historyForGame("stellaris")).empty());
    CHECK(queryInt(*f.db, "SELECT count(*) FROM playsets") == 1);
    CHECK(queryInt(*f.db, "SELECT count(*) FROM playset_entries WHERE playset_id = " +
                              std::to_string(deleted)) == 0);
    CHECK(require(f.repo.get(kept)).entries == workshopEntries(4));
    CHECK_FALSE(f.repo.restore(deletionHistory).ok());
    CHECK_FALSE(f.repo.purgeHistoryOlderThan(cutoff, -1).ok());
}

TEST_CASE("Restoring a purged playset recreates it under a new id", "[persistence][playsets]") {
    Fixture f;
    const auto original = trickyEntries();
    const auto id = f.create("Recreated", original);
    f.advance();
    const auto historyId = require(f.repo.remove(id));
    // Simulate the playset row having been purged while its history remains.
    execRaw(*f.db, "DELETE FROM playsets WHERE id = " + std::to_string(id));

    f.advance();
    const auto newId = require(f.repo.restore(historyId));
    CHECK(newId != id);
    auto restored = require(f.repo.get(newId));
    CHECK(restored.name == "Recreated");
    CHECK(restored.entries == original);
    CHECK(restored.createdAt == 1'000'000);
    CHECK(restored.updatedAt == 1'002'000);
}

namespace {

// Two local mods whose descriptor stems are Latin-1 bytes: not valid UTF-8, and
// identical once each bad byte is replaced with U+FFFD.
std::vector<PlaysetEntry> nonUtf8Entries() {
    auto e9 = entry(source::Local, "caf\xE9", true, "Cafe 1");
    auto e8 = entry(source::Local, "caf\xE8", true, "Cafe 2");
    e9.identity.game = "stellaris";
    e8.identity.game = "stellaris";
    return {e9, e8};
}

// Inserts an entry row directly, as an older build could have written it.
void insertRawEntry(Database& db, std::int64_t playsetId, int position, const std::string& hexId) {
    execRaw(db, "INSERT INTO playset_entries (playset_id, position, source, provider_id, "
                "enabled) VALUES (" +
                    std::to_string(playsetId) + ", " + std::to_string(position) +
                    ", 'local', CAST(X'" + hexId + "' AS TEXT), 1)");
}

} // namespace

TEST_CASE("Provider ids that are not valid UTF-8 are rejected when written",
          "[persistence][playsets]") {
    Fixture f;
    Playset bad;
    bad.game = "stellaris";
    bad.name = "Non-UTF-8 ids";
    bad.entries = nonUtf8Entries();
    auto created = f.repo.create(bad);
    REQUIRE_FALSE(created.ok());
    CHECK(created.error().code == ErrorCode::InvalidArgument);
    CHECK(created.error().message.find("not valid UTF-8") != std::string::npos);
    CHECK(created.error().detail.find("caf\xEF\xBF\xBD") != std::string::npos);
    CHECK(queryInt(*f.db, "SELECT count(*) FROM playsets") == 0);

    const auto original = workshopEntries(3);
    const auto id = f.create("Valid", original);
    f.advance();
    auto saved = f.repo.saveEntries(id, nonUtf8Entries());
    REQUIRE_FALSE(saved.ok());
    CHECK(saved.error().code == ErrorCode::InvalidArgument);
    auto replaced = f.repo.replaceWithSnapshot(id, nonUtf8Entries(), history_reason::BulkEdit);
    REQUIRE_FALSE(replaced.ok());
    CHECK(replaced.error().code == ErrorCode::InvalidArgument);
    auto badSource = original;
    badSource[1].identity.source = "steam\xFF";
    auto savedSource = f.repo.saveEntries(id, badSource);
    REQUIRE_FALSE(savedSource.ok());
    CHECK(savedSource.error().code == ErrorCode::InvalidArgument);

    auto loaded = require(f.repo.get(id));
    CHECK(loaded.entries == original);
    CHECK(loaded.updatedAt == 1'000'000);
    CHECK(require(f.repo.historyForPlayset(id)).empty());
}

TEST_CASE("Non-ASCII provider ids survive delete and restore byte for byte",
          "[persistence][playsets]") {
    Fixture f;
    auto entries = workshopEntries(2);
    for (const char* id : {"caf\xC3\xA9", "caf\xC3\xA8", "\xE6\x97\xA5\xE6\x9C\xAC",
                           "emoji \xF0\x9F\x98\x80", "replacement \xEF\xBF\xBD"}) {
        auto local = entry(source::Local, id);
        local.identity.game = "stellaris";
        entries.push_back(std::move(local));
    }
    const auto id = f.create("Unicode ids", entries);
    const auto deleted = require(f.repo.remove(id));
    CHECK(require(f.repo.restore(deleted)) == id);
    CHECK(require(f.repo.get(id)).entries == entries);

    const auto bulk = require(f.repo.replaceWithSnapshot(id, {}, history_reason::BulkEdit));
    CHECK(require(f.repo.restore(bulk)) == id);
    CHECK(require(f.repo.get(id)).entries == entries);
}

TEST_CASE("A playset whose stored ids cannot be snapshotted is left intact",
          "[persistence][playsets]") {
    Fixture f;
    const auto id = f.create("Legacy rows", workshopEntries(2));
    insertRawEntry(*f.db, id, 2, "636166E9"); // "caf\xE9"
    insertRawEntry(*f.db, id, 3, "636166E8"); // "caf\xE8"
    const auto before = require(f.repo.get(id));
    REQUIRE(before.entries.size() == 4);
    REQUIRE(before.entries[2].identity.id == "caf\xE9");
    f.advance();

    // Deleting or bulk-editing would write a snapshot that restore() cannot
    // bring back, so both refuse up front; copying would spread the rows.
    auto removed = f.repo.remove(id);
    REQUIRE_FALSE(removed.ok());
    CHECK(removed.error().code == ErrorCode::InvalidArgument);
    CHECK(removed.error().message.find("Legacy rows") != std::string::npos);
    auto bulk = f.repo.replaceWithSnapshot(id, workshopEntries(1), history_reason::BulkEdit);
    REQUIRE_FALSE(bulk.ok());
    CHECK(bulk.error().code == ErrorCode::InvalidArgument);
    auto copy = f.repo.duplicate(id, "Copy");
    REQUIRE_FALSE(copy.ok());
    CHECK(copy.error().code == ErrorCode::InvalidArgument);

    auto after = require(f.repo.get(id));
    CHECK(after.entries == before.entries);
    CHECK(after.updatedAt == before.updatedAt);
    CHECK(queryInt(*f.db, "SELECT count(*) FROM playset_history") == 0);
    CHECK(queryInt(*f.db, "SELECT count(*) FROM playsets") == 1);

    // Dropping the offending entries makes the playset deletable again.
    requireOk(f.repo.saveEntries(id, workshopEntries(2)));
    const auto deleted = require(f.repo.remove(id));
    CHECK(require(f.repo.restore(deleted)) == id);
    CHECK(require(f.repo.get(id)).entries == workshopEntries(2));
}

TEST_CASE("Restoring a snapshot that lists a mod twice fails with a clear error",
          "[persistence][playsets]") {
    // Older builds replaced invalid UTF-8 in snapshot ids with U+FFFD, which
    // turned "caf\xE9" and "caf\xE8" into the same id.
    Fixture f;
    const auto id = f.create("Collapsed ids", workshopEntries(1));
    const auto deleted = require(f.repo.remove(id));
    const std::string entryJson =
        R"({"source":"local","id":"caf)"
        "\xEF\xBF\xBD"
        R"(","enabled":true,"nameHint":"","versionHint":"","fingerprint":""})";
    const std::string json =
        R"({"format":1,"name":"Collapsed ids","createdAt":1000000,"updatedAt":1000000,)"
        R"("entries":[)" +
        entryJson + "," + entryJson + "]}";
    execRaw(*f.db, "UPDATE playset_history SET snapshot_json = '" + json +
                       "' WHERE id = " + std::to_string(deleted));

    auto restored = f.repo.restore(deleted);
    REQUIRE_FALSE(restored.ok());
    CHECK(restored.error().code == ErrorCode::InvalidArgument);
    CHECK(restored.error().message.find("cannot be restored") != std::string::npos);
    CHECK_FALSE(f.repo.get(id).ok()); // still deleted, nothing half-written
    CHECK(queryInt(*f.db, "SELECT count(*) FROM playset_entries WHERE playset_id = " +
                              std::to_string(id)) == 1);
}
