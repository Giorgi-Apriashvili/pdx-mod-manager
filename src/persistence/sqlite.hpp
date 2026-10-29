#pragma once

// Thin RAII layer over the SQLite C API. Connection owns a sqlite3 handle,
// Statement owns a prepared statement, Transaction is a scope guard that rolls
// back unless committed. None of these types lock anything: callers serialise
// access through Database (see database.hpp).
//
// Text is always UTF-8. Bind indexes are 1-based and column indexes 0-based,
// matching SQLite. Bind failures are deferred and reported by the next step().

#include "core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace pmm::persistence {

// Maps a SQLite result code to an Error. Busy/locked map to Busy, a full disk
// to IoError, unique/primary-key violations to AlreadyExists, other constraint
// violations to ValidationFailed, everything else to DatabaseError. `detail`
// receives the SQLite message and `context`.
Error sqliteError(sqlite3* db, int rc, std::string_view context);

class Statement;

class Connection {
public:
    Connection() = default;
    ~Connection();
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Opens (and creates, if missing) a database file. The path is handed to
    // SQLite as UTF-8, which its Windows VFS converts to UTF-16, so non-ASCII
    // paths work everywhere. Parent directories must already exist.
    static Result<Connection> open(const std::filesystem::path& file);
    static Result<Connection> openInMemory();

    bool isOpen() const noexcept { return m_db != nullptr; }
    sqlite3* handle() const noexcept { return m_db; }

    // Runs one or more SQL statements that return no rows of interest.
    Status exec(std::string_view sql);
    Result<Statement> prepare(std::string_view sql);

    std::int64_t lastInsertRowId() const noexcept;
    int changes() const noexcept; // rows changed by the most recent statement
    bool inTransaction() const noexcept;

private:
    friend class Transaction;
    explicit Connection(sqlite3* db) noexcept : m_db(db) {}
    void close() noexcept;

    sqlite3* m_db = nullptr;
    int m_savepointCounter = 0;
};

class Statement {
public:
    Statement() = default;
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bindInt64(int index, std::int64_t value);
    Statement& bindDouble(int index, double value);
    Statement& bindText(int index, std::string_view utf8);
    Statement& bindBlob(int index, std::span<const std::uint8_t> bytes);
    Statement& bindNull(int index);

    // Generic bind: integers (bool as 0/1; uint64 is stored bit-for-bit as
    // int64), floating point, anything convertible to std::string_view,
    // byte spans/vectors, nullptr/nullopt as NULL, and std::optional<T>.
    template <class T> Statement& bind(int index, const T& value);

    // Binds the arguments to parameters 1..N in order.
    template <class... Args> Statement& bindAll(const Args&... args) {
        int index = 0;
        (bind(++index, args), ...);
        return *this;
    }

    // Advances the statement. true: a row is available; false: done.
    Result<bool> step();
    // Steps until done, discarding any rows.
    Status run();
    // Resets the statement and clears bindings so it can be executed again.
    void reset() noexcept;

    int columnCount() const noexcept;
    bool isNull(int column) const noexcept;
    std::int64_t columnInt64(int column) const noexcept;
    int columnInt(int column) const noexcept;
    bool columnBool(int column) const noexcept;
    double columnDouble(int column) const noexcept;
    std::string columnText(int column) const; // NULL reads as ""
    std::optional<std::string> columnOptionalText(int column) const;
    std::optional<std::int64_t> columnOptionalInt64(int column) const noexcept;
    std::vector<std::uint8_t> columnBlob(int column) const;

    sqlite3_stmt* handle() const noexcept { return m_stmt; }

private:
    friend class Connection;
    explicit Statement(sqlite3_stmt* stmt) noexcept : m_stmt(stmt) {}
    void recordBindError(int rc, int index);

    sqlite3_stmt* m_stmt = nullptr;
    std::optional<Error> m_bindError;
};

enum class TransactionMode {
    Deferred,  // BEGIN: takes locks lazily; for multi-statement reads
    Immediate, // BEGIN IMMEDIATE: takes the write lock up front (default)
};

// Scope guard for a transaction. If the connection is already inside a
// transaction, a uniquely named SAVEPOINT is used instead, so guards nest and
// an inner failure only rolls back the inner part. The destructor rolls back
// unless commit() succeeded.
class Transaction {
public:
    static Result<Transaction> begin(Connection& connection,
                                     TransactionMode mode = TransactionMode::Immediate);

    Transaction(Transaction&& other) noexcept;
    Transaction& operator=(Transaction&&) = delete;
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction();

    Status commit();
    Status rollback();
    bool isNested() const noexcept { return !m_savepoint.empty(); }
    bool isActive() const noexcept { return m_active; }

private:
    Transaction(Connection& connection, std::string savepoint) noexcept
        : m_connection(&connection), m_savepoint(std::move(savepoint)), m_active(true) {}

    Connection* m_connection = nullptr;
    std::string m_savepoint; // empty for the outermost transaction
    bool m_active = false;
};

namespace detail {
template <class T> struct IsOptional : std::false_type {};
template <class T> struct IsOptional<std::optional<T>> : std::true_type {};
template <class> inline constexpr bool kAlwaysFalse = false;
} // namespace detail

template <class T> Statement& Statement::bind(int index, const T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        return bindInt64(index, value ? 1 : 0);
    } else if constexpr (std::is_integral_v<T>) {
        static_assert(sizeof(T) <= sizeof(std::int64_t));
        return bindInt64(index, static_cast<std::int64_t>(value));
    } else if constexpr (std::is_enum_v<T>) {
        static_assert(detail::kAlwaysFalse<T>, "convert enums to their stored text first");
        return *this;
    } else if constexpr (std::is_floating_point_v<T>) {
        return bindDouble(index, static_cast<double>(value));
    } else if constexpr (std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, std::nullopt_t>) {
        return bindNull(index);
    } else if constexpr (detail::IsOptional<T>::value) {
        return value ? bind(index, *value) : bindNull(index);
    } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        return bindText(index, std::string_view(value));
    } else if constexpr (std::is_convertible_v<const T&, std::span<const std::uint8_t>>) {
        return bindBlob(index, std::span<const std::uint8_t>(value));
    } else {
        static_assert(detail::kAlwaysFalse<T>, "unsupported bind type");
        return *this;
    }
}

} // namespace pmm::persistence
