#include "persistence/database.hpp"
#include "persistence/internal/mapping.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/settings_repository.hpp"
#include "persistence/testing.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::persistence;
using namespace pmm::persistence::test;

namespace {

std::vector<Migration> withFakeV2(const std::string& sql = "CREATE TABLE future_v2 (x TEXT);",
                                  bool foreignKeysOff = false) {
    std::vector<Migration> extra;
    extra.push_back(Migration{Database::latestSchemaVersion() + 1, "fake future migration", sql,
                              foreignKeysOff});
    return extra;
}

// A future migration that rebuilds the playsets table (SQLite's 12-step
// procedure), which is only safe with foreign key enforcement off.
constexpr const char* kRebuildPlaysets = R"sql(
CREATE TABLE playsets_new (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    game_key   TEXT NOT NULL,
    name       TEXT NOT NULL,
    note       TEXT NOT NULL DEFAULT '',
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL,
    deleted_at INTEGER
);
INSERT INTO playsets_new (id, game_key, name, created_at, updated_at, deleted_at)
    SELECT id, game_key, name, created_at, updated_at, deleted_at FROM playsets;
DROP TABLE playsets;
ALTER TABLE playsets_new RENAME TO playsets;
CREATE INDEX playsets_by_game ON playsets (game_key, deleted_at);
)sql";

fs::path backupPathFor(const fs::path& file, int version) {
    fs::path backup = file;
    backup += ".bak-v" + std::to_string(version);
    return backup;
}

std::set<std::string> directoryListing(const fs::path& dir) {
    std::set<std::string> names;
    for (const auto& item : fs::directory_iterator(dir))
        names.insert(internal::pathToText(item.path().filename()));
    return names;
}

bool tableExists(Database& db, const std::string& name) {
    return queryInt(db, "SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = '" +
                            name + "'") == 1;
}

} // namespace

TEST_CASE("Opening creates the database and parent folders in a Unicode path",
          "[persistence][database]") {
    TempDir temp;
    const fs::path file = temp.dbFile();
    REQUIRE_FALSE(fs::exists(file.parent_path()));
    auto db = require(Database::open(file));
    CHECK(fs::exists(file));
    CHECK(db->file() == file);
    CHECK_FALSE(db->isInMemory());
    CHECK(db->schemaVersion() == Database::latestSchemaVersion());
    CHECK(db->upgradeBackupPath().empty());
    CHECK(queryText(*db, "PRAGMA journal_mode") == "wal");
    CHECK(queryInt(*db, "PRAGMA foreign_keys") == 1);
    CHECK(queryInt(*db, "PRAGMA busy_timeout") == Database::kBusyTimeoutMs);
}

#if defined(_WIN32)
TEST_CASE("A database file name that is not valid UTF-16 is refused instead of throwing",
          "[persistence][database]") {
    // A lossy conversion would make SQLite open (and create) a different file.
    TempDir temp;
    const fs::path file = temp.path() / loneSurrogatePath(L"state", L".sqlite");
    Result<std::unique_ptr<Database>> opened = makeError(ErrorCode::Internal, "not run");
    REQUIRE_NOTHROW(opened = Database::open(file));
    REQUIRE_FALSE(opened.ok());
    CHECK(opened.error().code == ErrorCode::InvalidArgument);
    CHECK(opened.error().detail.find("state\xEF\xBF\xBD.sqlite") != std::string::npos);
    CHECK(directoryListing(temp.path()).empty());

    Result<Connection> connection = makeError(ErrorCode::Internal, "not run");
    REQUIRE_NOTHROW(connection = Connection::open(file));
    REQUIRE_FALSE(connection.ok());
    CHECK(connection.error().code == ErrorCode::InvalidArgument);
}
#endif

TEST_CASE("A new database is migrated from empty to the latest schema", "[persistence][database]") {
    auto db = openMemory();
    const int latest = Database::latestSchemaVersion();
    REQUIRE(latest >= 1);
    CHECK(db->schemaVersion() == latest);
    CHECK(queryInt(*db, "SELECT count(*) FROM schema_migrations") == latest);
    CHECK(queryInt(*db, "SELECT MAX(version) FROM schema_migrations") == latest);
    CHECK(queryInt(*db, "SELECT min(applied_at) FROM schema_migrations") > 0);
    CHECK(queryInt(*db, "PRAGMA user_version") == latest);
    for (const char* table :
         {"settings", "game_installations", "installed_mods", "playsets", "playset_entries",
          "playset_history", "acquisition_jobs", "launch_history"})
        CHECK(tableExists(*db, table));
}

TEST_CASE("Reopening a database preserves its data without a backup", "[persistence][database]") {
    TempDir temp;
    const fs::path file = temp.dbFile();
    std::int64_t playsetId = 0;
    {
        auto db = require(Database::open(file));
        SettingsRepository settings(*db);
        requireOk(settings.set("ui.theme", "dark"));
        PlaysetRepository playsets(*db);
        Playset p;
        p.game = "stellaris";
        p.name = "Ünïcødé playset";
        p.entries = workshopEntries(3);
        playsetId = require(playsets.create(p));
    }
    auto db = require(Database::open(file));
    CHECK(db->upgradeBackupPath().empty());
    CHECK(require(SettingsRepository(*db).get("ui.theme")) == std::optional<std::string>("dark"));
    auto playset = require(PlaysetRepository(*db).get(playsetId));
    CHECK(playset.name == "Ünïcødé playset");
    CHECK(playset.entries == workshopEntries(3));
    CHECK(directoryListing(file.parent_path())
              .count(internal::pathToText(backupPathFor(file, 1).filename())) == 0);
}

TEST_CASE("A database written by a newer version is refused and left untouched",
          "[persistence][database]") {
    TempDir temp;
    const fs::path file = temp.dbFile();
    {
        auto db = require(testing::openWithExtraMigrations(file, withFakeV2()));
        REQUIRE(db->schemaVersion() == Database::latestSchemaVersion() + 1);
        requireOk(SettingsRepository(*db).set("marker", "from the future"));
    }
    const std::string before = readBytes(file);
    const auto listingBefore = directoryListing(file.parent_path());
    REQUIRE_FALSE(before.empty());

    auto refused = Database::open(file);
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().code == ErrorCode::UnsupportedVersion);
    CHECK(refused.error().message.find("newer version") != std::string::npos);
    CHECK(refused.error().message.find("not modified") != std::string::npos);

    CHECK(readBytes(file) == before);
    CHECK(directoryListing(file.parent_path()) == listingBefore);
}

TEST_CASE("Upgrading an existing database writes a .bak copy first", "[persistence][database]") {
    TempDir temp;
    const fs::path file = temp.dbFile();
    const int oldVersion = Database::latestSchemaVersion();
    {
        auto db = require(Database::open(file));
        requireOk(SettingsRepository(*db).set("marker", "before upgrade"));
    }

    auto upgraded = require(testing::openWithExtraMigrations(file, withFakeV2()));
    const fs::path expectedBackup = backupPathFor(file, oldVersion);
    CHECK(upgraded->schemaVersion() == oldVersion + 1);
    CHECK(upgraded->upgradeBackupPath() == expectedBackup);
    CHECK(tableExists(*upgraded, "future_v2"));
    CHECK(require(SettingsRepository(*upgraded).get("marker")) ==
          std::optional<std::string>("before upgrade"));
    upgraded.reset();

    REQUIRE(fs::exists(expectedBackup));
    auto backup = require(Database::open(expectedBackup));
    CHECK(backup->schemaVersion() == oldVersion);
    CHECK_FALSE(tableExists(*backup, "future_v2"));
    CHECK(require(SettingsRepository(*backup).get("marker")) ==
          std::optional<std::string>("before upgrade"));
}

TEST_CASE("A failing migration leaves the database at its previous version",
          "[persistence][database]") {
    TempDir temp;
    const fs::path file = temp.dbFile();
    const int oldVersion = Database::latestSchemaVersion();
    {
        auto db = require(Database::open(file));
        requireOk(SettingsRepository(*db).set("marker", "kept"));
    }

    auto failed = testing::openWithExtraMigrations(
        file, withFakeV2("CREATE TABLE half_done (x TEXT); INSERT INTO missing_table VALUES (1);"));
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().code == ErrorCode::MigrationFailed);
    CHECK(failed.error().message.find("left at version " + std::to_string(oldVersion)) !=
          std::string::npos);
    CHECK(fs::exists(backupPathFor(file, oldVersion)));

    auto db = require(Database::open(file));
    CHECK(db->schemaVersion() == oldVersion);
    CHECK_FALSE(tableExists(*db, "half_done"));
    CHECK(require(SettingsRepository(*db).get("marker")) == std::optional<std::string>("kept"));
}

TEST_CASE("Table-rebuild migrations run with foreign keys off and keep child rows",
          "[persistence][database]") {
    TempDir temp;
    const fs::path file = temp.dbFile();
    std::int64_t id = 0;
    {
        auto db = require(Database::open(file));
        Playset p;
        p.game = "stellaris";
        p.name = "Survives rebuild";
        p.entries = workshopEntries(4);
        id = require(PlaysetRepository(*db).create(p));
    }
    auto db = require(testing::openWithExtraMigrations(file, withFakeV2(kRebuildPlaysets, true)));
    CHECK(db->schemaVersion() == Database::latestSchemaVersion() + 1);
    CHECK(queryInt(*db, "PRAGMA foreign_keys") == 1);
    CHECK(queryInt(*db, "SELECT count(*) FROM playsets WHERE note = ''") == 1);
    CHECK(require(PlaysetRepository(*db).get(id)).entries == workshopEntries(4));
    // Enforcement is back on: orphan entries are rejected again.
    auto access = db->access();
    auto orphan = access.connection().exec(
        "INSERT INTO playset_entries (playset_id, position, source, provider_id, enabled) "
        "VALUES (999, 0, 'local', 'x', 1)");
    REQUIRE_FALSE(orphan.ok());
    CHECK(orphan.error().code == ErrorCode::ValidationFailed);
}

TEST_CASE("Foreign key violations abort a migration that ran with them off",
          "[persistence][database]") {
    TempDir temp;
    const fs::path file = temp.dbFile();
    const int oldVersion = Database::latestSchemaVersion();
    {
        require(Database::open(file));
    }
    auto failed = testing::openWithExtraMigrations(
        file, withFakeV2("INSERT INTO playset_entries (playset_id, position, source, "
                         "provider_id, enabled) VALUES (999, 0, 'local', 'x', 1);",
                         true));
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().code == ErrorCode::MigrationFailed);
    auto db = require(Database::open(file));
    CHECK(db->schemaVersion() == oldVersion);
    CHECK(queryInt(*db, "SELECT count(*) FROM playset_entries") == 0);
}

TEST_CASE("Migration lists must be contiguous", "[persistence][database]") {
    std::vector<Migration> broken = builtinMigrations();
    broken.push_back(Migration{Database::latestSchemaVersion() + 2, "gap", "SELECT 1;", false});
    auto db = testing::openInMemoryWithMigrations(broken);
    REQUIRE_FALSE(db.ok());
    CHECK(db.error().code == ErrorCode::Internal);
}

TEST_CASE("A file that is not a database is reported with a recovery hint",
          "[persistence][database]") {
    TempDir temp;
    const fs::path file = temp.path() / "garbage.sqlite";
    {
        std::ofstream out(file, std::ios::binary);
        for (int i = 0; i < 64; ++i)
            out << "this is definitely not a SQLite database file\n";
    }
    const std::string before = readBytes(file);
    auto db = Database::open(file);
    REQUIRE_FALSE(db.ok());
    CHECK(db.error().code == ErrorCode::DatabaseError);
    CHECK(db.error().message.find("backup") != std::string::npos);
    CHECK(readBytes(file) == before);
}

TEST_CASE("A damaged database fails the integrity check on open", "[persistence][database]") {
    TempDir temp;
    const fs::path file = temp.dbFile();
    {
        auto db = require(Database::open(file));
        SettingsRepository settings(*db);
        const std::string filler(400, 'x');
        requireOk(db->transaction([&]() -> Status {
            for (int i = 0; i < 300; ++i)
                PMM_TRY(settings.set("key" + std::to_string(i), filler));
            return {};
        }));
    }
    std::string bytes = readBytes(file);
    const std::size_t pageSize = 4096;
    REQUIRE(bytes.size() > pageSize * 12);
    // Overwrite a page in the middle of the settings table with garbage.
    const std::size_t offset = (bytes.size() / pageSize / 2) * pageSize;
    for (std::size_t i = 0; i < pageSize; ++i)
        bytes[offset + i] = static_cast<char>(0xA5);
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    auto db = Database::open(file);
    REQUIRE_FALSE(db.ok());
    CHECK(db.error().code == ErrorCode::DatabaseError);
    CHECK(db.error().message.find("damaged") != std::string::npos);
    CHECK(db.error().message.find(".bak-v") != std::string::npos);
}

TEST_CASE("A SQLite file from another application is refused", "[persistence][database]") {
    TempDir temp;
    const fs::path file = temp.path() / "other.sqlite";
    {
        auto connection = require(Connection::open(file));
        requireOk(connection.exec("CREATE TABLE foreign_data (x INTEGER)"));
    }
    auto db = Database::open(file);
    REQUIRE_FALSE(db.ok());
    CHECK(db.error().code == ErrorCode::DatabaseError);
    CHECK(db.error().message.find("does not belong") != std::string::npos);
}

TEST_CASE("transaction() commits on success and rolls back on failure", "[persistence][database]") {
    auto db = openMemory();
    SettingsRepository settings(*db);
    PlaysetRepository playsets(*db);

    auto failed = db->transaction([&]() -> Status {
        PMM_TRY(settings.set("a", "1"));
        Playset p;
        p.game = "stellaris";
        p.name = "Inside failed transaction";
        PMM_TRY(playsets.create(p)); // nested transaction becomes a savepoint
        return makeError(ErrorCode::Cancelled, "injected failure");
    });
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().code == ErrorCode::Cancelled);
    CHECK_FALSE(require(settings.get("a")).has_value());
    CHECK(require(playsets.listForGame("stellaris")).empty());

    requireOk(db->transaction([&]() -> Status { return settings.set("a", "2"); }));
    CHECK(require(settings.get("a")) == std::optional<std::string>("2"));
}

TEST_CASE("The unfinished-job index matches the model's non-terminal states",
          "[persistence][database]") {
    std::set<std::string> nonTerminal;
    for (const char* name :
         {"pending", "subscribing", "subscribed", "download_requested", "downloading", "installed",
          "failed", "unavailable", "needs_user_action", "cancelled"}) {
        const auto state = acquisitionStateFromString(name);
        REQUIRE(state.has_value());
        REQUIRE(std::string(toString(*state)) == name);
        if (!isTerminal(*state))
            nonTerminal.insert(name);
    }
    std::set<std::string> listed;
    const std::string sqlList = internal::kUnfinishedJobStatesSql;
    for (std::size_t pos = sqlList.find('\''); pos != std::string::npos;) {
        const std::size_t end = sqlList.find('\'', pos + 1);
        listed.insert(sqlList.substr(pos + 1, end - pos - 1));
        pos = sqlList.find('\'', end + 1);
    }
    CHECK(listed == nonTerminal);

    auto db = openMemory();
    const std::string indexSql = queryText(
        *db, "SELECT sql FROM sqlite_master WHERE name = 'acquisition_jobs_one_unfinished'");
    CHECK(indexSql.find(std::string("state IN ") + internal::kUnfinishedJobStatesSql) !=
          std::string::npos);
}
