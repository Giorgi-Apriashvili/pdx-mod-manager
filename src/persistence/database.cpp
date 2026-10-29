#include "persistence/database.hpp"

#include "core/log.hpp"
#include "persistence/internal/mapping.hpp"
#include "persistence/testing.hpp"

#include <sqlite3.h>

#include <system_error>
#include <utility>

namespace pmm::persistence {

namespace fs = std::filesystem;
using internal::pathToText;

namespace {

constexpr const char* kLogComponent = "persistence";

constexpr const char* kBootstrapSql = R"sql(
CREATE TABLE IF NOT EXISTS schema_migrations (
    version    INTEGER PRIMARY KEY,
    name       TEXT NOT NULL,
    applied_at INTEGER NOT NULL
);
)sql";

Error withPath(Error error, const fs::path* file) {
    if (file != nullptr) {
        if (!error.detail.empty())
            error.detail += "; ";
        error.detail += "database: " + pathToText(*file);
    }
    return error;
}

Result<std::int64_t> queryInt(Connection& connection, std::string_view sql) {
    auto stmt = connection.prepare(sql);
    if (!stmt)
        return std::move(stmt).error();
    auto row = stmt->step();
    if (!row)
        return std::move(row).error();
    return *row ? stmt->columnInt64(0) : 0;
}

Status validateMigrations(const std::vector<Migration>& migrations) {
    for (std::size_t i = 0; i < migrations.size(); ++i) {
        if (migrations[i].version != static_cast<int>(i) + 1 || migrations[i].sql.empty())
            return makeError(ErrorCode::Internal, "The database migration list is invalid.",
                             "migration at index " + std::to_string(i) + " has version " +
                                 std::to_string(migrations[i].version));
    }
    return {};
}

// Reads the schema version without modifying the file. A database without a
// schema_migrations table is only accepted if it is completely empty.
Result<int> readSchemaVersion(Connection& connection) {
    auto hasTable = queryInt(connection, "SELECT count(*) FROM sqlite_master WHERE type = 'table' "
                                         "AND name = 'schema_migrations'");
    if (!hasTable)
        return std::move(hasTable).error();
    if (*hasTable == 0) {
        auto objects = queryInt(connection, "SELECT count(*) FROM sqlite_master "
                                            "WHERE name NOT LIKE 'sqlite\\_%' ESCAPE '\\'");
        if (!objects)
            return std::move(objects).error();
        if (*objects > 0)
            return makeError(ErrorCode::DatabaseError,
                             "The file is a SQLite database that does not belong to Paradox Mod "
                             "Manager. Choose a different location, or move the file aside.",
                             "no schema_migrations table but " + std::to_string(*objects) +
                                 " schema objects");
        return 0;
    }
    auto version = queryInt(connection, "SELECT COALESCE(MAX(version), 0) FROM schema_migrations");
    if (!version)
        return std::move(version).error();
    return static_cast<int>(*version);
}

Status checkIntegrity(Connection& connection) {
    auto stmt = connection.prepare("PRAGMA quick_check(20)");
    if (!stmt)
        return std::move(stmt).error();
    std::string problems;
    for (;;) {
        auto row = stmt->step();
        if (!row)
            return std::move(row).error();
        if (!*row)
            break;
        const std::string line = stmt->columnText(0);
        if (line == "ok")
            continue;
        if (!problems.empty())
            problems += "; ";
        problems += line;
    }
    if (problems.empty())
        return {};
    return makeError(ErrorCode::DatabaseError,
                     "The application database is damaged (integrity check failed). Close the "
                     "application, then restore the most recent backup (a file named like the "
                     "database with a .bak-v<N> suffix) or move the database file aside to start "
                     "with an empty one. Playsets can be recreated from share codes.",
                     "quick_check: " + problems);
}

// Copies the live database (including WAL content) to `target` through the
// SQLite online backup API, via a temporary file so a failed copy never
// replaces an older backup.
Status writeBackup(Connection& source, const fs::path& target) {
    fs::path temp = target;
    temp += ".tmp";
    std::error_code ec;
    fs::remove(temp, ec);
    {
        auto dest = Connection::open(temp);
        if (!dest)
            return std::move(dest).error();
        sqlite3_backup* backup =
            sqlite3_backup_init(dest->handle(), "main", source.handle(), "main");
        if (backup == nullptr)
            return sqliteError(dest->handle(), sqlite3_extended_errcode(dest->handle()),
                               "starting database backup");
        const int stepRc = sqlite3_backup_step(backup, -1);
        const int finishRc = sqlite3_backup_finish(backup);
        if (stepRc != SQLITE_DONE)
            return sqliteError(dest->handle(), stepRc, "copying database backup");
        if (finishRc != SQLITE_OK)
            return sqliteError(dest->handle(), finishRc, "finishing database backup");
    }
    fs::remove(target, ec);
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        return makeError(ErrorCode::IoError, "The database backup could not be saved.",
                         ec.message());
    }
    return {};
}

Status checkForeignKeys(Connection& connection) {
    auto stmt = connection.prepare("PRAGMA foreign_key_check");
    if (!stmt)
        return std::move(stmt).error();
    auto row = stmt->step();
    if (!row)
        return std::move(row).error();
    if (*row)
        return makeError(ErrorCode::ValidationFailed,
                         "The upgrade would leave records that refer to missing data.",
                         "foreign_key_check: table " + stmt->columnText(0) + " rowid " +
                             std::to_string(stmt->columnInt64(1)) + " -> " + stmt->columnText(2));
    return {};
}

Status applyMigrationInTransaction(Connection& connection, const Migration& migration,
                                   std::int64_t now, int& current) {
    auto tx = Transaction::begin(connection, TransactionMode::Immediate);
    if (!tx)
        return std::move(tx).error();
    // Another process may have migrated while we waited for the write lock.
    auto version = readSchemaVersion(connection);
    if (!version)
        return std::move(version).error();
    if (*version >= migration.version) {
        current = *version;
        return tx->commit();
    }
    PMM_TRY(connection.exec(kBootstrapSql));
    PMM_TRY(connection.exec(migration.sql));
    if (migration.foreignKeysOff)
        PMM_TRY(checkForeignKeys(connection));
    auto insert = connection.prepare(
        "INSERT INTO schema_migrations (version, name, applied_at) VALUES (?, ?, ?)");
    if (!insert)
        return std::move(insert).error();
    PMM_TRY(insert->bindAll(migration.version, migration.name, now).run());
    PMM_TRY(connection.exec("PRAGMA user_version = " + std::to_string(migration.version)));
    PMM_TRY(tx->commit());
    current = migration.version;
    return {};
}

Status applyMigration(Connection& connection, const Migration& migration, std::int64_t now,
                      int& current) {
    if (!migration.foreignKeysOff)
        return applyMigrationInTransaction(connection, migration, now, current);
    // The pragma is a no-op inside a transaction, so it brackets the transaction.
    PMM_TRY(connection.exec("PRAGMA foreign_keys = OFF"));
    Status result = applyMigrationInTransaction(connection, migration, now, current);
    Status restored = connection.exec("PRAGMA foreign_keys = ON");
    if (!result.ok())
        return result;
    return restored;
}

} // namespace

Database::Database(Connection connection, fs::path file, int schemaVersion, fs::path backupPath)
    : m_connection(std::move(connection)), m_file(std::move(file)),
      m_backupPath(std::move(backupPath)), m_schemaVersion(schemaVersion) {}

Database::~Database() = default;

int Database::latestSchemaVersion() {
    const auto& migrations = builtinMigrations();
    return migrations.empty() ? 0 : migrations.back().version;
}

Result<std::unique_ptr<Database>> Database::open(const fs::path& file) {
    return openImpl(&file, builtinMigrations());
}

Result<std::unique_ptr<Database>> Database::openInMemory() {
    return openImpl(nullptr, builtinMigrations());
}

Result<std::unique_ptr<Database>> Database::openImpl(const fs::path* file,
                                                     const std::vector<Migration>& migrations) {
    PMM_TRY(validateMigrations(migrations));
    const int latest = migrations.empty() ? 0 : migrations.back().version;

    Connection connection;
    bool existing = false;
    if (file != nullptr) {
        if (file->empty())
            return makeError(ErrorCode::InvalidArgument, "No database file name was given.");
        std::error_code ec;
        const fs::path parent = file->parent_path();
        if (!parent.empty()) {
            fs::create_directories(parent, ec);
            if (ec)
                return makeError(ErrorCode::IoError,
                                 "The application data folder could not be created. Check that "
                                 "the location is writable.",
                                 pathToText(parent) + ": " + ec.message());
        }
        const auto size = fs::file_size(*file, ec);
        existing = !ec && size > 0;
        auto opened = Connection::open(*file);
        if (!opened)
            return withPath(std::move(opened).error(), file);
        connection = std::move(*opened);
    } else {
        auto opened = Connection::openInMemory();
        if (!opened)
            return std::move(opened).error();
        connection = std::move(*opened);
    }

    // Connection-level settings only; nothing below writes to the file until
    // the version check has passed.
    sqlite3_busy_timeout(connection.handle(), kBusyTimeoutMs);
    if (auto st = connection.exec("PRAGMA foreign_keys = ON"); !st)
        return withPath(std::move(st).error(), file);
    if (auto fk = queryInt(connection, "PRAGMA foreign_keys"); !fk || *fk != 1)
        return makeError(ErrorCode::Internal, "SQLite foreign key support is unavailable.");

    if (auto st = checkIntegrity(connection); !st)
        return withPath(std::move(st).error(), file);
    auto version = readSchemaVersion(connection);
    if (!version)
        return withPath(std::move(version).error(), file);
    if (*version > latest)
        return withPath(
            makeError(ErrorCode::UnsupportedVersion,
                      "The application database was created by a newer version of Paradox Mod "
                      "Manager (schema version " +
                          std::to_string(*version) + "; this version supports up to " +
                          std::to_string(latest) +
                          "). Update the application, or move the database file aside to start "
                          "with an empty one. The file was not modified."),
            file);

    fs::path backupPath;
    if (file != nullptr && existing && *version > 0 && *version < latest) {
        backupPath = *file;
        backupPath += ".bak-v" + std::to_string(*version);
        if (auto st = writeBackup(connection, backupPath); !st) {
            Error error = std::move(st).error();
            error.message = "The application database needs an upgrade, but a backup could not "
                            "be created first, so it was left unchanged. Check free disk space "
                            "and permissions of the application data folder. (" +
                            error.message + ")";
            return withPath(std::move(error), file);
        }
        logInfo(kLogComponent, "database backed up before upgrade",
                {{"from_version", std::to_string(*version)}, {"backup", pathToText(backupPath)}});
    }

    if (file != nullptr) {
        auto stmt = connection.prepare("PRAGMA journal_mode = WAL");
        if (!stmt)
            return withPath(std::move(stmt).error(), file);
        auto row = stmt->step();
        if (!row)
            return withPath(std::move(row).error(), file);
        const std::string mode = *row ? stmt->columnText(0) : std::string();
        if (mode != "wal")
            logWarning(kLogComponent, "WAL journal mode unavailable; using rollback journal",
                       {{"mode", mode}});
    }

    int current = *version;
    for (const auto& migration : migrations) {
        if (migration.version <= current)
            continue;
        const int from = current;
        if (auto st = applyMigration(connection, migration, nowUnixMs(), current); !st) {
            const Error& cause = st.error();
            std::string message = "The application database could not be upgraded from schema "
                                  "version " +
                                  std::to_string(from) + " to " +
                                  std::to_string(migration.version) + ". It was left at version " +
                                  std::to_string(from) + ".";
            if (!backupPath.empty())
                message += " A backup of the previous state was saved next to it with the suffix "
                           ".bak-v" +
                           std::to_string(*version) + ".";
            message += " (" + cause.message + ")";
            return withPath(makeError(ErrorCode::MigrationFailed, std::move(message),
                                      "migration " + std::to_string(migration.version) + " '" +
                                          migration.name + "': " + cause.detail),
                            file);
        }
        logInfo(kLogComponent, "database migration applied",
                {{"version", std::to_string(migration.version)}, {"name", migration.name}});
    }
    if (current > latest)
        return withPath(makeError(ErrorCode::UnsupportedVersion,
                                  "The application database was upgraded by a newer version of "
                                  "Paradox Mod Manager while opening it. Update the application."),
                        file);

    return std::unique_ptr<Database>(new Database(std::move(connection),
                                                  file != nullptr ? *file : fs::path(), current,
                                                  std::move(backupPath)));
}

Database::Access Database::access() {
    return Access(m_mutex, m_connection);
}

Status Database::transaction(const std::function<Status()>& body, TransactionMode mode) {
    auto guard = access();
    auto tx = Transaction::begin(guard.connection(), mode);
    if (!tx)
        return std::move(tx).error();
    Status status = body();
    if (!status.ok()) {
        (void)tx->rollback();
        return status;
    }
    return tx->commit();
}

std::int64_t Database::now() const {
    return m_clock ? m_clock() : nowUnixMs();
}

// --- Test hooks ----------------------------------------------------------------

struct DatabaseTestAccess {
    static Result<std::unique_ptr<Database>> open(const fs::path* file,
                                                  const std::vector<Migration>& migrations) {
        return Database::openImpl(file, migrations);
    }
    static void setClock(Database& db, std::function<std::int64_t()> clock) {
        auto guard = db.access();
        db.m_clock = std::move(clock);
    }
};

namespace testing {

Result<std::unique_ptr<Database>> openWithMigrations(const fs::path& file,
                                                     const std::vector<Migration>& migrations) {
    return DatabaseTestAccess::open(&file, migrations);
}

Result<std::unique_ptr<Database>> openWithExtraMigrations(const fs::path& file,
                                                          const std::vector<Migration>& extra) {
    std::vector<Migration> all = builtinMigrations();
    all.insert(all.end(), extra.begin(), extra.end());
    return DatabaseTestAccess::open(&file, all);
}

Result<std::unique_ptr<Database>>
openInMemoryWithMigrations(const std::vector<Migration>& migrations) {
    return DatabaseTestAccess::open(nullptr, migrations);
}

void setClock(Database& db, std::function<std::int64_t()> clock) {
    DatabaseTestAccess::setClock(db, std::move(clock));
}

} // namespace testing

} // namespace pmm::persistence
