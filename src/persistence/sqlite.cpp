#include "persistence/sqlite.hpp"

#include "persistence/internal/mapping.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <string>

namespace pmm::persistence {

namespace {

// SQLITE_TRANSIENT without the C-style cast in the macro.
sqlite3_destructor_type transientDestructor() noexcept {
    // SQLITE_TRANSIENT: SQLite's documented sentinel, not a real address.
    return reinterpret_cast<sqlite3_destructor_type>( // NOLINT(performance-no-int-to-ptr)
        static_cast<std::intptr_t>(-1));
}

std::string abbreviate(std::string_view sql) {
    constexpr std::size_t kMax = 160;
    std::string out;
    out.reserve(std::min(sql.size(), kMax) + 3);
    bool lastSpace = false;
    for (char c : sql) {
        const bool space = std::isspace(static_cast<unsigned char>(c)) != 0;
        if (space && lastSpace)
            continue;
        out += space ? ' ' : c;
        lastSpace = space;
        if (out.size() >= kMax) {
            out += "...";
            break;
        }
    }
    return out;
}

std::string_view sqlText(sqlite3_stmt* stmt) noexcept {
    const char* sql = stmt != nullptr ? sqlite3_sql(stmt) : nullptr;
    return sql != nullptr ? std::string_view(sql) : std::string_view();
}

} // namespace

Error sqliteError(sqlite3* db, int rc, std::string_view context) {
    std::string detail = "sqlite error " + std::to_string(rc) + " (" + sqlite3_errstr(rc) + ")";
    if (db != nullptr) {
        const char* msg = sqlite3_errmsg(db);
        if (msg != nullptr && *msg != '\0' && std::string_view(msg) != sqlite3_errstr(rc)) {
            detail += ": ";
            detail += msg;
        }
    }
    if (!context.empty()) {
        detail += " [";
        detail += context;
        detail += "]";
    }

    switch (rc & 0xff) {
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        return makeError(ErrorCode::Busy,
                         "The application database is busy, possibly because another Paradox Mod "
                         "Manager window is using it. Try again in a moment.",
                         std::move(detail));
    case SQLITE_FULL:
        return makeError(ErrorCode::IoError,
                         "The disk holding the application database is full. Free some space and "
                         "try again.",
                         std::move(detail));
    case SQLITE_READONLY:
    case SQLITE_PERM:
    case SQLITE_AUTH:
        return makeError(ErrorCode::PermissionDenied,
                         "The application database cannot be written. Check the permissions of "
                         "the application data folder.",
                         std::move(detail));
    case SQLITE_CANTOPEN:
        return makeError(ErrorCode::IoError,
                         "The application database file could not be opened. Check that the "
                         "application data folder exists and is writable.",
                         std::move(detail));
    case SQLITE_IOERR:
        return makeError(ErrorCode::IoError,
                         "A disk error occurred while accessing the application database.",
                         std::move(detail));
    case SQLITE_CORRUPT:
    case SQLITE_NOTADB:
        return makeError(ErrorCode::DatabaseError,
                         "The application database is damaged or is not a Paradox Mod Manager "
                         "database. Close the application, then restore a backup (a file named "
                         "like the database with a .bak-v<N> suffix) or move the file aside to "
                         "start with an empty database.",
                         std::move(detail));
    case SQLITE_CONSTRAINT:
        if (rc == SQLITE_CONSTRAINT_UNIQUE || rc == SQLITE_CONSTRAINT_PRIMARYKEY)
            return makeError(ErrorCode::AlreadyExists,
                             "The change conflicts with an existing record.", std::move(detail));
        if (rc == SQLITE_CONSTRAINT_FOREIGNKEY)
            return makeError(ErrorCode::ValidationFailed,
                             "The change refers to a record that does not exist.",
                             std::move(detail));
        return makeError(ErrorCode::ValidationFailed,
                         "The change was rejected because it violates a data rule.",
                         std::move(detail));
    case SQLITE_NOMEM:
        return makeError(ErrorCode::Internal, "The application ran out of memory.",
                         std::move(detail));
    default:
        return makeError(ErrorCode::DatabaseError,
                         "An unexpected error occurred in the application database.",
                         std::move(detail));
    }
}

// --- Connection ----------------------------------------------------------------

Connection::~Connection() {
    close();
}

Connection::Connection(Connection&& other) noexcept
    : m_db(std::exchange(other.m_db, nullptr)),
      m_savepointCounter(std::exchange(other.m_savepointCounter, 0)) {}

Connection& Connection::operator=(Connection&& other) noexcept {
    if (this != &other) {
        close();
        m_db = std::exchange(other.m_db, nullptr);
        m_savepointCounter = std::exchange(other.m_savepointCounter, 0);
    }
    return *this;
}

void Connection::close() noexcept {
    if (m_db != nullptr) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

namespace {
Result<sqlite3*> openHandle(const std::string& utf8Name, std::string_view what) {
    sqlite3* db = nullptr;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_EXRESCODE;
    const int rc = sqlite3_open_v2(utf8Name.c_str(), &db, flags, nullptr);
    if (rc != SQLITE_OK) {
        Error error = sqliteError(db, rc, "opening " + std::string(what));
        sqlite3_close_v2(db);
        return error;
    }
    sqlite3_extended_result_codes(db, 1);
    return db;
}
} // namespace

Result<Connection> Connection::open(const std::filesystem::path& file) {
    // SQLite takes the name as UTF-8; a lossy form would open a different file.
    PMM_TRY(internal::checkStorablePath(file, "The database file"));
    const std::string utf8 = internal::pathToText(file);
    if (utf8.empty())
        return makeError(ErrorCode::InvalidArgument, "No database file name was given.");
    auto handle = openHandle(utf8, utf8);
    if (!handle)
        return std::move(handle).error();
    return Connection(*handle);
}

Result<Connection> Connection::openInMemory() {
    auto handle = openHandle(":memory:", "in-memory database");
    if (!handle)
        return std::move(handle).error();
    return Connection(*handle);
}

Status Connection::exec(std::string_view sql) {
    if (m_db == nullptr)
        return makeError(ErrorCode::Internal, "The database connection is closed.");
    const std::string text(sql);
    const int rc = sqlite3_exec(m_db, text.c_str(), nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK)
        return sqliteError(m_db, rc, abbreviate(sql));
    return {};
}

Result<Statement> Connection::prepare(std::string_view sql) {
    if (m_db == nullptr)
        return makeError(ErrorCode::Internal, "The database connection is closed.");
    if (sql.size() > static_cast<std::size_t>(INT_MAX))
        return makeError(ErrorCode::LimitExceeded, "SQL statement is too long.");
    sqlite3_stmt* stmt = nullptr;
    const char* tail = nullptr;
    const int rc = sqlite3_prepare_v2(m_db, sql.data(), static_cast<int>(sql.size()), &stmt, &tail);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return sqliteError(m_db, rc, abbreviate(sql));
    }
    if (stmt == nullptr)
        return makeError(ErrorCode::Internal, "Empty SQL statement.", abbreviate(sql));
    if (tail != nullptr) {
        const std::string_view rest(tail, static_cast<std::size_t>(sql.data() + sql.size() - tail));
        for (char c : rest) {
            if (std::isspace(static_cast<unsigned char>(c)) == 0 && c != ';') {
                sqlite3_finalize(stmt);
                return makeError(ErrorCode::Internal,
                                 "prepare() accepts a single SQL statement; use exec() for "
                                 "scripts.",
                                 abbreviate(sql));
            }
        }
    }
    return Statement(stmt);
}

std::int64_t Connection::lastInsertRowId() const noexcept {
    return m_db != nullptr ? sqlite3_last_insert_rowid(m_db) : 0;
}

int Connection::changes() const noexcept {
    return m_db != nullptr ? sqlite3_changes(m_db) : 0;
}

bool Connection::inTransaction() const noexcept {
    return m_db != nullptr && sqlite3_get_autocommit(m_db) == 0;
}

// --- Statement -----------------------------------------------------------------

Statement::~Statement() {
    if (m_stmt != nullptr)
        sqlite3_finalize(m_stmt);
}

Statement::Statement(Statement&& other) noexcept
    : m_stmt(std::exchange(other.m_stmt, nullptr)), m_bindError(std::move(other.m_bindError)) {
    other.m_bindError.reset();
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (m_stmt != nullptr)
            sqlite3_finalize(m_stmt);
        m_stmt = std::exchange(other.m_stmt, nullptr);
        m_bindError = std::move(other.m_bindError);
        other.m_bindError.reset();
    }
    return *this;
}

void Statement::recordBindError(int rc, int index) {
    if (m_bindError)
        return;
    std::string context = "binding parameter " + std::to_string(index);
    if (m_stmt != nullptr) {
        context += " of ";
        context += abbreviate(sqlText(m_stmt));
    }
    m_bindError = sqliteError(m_stmt != nullptr ? sqlite3_db_handle(m_stmt) : nullptr, rc, context);
}

Statement& Statement::bindInt64(int index, std::int64_t value) {
    const int rc = m_stmt != nullptr ? sqlite3_bind_int64(m_stmt, index, value) : SQLITE_MISUSE;
    if (rc != SQLITE_OK)
        recordBindError(rc, index);
    return *this;
}

Statement& Statement::bindDouble(int index, double value) {
    const int rc = m_stmt != nullptr ? sqlite3_bind_double(m_stmt, index, value) : SQLITE_MISUSE;
    if (rc != SQLITE_OK)
        recordBindError(rc, index);
    return *this;
}

Statement& Statement::bindText(int index, std::string_view utf8) {
    // A null pointer would bind SQL NULL; empty strings must stay ''.
    const char* data = utf8.data() != nullptr ? utf8.data() : "";
    const int rc = m_stmt != nullptr ? sqlite3_bind_text64(m_stmt, index, data, utf8.size(),
                                                           transientDestructor(), SQLITE_UTF8)
                                     : SQLITE_MISUSE;
    if (rc != SQLITE_OK)
        recordBindError(rc, index);
    return *this;
}

Statement& Statement::bindBlob(int index, std::span<const std::uint8_t> bytes) {
    int rc = SQLITE_MISUSE;
    if (m_stmt != nullptr) {
        rc = bytes.empty() ? sqlite3_bind_zeroblob(m_stmt, index, 0)
                           : sqlite3_bind_blob64(m_stmt, index, bytes.data(), bytes.size(),
                                                 transientDestructor());
    }
    if (rc != SQLITE_OK)
        recordBindError(rc, index);
    return *this;
}

Statement& Statement::bindNull(int index) {
    const int rc = m_stmt != nullptr ? sqlite3_bind_null(m_stmt, index) : SQLITE_MISUSE;
    if (rc != SQLITE_OK)
        recordBindError(rc, index);
    return *this;
}

Result<bool> Statement::step() {
    if (m_stmt == nullptr)
        return makeError(ErrorCode::Internal, "The SQL statement is not prepared.");
    if (m_bindError)
        return *m_bindError;
    const int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW)
        return true;
    if (rc == SQLITE_DONE)
        return false;
    return sqliteError(sqlite3_db_handle(m_stmt), rc, abbreviate(sqlText(m_stmt)));
}

Status Statement::run() {
    for (;;) {
        auto row = step();
        if (!row)
            return std::move(row).error();
        if (!*row)
            return {};
    }
}

void Statement::reset() noexcept {
    if (m_stmt != nullptr) {
        sqlite3_reset(m_stmt);
        sqlite3_clear_bindings(m_stmt);
    }
    m_bindError.reset();
}

int Statement::columnCount() const noexcept {
    return m_stmt != nullptr ? sqlite3_column_count(m_stmt) : 0;
}

bool Statement::isNull(int column) const noexcept {
    return sqlite3_column_type(m_stmt, column) == SQLITE_NULL;
}

std::int64_t Statement::columnInt64(int column) const noexcept {
    return sqlite3_column_int64(m_stmt, column);
}

int Statement::columnInt(int column) const noexcept {
    return sqlite3_column_int(m_stmt, column);
}

bool Statement::columnBool(int column) const noexcept {
    return sqlite3_column_int64(m_stmt, column) != 0;
}

double Statement::columnDouble(int column) const noexcept {
    return sqlite3_column_double(m_stmt, column);
}

std::string Statement::columnText(int column) const {
    // Per the SQLite docs: fetch the pointer first, then the byte count.
    const unsigned char* text = sqlite3_column_text(m_stmt, column);
    const int size = sqlite3_column_bytes(m_stmt, column);
    if (text == nullptr || size <= 0)
        return {};
    return {reinterpret_cast<const char*>(text), static_cast<std::size_t>(size)};
}

std::optional<std::string> Statement::columnOptionalText(int column) const {
    if (isNull(column))
        return std::nullopt;
    return columnText(column);
}

std::optional<std::int64_t> Statement::columnOptionalInt64(int column) const noexcept {
    if (isNull(column))
        return std::nullopt;
    return columnInt64(column);
}

std::vector<std::uint8_t> Statement::columnBlob(int column) const {
    const void* data = sqlite3_column_blob(m_stmt, column);
    const int size = sqlite3_column_bytes(m_stmt, column);
    if (data == nullptr || size <= 0)
        return {};
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    return {bytes, bytes + size};
}

// --- Transaction ---------------------------------------------------------------

Result<Transaction> Transaction::begin(Connection& connection, TransactionMode mode) {
    if (!connection.isOpen())
        return makeError(ErrorCode::Internal, "The database connection is closed.");
    if (!connection.inTransaction()) {
        PMM_TRY(connection.exec(mode == TransactionMode::Immediate ? "BEGIN IMMEDIATE" : "BEGIN"));
        return Transaction(connection, std::string());
    }
    std::string name = "pmm_sp_" + std::to_string(++connection.m_savepointCounter);
    PMM_TRY(connection.exec("SAVEPOINT " + name));
    return Transaction(connection, std::move(name));
}

Transaction::Transaction(Transaction&& other) noexcept
    : m_connection(std::exchange(other.m_connection, nullptr)),
      m_savepoint(std::move(other.m_savepoint)), m_active(std::exchange(other.m_active, false)) {}

Transaction::~Transaction() {
    if (m_active)
        (void)rollback();
}

Status Transaction::commit() {
    if (!m_active || m_connection == nullptr)
        return makeError(ErrorCode::Internal, "The transaction is no longer active.");
    if (!m_connection->inTransaction()) {
        // SQLite rolled the transaction back on its own (e.g. after a full disk).
        m_active = false;
        return makeError(ErrorCode::DatabaseError,
                         "The change could not be saved because the database rolled it back.",
                         "transaction no longer active at commit");
    }
    // On failure the transaction stays active and the destructor rolls it back.
    PMM_TRY(
        m_connection->exec(m_savepoint.empty() ? std::string("COMMIT") : "RELEASE " + m_savepoint));
    m_active = false;
    return {};
}

Status Transaction::rollback() {
    if (!m_active || m_connection == nullptr)
        return {};
    m_active = false;
    if (!m_connection->inTransaction())
        return {}; // already rolled back by SQLite
    if (m_savepoint.empty())
        return m_connection->exec("ROLLBACK");
    Status undo = m_connection->exec("ROLLBACK TO " + m_savepoint);
    Status release = m_connection->exec("RELEASE " + m_savepoint);
    if (!undo.ok())
        return undo;
    return release;
}

} // namespace pmm::persistence
