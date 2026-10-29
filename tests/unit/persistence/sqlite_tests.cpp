#include "persistence/sqlite.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::persistence;
using namespace pmm::persistence::test;

namespace {
Connection memoryConnection() {
    auto connection = require(Connection::openInMemory());
    requireOk(connection.exec("CREATE TABLE t (id INTEGER PRIMARY KEY, txt TEXT, num INTEGER, "
                              "data BLOB, opt INTEGER, UNIQUE (txt))"));
    return connection;
}

std::int64_t countRows(Connection& connection) {
    auto stmt = require(connection.prepare("SELECT count(*) FROM t"));
    REQUIRE(require(stmt.step()));
    return stmt.columnInt64(0);
}
} // namespace

TEST_CASE("Statement binds and reads UTF-8 text, integers, blobs and NULL",
          "[persistence][sqlite]") {
    auto connection = memoryConnection();
    const std::string unicode = "Ünïcødé 日本語 \xF0\x9F\x9A\x80";
    const std::vector<std::uint8_t> blob{0x00, 0xFF, 0x10, 0x00};
    auto insert =
        require(connection.prepare("INSERT INTO t (txt, num, data, opt) VALUES (?, ?, ?, ?)"));
    requireOk(insert
                  .bindAll(unicode, std::numeric_limits<std::int64_t>::min(), blob,
                           std::optional<std::int64_t>())
                  .run());
    insert.reset();
    requireOk(insert
                  .bindAll(std::string(), std::numeric_limits<std::int64_t>::max(),
                           std::vector<std::uint8_t>(), std::optional<std::int64_t>(7))
                  .run());

    auto select = require(connection.prepare("SELECT txt, num, data, opt, typeof(txt), "
                                             "typeof(data) FROM t ORDER BY id"));
    REQUIRE(require(select.step()));
    CHECK(select.columnText(0) == unicode);
    CHECK(select.columnInt64(1) == std::numeric_limits<std::int64_t>::min());
    CHECK(select.columnBlob(2) == blob);
    CHECK(select.isNull(3));
    CHECK_FALSE(select.columnOptionalInt64(3).has_value());

    REQUIRE(require(select.step()));
    // Empty strings and blobs stay empty values, never NULL.
    CHECK(select.columnText(4) == "text");
    CHECK(select.columnText(5) == "blob");
    CHECK(select.columnOptionalText(0) == std::optional<std::string>(""));
    CHECK(select.columnInt64(1) == std::numeric_limits<std::int64_t>::max());
    CHECK(select.columnBlob(2).empty());
    CHECK(select.columnOptionalInt64(3) == std::optional<std::int64_t>(7));
    CHECK_FALSE(require(select.step()));
}

TEST_CASE("Unsigned 64-bit values round-trip bit for bit", "[persistence][sqlite]") {
    auto connection = memoryConnection();
    const std::uint64_t big = std::numeric_limits<std::uint64_t>::max() - 5;
    auto insert = require(connection.prepare("INSERT INTO t (num) VALUES (?)"));
    requireOk(insert.bind(1, big).run());
    auto select = require(connection.prepare("SELECT num FROM t"));
    REQUIRE(require(select.step()));
    CHECK(static_cast<std::uint64_t>(select.columnInt64(0)) == big);
}

TEST_CASE("Bind failures are reported by the next step", "[persistence][sqlite]") {
    auto connection = memoryConnection();
    auto insert = require(connection.prepare("INSERT INTO t (num) VALUES (?)"));
    insert.bind(1, 1).bind(5, 2); // parameter 5 does not exist
    auto status = insert.run();
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().detail.find("parameter 5") != std::string::npos);
    CHECK(countRows(connection) == 0);
    insert.reset();
    requireOk(insert.bind(1, 3).run());
    CHECK(countRows(connection) == 1);
}

TEST_CASE("prepare() accepts exactly one statement", "[persistence][sqlite]") {
    auto connection = memoryConnection();
    CHECK(connection.prepare("SELECT 1;  ").ok());
    auto two = connection.prepare("SELECT 1; SELECT 2");
    REQUIRE_FALSE(two.ok());
    CHECK(two.error().code == ErrorCode::Internal);
    auto bad = connection.prepare("SELEKT nonsense");
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().code == ErrorCode::DatabaseError);
}

TEST_CASE("Unique violations map to AlreadyExists", "[persistence][sqlite]") {
    auto connection = memoryConnection();
    requireOk(connection.exec("INSERT INTO t (txt) VALUES ('a')"));
    auto status = connection.exec("INSERT INTO t (txt) VALUES ('a')");
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().code == ErrorCode::AlreadyExists);
}

TEST_CASE("Transaction guard rolls back unless committed", "[persistence][sqlite]") {
    auto connection = memoryConnection();
    {
        auto tx = require(Transaction::begin(connection));
        CHECK_FALSE(tx.isNested());
        requireOk(connection.exec("INSERT INTO t (num) VALUES (1)"));
    }
    CHECK_FALSE(connection.inTransaction());
    CHECK(countRows(connection) == 0);
    {
        auto tx = require(Transaction::begin(connection));
        requireOk(connection.exec("INSERT INTO t (num) VALUES (1)"));
        requireOk(tx.commit());
        CHECK_FALSE(tx.isActive());
    }
    CHECK(countRows(connection) == 1);
}

TEST_CASE("Nested transaction guards use savepoints", "[persistence][sqlite]") {
    auto connection = memoryConnection();
    auto outer = require(Transaction::begin(connection));
    requireOk(connection.exec("INSERT INTO t (num) VALUES (1)"));
    {
        auto inner = require(Transaction::begin(connection));
        CHECK(inner.isNested());
        requireOk(connection.exec("INSERT INTO t (num) VALUES (2)"));
        requireOk(inner.rollback());
    }
    {
        auto inner = require(Transaction::begin(connection));
        requireOk(connection.exec("INSERT INTO t (num) VALUES (3)"));
        requireOk(inner.commit());
    }
    CHECK(connection.inTransaction());
    requireOk(outer.commit());
    auto select = require(connection.prepare("SELECT group_concat(num) FROM t ORDER BY id"));
    REQUIRE(require(select.step()));
    CHECK(select.columnText(0) == "1,3");
}
