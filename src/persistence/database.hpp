#pragma once

// The application's SQLite database (application-owned state only; game files
// are never stored here). See docs/persistence.md for schema and policies.
//
// Opening a database:
//   1. creates missing parent directories,
//   2. verifies the file is a SQLite database and passes PRAGMA quick_check,
//   3. refuses files written by a newer schema version (UnsupportedVersion,
//      the file is not modified),
//   4. copies an existing file to "<file>.bak-v<old>" before upgrading it,
//   5. enables WAL, foreign keys and a busy timeout, and applies pending
//      migrations, each in its own transaction.
//
// Thread safety: one Database is shared by the GUI thread and worker threads.
// It owns a single connection guarded by a recursive mutex. Every public call
// that touches the connection, on Database and on the repositories (which take
// Database&), holds the mutex for its duration, so calls are serialised and
// each sees a consistent state. Accessors of immutable data (file(),
// schemaVersion(), ...) do not lock.
// The mutex is recursive so repository calls compose inside transaction()
// bodies. Keep transaction bodies short: they block every other thread.
// The Database must outlive all repositories and threads that use it.

#include "core/result.hpp"
#include "persistence/sqlite.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace pmm::persistence {

struct Migration {
    int version = 0;  // 1, 2, 3, ... without gaps
    std::string name; // short description, stored in schema_migrations
    std::string sql;  // script executed inside the migration's transaction
    // Run with foreign key enforcement off, as required for table rebuilds
    // (with it on, dropping a parent table cascades to its child rows).
    // PRAGMA foreign_key_check must pass before the migration commits.
    bool foreignKeysOff = false;
};

// Built-in migrations in version order. A released migration is immutable:
// schema changes are made by appending a new migration.
const std::vector<Migration>& builtinMigrations();

struct DatabaseTestAccess;

class Database {
public:
    // Exclusive access to the connection for the lifetime of this object.
    class Access {
    public:
        Connection& connection() const noexcept { return *m_connection; }

    private:
        friend class Database;
        Access(std::recursive_mutex& mutex, Connection& connection)
            : m_lock(mutex), m_connection(&connection) {}

        std::unique_lock<std::recursive_mutex> m_lock;
        Connection* m_connection;
    };

    static constexpr int kBusyTimeoutMs = 5000;
    // File name inside the per-user application data directory.
    static constexpr const char* kDefaultFileName = "state.sqlite";

    // Opens or creates the database file. The application passes
    // platform::standardDirectories().appData / kDefaultFileName.
    static Result<std::unique_ptr<Database>> open(const std::filesystem::path& file);
    // Private, non-persistent database with the latest schema (tests, tools).
    static Result<std::unique_ptr<Database>> openInMemory();

    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Highest schema version this build knows about.
    static int latestSchemaVersion();
    // Schema version of the open database (equals latestSchemaVersion() after
    // a successful open unless test migrations were injected).
    int schemaVersion() const noexcept { return m_schemaVersion; }

    const std::filesystem::path& file() const noexcept { return m_file; } // empty if in memory
    bool isInMemory() const noexcept { return m_file.empty(); }
    // Backup written by this open before upgrading, empty if none was needed.
    const std::filesystem::path& upgradeBackupPath() const noexcept { return m_backupPath; }

    Access access();

    // Runs `body` in a transaction: BEGIN IMMEDIATE by default, or a SAVEPOINT
    // when already inside one. Commits if `body` succeeds, otherwise rolls back
    // and returns the body's error. An exception thrown by `body` rolls back
    // and propagates.
    Status transaction(const std::function<Status()>& body,
                       TransactionMode mode = TransactionMode::Immediate);

    // Current time in unix milliseconds for stored timestamps.
    std::int64_t now() const;

private:
    friend struct DatabaseTestAccess;

    Database(Connection connection, std::filesystem::path file, int schemaVersion,
             std::filesystem::path backupPath);

    static Result<std::unique_ptr<Database>> openImpl(const std::filesystem::path* file,
                                                      const std::vector<Migration>& migrations);

    mutable std::recursive_mutex m_mutex;
    Connection m_connection;
    std::filesystem::path m_file;
    std::filesystem::path m_backupPath;
    int m_schemaVersion = 0;
    std::function<std::int64_t()> m_clock; // null: system clock
};

} // namespace pmm::persistence
