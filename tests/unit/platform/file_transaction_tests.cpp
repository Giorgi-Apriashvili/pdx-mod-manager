#include "core/log.hpp"
#include "platform/file_transaction.hpp"
#include "platform/fs.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <windows.h>

#include <chrono>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

using namespace pmm;
using namespace pmm::platform;
using pmm::test::TempDir;

namespace {

// Three targets in folders with spaces and non-ASCII names: two existing
// files and one that does not exist yet (its folder does not either).
struct Scenario {
    TempDir temp;
    fs::path journal = temp / "journal";
    fs::path gameDir = temp.path() / test::unicodeSubdir() / "Stellaris settings";
    fs::path first = gameDir / "dlc_load.json";
    fs::path created = gameDir / "new folder" / "content_load.json";
    fs::path third = gameDir / "settings.txt";
    fs::path unrelated = gameDir / "unrelated.txt";

    Scenario() {
        test::writeText(first, "{\"enabled_mods\":[]}");
        test::writeText(third, "language=\"l_english\"\n");
        test::writeText(unrelated, "leave me alone");
    }

    std::vector<FileWrite> writes() const {
        return {{first, "{\"enabled_mods\":[\"mod/ugc_1.mod\"]}"},
                {created, "{\"enabledMods\":[]}"},
                {third, "language=\"l_german\"\n"}};
    }

    void checkOriginal() const {
        CHECK(test::readText(first) == "{\"enabled_mods\":[]}");
        CHECK_FALSE(fs::exists(created));
        CHECK(test::readText(third) == "language=\"l_english\"\n");
        CHECK(test::readText(unrelated) == "leave me alone");
    }

    void checkApplied() const {
        CHECK(test::readText(first) == "{\"enabled_mods\":[\"mod/ugc_1.mod\"]}");
        CHECK(test::readText(created) == "{\"enabledMods\":[]}");
        CHECK(test::readText(third) == "language=\"l_german\"\n");
        CHECK(test::readText(unrelated) == "leave me alone");
    }
};

TransactionState stateOf(const FileTransactionManager& manager, const std::string& id) {
    const auto record = manager.load(id);
    REQUIRE(record.ok());
    return record->state;
}

std::string onlyTransactionId(const FileTransactionManager& manager) {
    const auto all = manager.list();
    REQUIRE(all.ok());
    REQUIRE(all->size() == 1);
    return all->front().id;
}

// Leaves the scenario's change "applying", as a crash after `writes` writes would.
TransactionRecord crashAfter(FileTransactionManager& manager, const Scenario& s,
                             std::size_t writes) {
    manager.setFaultsForTesting({std::nullopt, writes});
    REQUIRE_FALSE(manager.apply("crashing", s.writes()).ok());
    manager.setFaultsForTesting({});
    const auto incomplete = manager.listIncomplete();
    REQUIRE(incomplete.ok());
    REQUIRE_FALSE(incomplete->empty());
    return incomplete->back();
}

// Rewinds an interrupted journal to "prepared", as if the crash hit during backups.
void rewindToPrepared(const fs::path& journalFile) {
    std::string journal = test::readText(journalFile);
    const auto at = journal.find("\"applying\"");
    REQUIRE(at != std::string::npos);
    journal.replace(at, 10, "\"prepared\"");
    test::writeText(journalFile, journal);
}

} // namespace

TEST_CASE("apply replaces every target and keeps backups", "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    const auto record = manager.apply("Launch Stellaris — Ünïcødé", s.writes());
    REQUIRE(record.ok());
    s.checkApplied();

    CHECK(record->state == TransactionState::Committed);
    CHECK(record->label == "Launch Stellaris — Ünïcødé");
    REQUIRE(record->entries.size() == 3);
    CHECK(record->entries[0].existed);
    CHECK_FALSE(record->entries[1].existed);
    CHECK(record->entries[1].backupFile.empty());
    CHECK(record->entries[2].existed);
    CHECK(test::readText(record->directory / record->entries[0].backupFile) ==
          "{\"enabled_mods\":[]}");
    CHECK(record->directory == s.journal / record->id);

    // The journal survives a "restart" (a fresh manager) unchanged.
    FileTransactionManager reopened(s.journal);
    const auto loaded = reopened.load(record->id);
    REQUIRE(loaded.ok());
    CHECK(loaded->state == TransactionState::Committed);
    CHECK(loaded->label == record->label);
    REQUIRE(loaded->entries.size() == 3);
    CHECK(loaded->entries[1].target == s.created);
    CHECK(loaded->entries[2].backupSha256 == record->entries[2].backupSha256);
    CHECK(reopened.listIncomplete()->empty());
    REQUIRE(reopened.listCommitted()->size() == 1);

    // Nothing but the targets in the target folder: no temporary files.
    CHECK(test::entryNames(s.gameDir) ==
          std::vector<std::string>{"dlc_load.json", "new folder", "settings.txt", "unrelated.txt"});
}

TEST_CASE("A write failure at target k rolls back exactly the touched targets",
          "[platform][transaction]") {
    const std::size_t k = GENERATE(0u, 1u, 2u);
    CAPTURE(k);
    Scenario s;
    FileTransactionManager manager(s.journal);
    manager.setFaultsForTesting({k, std::nullopt});

    const auto result = manager.apply("failing", s.writes());
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::IoError);
    s.checkOriginal();

    const std::string id = onlyTransactionId(manager);
    CHECK(stateOf(manager, id) == TransactionState::RolledBack);
    CHECK(manager.load(id)->lastError == result.error().message);
    CHECK(manager.listIncomplete()->empty());
    CHECK(manager.listCommitted()->empty());
}

TEST_CASE("A crash mid-apply is detected and recovered after restart", "[platform][transaction]") {
    const std::size_t writesBeforeCrash = GENERATE(0u, 1u, 2u, 3u);
    CAPTURE(writesBeforeCrash);
    Scenario s;
    std::string id;
    {
        FileTransactionManager manager(s.journal);
        manager.setFaultsForTesting({std::nullopt, writesBeforeCrash});
        const auto result = manager.apply("crashing", s.writes());
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().code == ErrorCode::Interrupted);
        id = onlyTransactionId(manager);
    }
    if (writesBeforeCrash >= 1)
        CHECK(test::readText(s.first) == "{\"enabled_mods\":[\"mod/ugc_1.mod\"]}");
    CHECK(fs::exists(s.created) == (writesBeforeCrash >= 2));

    FileTransactionManager restarted(s.journal);
    const auto incomplete = restarted.listIncomplete();
    REQUIRE(incomplete.ok());
    REQUIRE(incomplete->size() == 1);
    CHECK(incomplete->front().id == id);
    CHECK(incomplete->front().state == TransactionState::Applying);

    const auto recovered = restarted.recover(id);
    REQUIRE(recovered.ok());
    CHECK(recovered->state == TransactionState::Recovered);
    s.checkOriginal();
    CHECK(restarted.listIncomplete()->empty());
    CHECK(stateOf(restarted, id) == TransactionState::Recovered);

    const auto again = restarted.recover(id);
    REQUIRE_FALSE(again.ok());
    CHECK(again.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("Recovery keeps a new file that someone else changed after the crash",
          "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    manager.setFaultsForTesting({std::nullopt, 3u});
    REQUIRE_FALSE(manager.apply("crashing", s.writes()).ok());
    test::writeText(s.created, "written by the game afterwards");

    REQUIRE(manager.recover(onlyTransactionId(manager)).ok());
    CHECK(test::readText(s.created) == "written by the game afterwards");
    CHECK(test::readText(s.first) == "{\"enabled_mods\":[]}");
}

TEST_CASE("A crash while preparing leaves the targets untouched", "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    manager.setFaultsForTesting({std::nullopt, 0u});
    REQUIRE_FALSE(manager.apply("crashing", s.writes()).ok());
    const std::string id = onlyTransactionId(manager);

    rewindToPrepared(s.journal / id / "journal.json");
    test::writeText(s.first, "changed later by the user");

    REQUIRE(manager.listIncomplete()->front().state == TransactionState::Prepared);
    const auto recovered = manager.recover(id);
    REQUIRE(recovered.ok());
    CHECK(test::readText(s.first) == "changed later by the user");
}

TEST_CASE("restore puts back the configuration from before a committed change",
          "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    const auto record = manager.apply("launch", s.writes());
    REQUIRE(record.ok());
    test::writeText(s.third, "modified by the game while running");

    const auto restored = manager.restore(record->id);
    REQUIRE(restored.ok());
    CHECK(restored->state == TransactionState::Restored);
    s.checkOriginal();
    CHECK(manager.listCommitted()->empty());

    REQUIRE(manager.restore(record->id).ok()); // repeatable
    s.checkOriginal();

    manager.setFaultsForTesting({0u, std::nullopt});
    REQUIRE_FALSE(manager.apply("failing", s.writes()).ok());
    const auto all = manager.list();
    REQUIRE(all->size() == 2);
    const auto rejected = manager.restore(all->back().id);
    REQUIRE_FALSE(rejected.ok());
    CHECK(rejected.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("A damaged backup aborts restore before anything changes", "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    const auto record = manager.apply("launch", s.writes());
    REQUIRE(record.ok());
    test::writeText(record->directory / record->entries[2].backupFile, "tampered");

    const auto restored = manager.restore(record->id);
    REQUIRE_FALSE(restored.ok());
    CHECK(restored.error().code == ErrorCode::IntegrityCheckFailed);
    s.checkApplied();
    CHECK(stateOf(manager, record->id) == TransactionState::Committed);
}

TEST_CASE("Recovery needs no backup for a target that is already original",
          "[platform][transaction]") {
    const bool damaged = GENERATE(false, true);
    CAPTURE(damaged);
    Scenario s;
    FileTransactionManager manager(s.journal);
    const auto spoil = [&](const TransactionRecord& record, std::size_t entry) {
        const fs::path backup = record.directory / record.entries[entry].backupFile;
        if (damaged)
            test::writeText(backup, "tampered");
        else
            fs::remove(backup);
    };

    SECTION("a crash before the first write: every target is original or absent") {
        const TransactionRecord record = crashAfter(manager, s, 0);
        spoil(record, 0);
        spoil(record, 2);
        const auto recovered = manager.recover(record.id);
        REQUIRE(recovered.ok());
        CHECK(recovered->state == TransactionState::Recovered);
    }
    SECTION("a changed target was put back by hand; the new file is still removed") {
        const TransactionRecord record = crashAfter(manager, s, 2);
        test::writeText(s.first, "{\"enabled_mods\":[]}");
        spoil(record, 0);
        REQUIRE(manager.recover(record.id).ok());
    }
    s.checkOriginal();
    CHECK(manager.listIncomplete()->empty());
}

TEST_CASE("Recovery refuses before changing anything when a needed backup is gone",
          "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    const TransactionRecord record = crashAfter(manager, s, 3);
    const fs::path backup = record.directory / record.entries[2].backupFile;
    SECTION("missing") {
        fs::remove(backup);
        const auto recovered = manager.recover(record.id);
        REQUIRE_FALSE(recovered.ok());
        CHECK(recovered.error().code == ErrorCode::IntegrityCheckFailed);
        CHECK(recovered.error().message.find("settings.txt") != std::string::npos);
        CHECK(recovered.error().message.find("missing") != std::string::npos);
    }
    SECTION("damaged") {
        test::writeText(backup, "tampered");
        const auto recovered = manager.recover(record.id);
        REQUIRE_FALSE(recovered.ok());
        CHECK(recovered.error().code == ErrorCode::IntegrityCheckFailed);
        CHECK(recovered.error().message.find("damaged") != std::string::npos);
    }
    SECTION("missing, and the target cannot be read: the read error, as a retry may work") {
        fs::remove(backup);
        fs::remove(s.third);
        fs::create_directory(s.third);
        const auto recovered = manager.recover(record.id);
        REQUIRE_FALSE(recovered.ok());
        CHECK(recovered.error().code == ErrorCode::IoError);
        CHECK(recovered.error().message.find("settings.txt") != std::string::npos);
        // Gone for good: only its backup could put it back.
        fs::remove(s.third);
        const auto again = manager.recover(record.id);
        REQUIRE_FALSE(again.ok());
        CHECK(again.error().code == ErrorCode::IntegrityCheckFailed);
        test::writeText(s.third, "language=\"l_german\"\n");
    }
    // Nothing was changed, and the change is still waiting.
    s.checkApplied();
    CHECK(stateOf(manager, record.id) == TransactionState::Applying);
    CHECK(manager.listIncomplete()->size() == 1);
}

TEST_CASE("A backup that another program keeps open is not reported as gone",
          "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    const TransactionRecord record = crashAfter(manager, s, 3);
    const fs::path backup = record.directory / record.entries[2].backupFile;
    // Opened without sharing, as a virus scanner might.
    const HANDLE lock = CreateFileW(backup.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(lock != INVALID_HANDLE_VALUE);
    const auto recovered = manager.recover(record.id);
    CloseHandle(lock);
    REQUIRE_FALSE(recovered.ok());
    CHECK(recovered.error().code == ErrorCode::IoError);
    CHECK(recovered.error().message.find("settings.txt") != std::string::npos);
    CHECK(recovered.error().message.find("close it and try again") != std::string::npos);
    s.checkApplied();
    CHECK(stateOf(manager, record.id) == TransactionState::Applying);

    // Once it is released, Recover works.
    REQUIRE(manager.recover(record.id).ok());
    s.checkOriginal();
}

TEST_CASE("restore needs no backup for a target already back to its original",
          "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    const auto record = manager.apply("launch", s.writes());
    REQUIRE(record.ok());
    test::writeText(s.first, "{\"enabled_mods\":[]}");
    fs::remove(record->directory / record->entries[0].backupFile);

    const auto restored = manager.restore(record->id);
    REQUIRE(restored.ok());
    CHECK(restored->state == TransactionState::Restored);
    s.checkOriginal();
}

TEST_CASE("abandon gives up an interrupted change without touching its files",
          "[platform][transaction]") {
    const bool prepared = GENERATE(false, true);
    CAPTURE(prepared);
    Scenario s;
    std::string id;
    fs::path directory;
    {
        FileTransactionManager manager(s.journal);
        const TransactionRecord record = crashAfter(manager, s, 2);
        id = record.id;
        directory = record.directory;
        fs::remove(directory / record.entries[0].backupFile);
        REQUIRE(manager.recover(id).error().code == ErrorCode::IntegrityCheckFailed);
        if (prepared)
            rewindToPrepared(directory / "journal.json");

        const auto abandoned = manager.abandon(id, "The backup is missing.");
        REQUIRE(abandoned.ok());
        CHECK(abandoned->state == TransactionState::Abandoned);
        CHECK(abandoned->lastError == "The backup is missing.");
        CHECK(manager.listIncomplete()->empty());
    }
    // The files stay as the interrupted change left them.
    CHECK(test::readText(s.first) == "{\"enabled_mods\":[\"mod/ugc_1.mod\"]}");
    CHECK(test::readText(s.created) == "{\"enabledMods\":[]}");
    CHECK(test::readText(s.third) == "language=\"l_english\"\n");
    CHECK(test::readText(s.unrelated) == "leave me alone");
    // The folder keeps the journal and the remaining backup.
    CHECK(test::entryNames(directory) == std::vector<std::string>{"0002.bak", "journal.json"});

    // A restart reads it back as given up.
    FileTransactionManager restarted(s.journal);
    const auto loaded = restarted.load(id);
    REQUIRE(loaded.ok());
    CHECK(loaded->state == TransactionState::Abandoned);
    CHECK(loaded->lastError == "The backup is missing.");
    REQUIRE(loaded->entries.size() == 3);
    CHECK(loaded->entries[1].target == s.created);
    CHECK(restarted.listIncomplete()->empty());
    CHECK(restarted.listCommitted()->empty());
    CHECK(restarted.list()->size() == 1);

    const auto again = restarted.abandon(id);
    REQUIRE_FALSE(again.ok());
    CHECK(again.error().code == ErrorCode::InvalidArgument);
    CHECK(again.error().message.find("already discarded") != std::string::npos);
    CHECK(restarted.recover(id).error().code == ErrorCode::InvalidArgument);
    CHECK(restarted.restore(id).error().code == ErrorCode::InvalidArgument);
    CHECK(stateOf(restarted, id) == TransactionState::Abandoned);
}

TEST_CASE("abandon refuses changes that are not interrupted", "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    std::string id;
    TransactionState state = TransactionState::Committed;
    std::string expected = "was not interrupted";
    SECTION("committed") {
        REQUIRE(manager.apply("launch", s.writes()).ok());
        id = onlyTransactionId(manager);
    }
    SECTION("restored") {
        const auto record = manager.apply("launch", s.writes());
        REQUIRE(record.ok());
        id = record->id;
        REQUIRE(manager.restore(id).ok());
        state = TransactionState::Restored;
    }
    SECTION("rolled back") {
        manager.setFaultsForTesting({1u, std::nullopt});
        REQUIRE_FALSE(manager.apply("failing", s.writes()).ok());
        id = onlyTransactionId(manager);
        state = TransactionState::RolledBack;
    }
    SECTION("recovered") {
        id = crashAfter(manager, s, 1).id;
        REQUIRE(manager.recover(id).ok());
        state = TransactionState::Recovered;
        expected = "already recovered";
    }
    const fs::path journalFile = s.journal / id / "journal.json";
    const std::string before = test::readText(journalFile);
    const auto abandoned = manager.abandon(id, "reason");
    REQUIRE_FALSE(abandoned.ok());
    CHECK(abandoned.error().code == ErrorCode::InvalidArgument);
    CHECK(abandoned.error().message.find(expected) != std::string::npos);
    CHECK(stateOf(manager, id) == state);
    CHECK(test::readText(journalFile) == before);
    CHECK(manager.abandon("../escape").error().code == ErrorCode::InvalidArgument);
    CHECK(manager.abandon("20260101T000000000Z-0000").error().code == ErrorCode::NotFound);
}

TEST_CASE("A permission failure surfaces PermissionDenied and rolls back",
          "[platform][transaction]") {
    Scenario s;
    // The second write targets a locked file; the first must be rolled back.
    const fs::path lockedDir = s.temp.path() / test::unicodeSubdir() / "locked dir";
    const fs::path locked = lockedDir / "locked.txt";
    test::writeText(locked, "locked original");
    fs::permissions(locked,
                    fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::remove);
    FileTransactionManager manager(s.journal);
    const auto result =
        manager.apply("locked", {{s.first, "new"}, {locked, "new"}, {s.third, "new"}});
    fs::permissions(lockedDir, fs::perms::owner_all, fs::perm_options::add);
    fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::add);

    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::PermissionDenied);
    CHECK_FALSE(result.error().detail.empty());
    s.checkOriginal();
    CHECK(test::readText(locked) == "locked original");
    CHECK(stateOf(manager, onlyTransactionId(manager)) == TransactionState::RolledBack);
}

TEST_CASE("A folder in place of a target fails without touching anything",
          "[platform][transaction]") {
    Scenario s;
    fs::create_directories(s.created);
    FileTransactionManager manager(s.journal);
    const auto result = manager.apply("blocked", s.writes());
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::IoError);
    CHECK(test::readText(s.first) == "{\"enabled_mods\":[]}");
    CHECK(fs::is_directory(s.created));
    CHECK(manager.list()->empty());
}

TEST_CASE("apply validates its input", "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    CHECK(manager.apply("empty", {}).error().code == ErrorCode::InvalidArgument);
    CHECK(manager.apply("relative", {{"relative/file.txt", "x"}}).error().code ==
          ErrorCode::InvalidArgument);
    CHECK(
        manager
            .apply("twice", {{s.first, "a"}, {s.first.parent_path() / "." / "dlc_load.json", "b"}})
            .error()
            .code == ErrorCode::InvalidArgument);
    CHECK(manager.load("../escape").error().code == ErrorCode::InvalidArgument);
    CHECK(manager.load("20260101T000000000Z-0000").error().code == ErrorCode::NotFound);
    CHECK(manager.list()->empty());
    s.checkOriginal();
}

TEST_CASE("apply refuses a target path that has no exact UTF-8 form", "[platform][transaction]") {
    // The journal records targets as UTF-8 text. A path that does not survive
    // that round trip would be journaled as a different file, so recover() and
    // restore() would write there and still report success.
    Scenario s;
    // NTFS accepts names with unpaired surrogates.
    const fs::path folder = s.gameDir / fs::path(L"caf\xD800");
    const fs::path target = folder / "dlc_load.json";
    // Some filesystems reject such names; then there is no original.
    std::error_code ec;
    const bool hasOriginal = fs::create_directory(folder, ec);
    if (hasOriginal)
        test::writeText(target, "ORIGINAL");

    FileTransactionManager manager(s.journal);
    const auto result = manager.apply("lossy", {{s.first, "new"}, {target, "NEW"}});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::InvalidArgument);
    CHECK(result.error().message.find("dlc_load.json") != std::string::npos);
    CHECK(result.error().detail.find("caf\xEF\xBF\xBD") != std::string::npos); // valid UTF-8
    if (hasOriginal)
        CHECK(test::readText(target) == "ORIGINAL");
    else
        CHECK_FALSE(fs::exists(target, ec));
    s.checkOriginal();
    CHECK_FALSE(fs::exists(s.journal));
}

TEST_CASE("A label that is not valid UTF-8 is journaled with replacement characters",
          "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    const auto record = manager.apply("launch \xFF", {{s.first, "new"}});
    REQUIRE(record.ok());
    const auto loaded = manager.load(record->id);
    REQUIRE(loaded.ok());
    CHECK(loaded->label == "launch \xEF\xBF\xBD");
    CHECK(loaded->state == TransactionState::Committed);
}

TEST_CASE("Transaction ids sort by creation time and prune keeps the newest",
          "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    std::vector<std::string> ids;
    for (int i = 0; i < 4; ++i) {
        const auto record = manager.apply("launch " + std::to_string(i),
                                          {{s.first, "version " + std::to_string(i)}});
        REQUIRE(record.ok());
        CHECK(record->id.size() == 24);
        if (!ids.empty())
            CHECK(ids.back() < record->id);
        ids.push_back(record->id);
    }
    manager.setFaultsForTesting({std::nullopt, 0u});
    REQUIRE_FALSE(manager.apply("interrupted", {{s.first, "x"}}).ok());
    manager.setFaultsForTesting({});

    REQUIRE(manager.prune(2).ok());
    const auto remaining = manager.list();
    REQUIRE(remaining.ok());
    REQUIRE(remaining->size() == 3);
    CHECK((*remaining)[0].id == ids[2]);
    CHECK((*remaining)[1].id == ids[3]);
    CHECK((*remaining)[2].state == TransactionState::Applying); // never pruned
    CHECK_FALSE(fs::exists(s.journal / ids[0]));

    REQUIRE(manager.prune(0).ok());
    CHECK(manager.list()->size() == 1);
}

TEST_CASE("prune treats an abandoned change as finished", "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    const std::string abandoned = crashAfter(manager, s, 1).id;
    REQUIRE(manager.abandon(abandoned).ok());
    const std::string interrupted = crashAfter(manager, s, 1).id;
    const auto committed = manager.apply("launch", {{s.third, "newest"}});
    REQUIRE(committed.ok());

    // The abandoned change is the oldest finished one, so it goes first.
    REQUIRE(manager.prune(1).ok());
    const auto remaining = manager.list();
    REQUIRE(remaining.ok());
    REQUIRE(remaining->size() == 2);
    CHECK((*remaining)[0].id == interrupted);
    CHECK((*remaining)[1].id == committed->id);
    CHECK_FALSE(fs::exists(s.journal / abandoned));
}

TEST_CASE("prune counts an abandoned change from when it was abandoned",
          "[platform][transaction]") {
    Scenario s;
    FileTransactionManager manager(s.journal);
    const TransactionRecord crashed = crashAfter(manager, s, 2);
    fs::remove(crashed.directory / crashed.entries[0].backupFile);
    std::vector<std::string> launches;
    for (int i = 0; i < 3; ++i) {
        const auto record = manager.apply("launch " + std::to_string(i), {{s.unrelated, "x"}});
        REQUIRE(record.ok());
        launches.push_back(record->id);
    }
    // Abandoned later than the newest launch was made.
    const std::int64_t newest = manager.load(launches.back())->createdUnixMs;
    while (nowUnixMs() <= newest)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(manager.abandon(crashed.id).ok());

    // Created first, it is still the newest finished change; its remaining
    // backup stays.
    REQUIRE(manager.prune(2).ok());
    const auto remaining = manager.list();
    REQUIRE(remaining.ok());
    REQUIRE(remaining->size() == 2);
    CHECK((*remaining)[0].id == crashed.id);
    CHECK((*remaining)[1].id == launches[2]);
    CHECK(fs::exists(crashed.directory / crashed.entries[2].backupFile));

    REQUIRE(manager.prune(1).ok());
    CHECK(manager.list()->front().id == crashed.id);
}

TEST_CASE("Transaction states round-trip through their names", "[platform][transaction]") {
    for (const auto state :
         {TransactionState::Prepared, TransactionState::Applying, TransactionState::Committed,
          TransactionState::RolledBack, TransactionState::Recovered, TransactionState::Restored,
          TransactionState::Abandoned})
        CHECK(transactionStateFromString(toString(state)) == state);
    CHECK_FALSE(transactionStateFromString("committing").has_value());
    CHECK(std::string(toString(TransactionState::RolledBack)) == "rolled_back");
    CHECK(std::string(toString(TransactionState::Abandoned)) == "abandoned");
}
