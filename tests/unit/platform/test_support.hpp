#pragma once

// Shared helpers for the platform tests: hermetic temporary directories whose
// names contain spaces and non-ASCII characters, and fixture lookup.

#include "platform/fs.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace pmm::test {

namespace fs = std::filesystem;

// Unique directory under the system temp folder, deleted on destruction.
class TempDir {
public:
    explicit TempDir(std::string_view tag = "pmm platform") {
        static std::atomic<unsigned> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path() /
                 platform::pathFromUtf8(std::string(tag) + " Ünïcødé " + std::to_string(stamp) +
                                        "-" + std::to_string(counter++));
        fs::create_directories(m_path);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    ~TempDir() {
        std::error_code ec;
        // Undo read-only markers set by tests so that removal succeeds.
        fs::permissions(m_path, fs::perms::owner_all, fs::perm_options::add, ec);
        for (fs::recursive_directory_iterator it(m_path, ec), end; !ec && it != end;
             it.increment(ec)) {
            std::error_code ignored;
            fs::permissions(it->path(), fs::perms::owner_all, fs::perm_options::add, ignored);
        }
        fs::remove_all(m_path, ec);
    }

    const fs::path& path() const noexcept { return m_path; }
    fs::path operator/(std::string_view utf8) const {
        return m_path / platform::pathFromUtf8(utf8);
    }

private:
    fs::path m_path;
};

// Relative path with spaces and non-ASCII characters in several components.
inline fs::path unicodeSubdir() {
    return platform::pathFromUtf8("Ünïcødé dir/日本語 folder");
}

inline fs::path fixturePath(std::string_view relative) {
    return platform::pathFromUtf8(PMM_FIXTURES_DIR) / platform::pathFromUtf8(relative);
}

inline std::string readText(const fs::path& path) {
    auto contents = platform::readFile(path);
    REQUIRE(contents.ok());
    return *contents;
}

inline void writeText(const fs::path& path, std::string_view text) {
    REQUIRE(platform::writeFileAtomic(path, text).ok());
}

// Names of the entries directly inside `directory`, as UTF-8.
inline std::vector<std::string> entryNames(const fs::path& directory) {
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(directory))
        names.push_back(platform::pathToUtf8(entry.path().filename()));
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace pmm::test
