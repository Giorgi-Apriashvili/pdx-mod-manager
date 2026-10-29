#pragma once

// Helpers shared by the persistence tests: unique temporary directories whose
// path contains spaces and non-ASCII characters, and small builders.

#include "core/model.hpp"
#include "core/result.hpp"
#include "persistence/database.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace pmm::persistence::test {

namespace fs = std::filesystem;

// Creates "<temp>/pmm-persistence-<unique>/Ünïcødé dir/日本語" and removes the
// unique root on destruction.
class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned> counter{0};
        std::random_device random;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = fs::temp_directory_path() /
                 ("pmm-persistence-" + std::to_string(stamp) + "-" + std::to_string(random()) +
                  "-" + std::to_string(counter++));
        m_path = m_root / fs::path(u8"Ünïcødé dir") / fs::path(u8"日本語");
        fs::create_directories(m_path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const noexcept { return m_path; }
    // Database file inside a not-yet-existing subdirectory with a space.
    fs::path dbFile() const { return m_path / "app data" / fs::path(u8"state ü.sqlite"); }

private:
    fs::path m_root;
    fs::path m_path;
};

inline std::string readBytes(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline std::string readFixture(const std::string& relative) {
    return readBytes(fs::path(PMM_FIXTURES_DIR) / "persistence" / relative);
}

template <class T> T require(Result<T> result) {
    if (!result.ok())
        FAIL("unexpected error: " << result.error().message << " | " << result.error().detail);
    return std::move(result).value();
}

inline void requireOk(const Status& status) {
    if (!status.ok())
        FAIL("unexpected error: " << status.error().message << " | " << status.error().detail);
}

inline std::unique_ptr<Database> openMemory() {
    return require(Database::openInMemory());
}

inline PlaysetEntry entry(std::string source, std::string id, bool enabled = true,
                          std::string nameHint = {}) {
    PlaysetEntry e;
    e.identity.source = std::move(source);
    e.identity.id = std::move(id);
    e.enabled = enabled;
    e.nameHint = std::move(nameHint);
    return e;
}

// `count` Workshop entries with ids "1000000000".."", every third disabled.
inline std::vector<PlaysetEntry> workshopEntries(int count, const GameKey& game = "stellaris",
                                                 int firstId = 1000000000) {
    std::vector<PlaysetEntry> out;
    for (int i = 0; i < count; ++i) {
        auto e = entry(source::SteamWorkshop, std::to_string(firstId + i), i % 3 != 2,
                       "Mod " + std::to_string(i));
        e.identity.game = game;
        out.push_back(std::move(e));
    }
    return out;
}

// Runs a statement against the raw connection (test setup only).
inline void execRaw(Database& db, const std::string& sql) {
    auto access = db.access();
    requireOk(access.connection().exec(sql));
}

inline std::int64_t queryInt(Database& db, const std::string& sql) {
    auto access = db.access();
    auto stmt = require(access.connection().prepare(sql));
    REQUIRE(require(stmt.step()));
    return stmt.columnInt64(0);
}

inline std::string queryText(Database& db, const std::string& sql) {
    auto access = db.access();
    auto stmt = require(access.connection().prepare(sql));
    REQUIRE(require(stmt.step()));
    return stmt.columnText(0);
}

#if defined(_WIN32)
// prefix + U+D800 + suffix: NTFS accepts such names although they are not
// valid UTF-16, so they have no exact UTF-8 form.
inline fs::path loneSurrogatePath(std::wstring prefix, const std::wstring& suffix) {
    prefix += static_cast<wchar_t>(0xD800);
    return fs::path(prefix + suffix);
}
#endif

} // namespace pmm::persistence::test
