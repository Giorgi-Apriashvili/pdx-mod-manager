#pragma once

// Small helpers shared by the provider tests: hermetic temp directories and
// UTF-8 path conversion.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <system_error>

namespace pmm::test {

namespace fs = std::filesystem;

inline fs::path u8path(std::string_view utf8) {
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string utf8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

inline void writeFile(const fs::path& path, std::string_view contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

// Unique directory under the system temp dir, removed on destruction.
class TempDir {
public:
    explicit TempDir(std::string_view tag) {
        std::random_device rd;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path =
            fs::temp_directory_path() / u8path("pmm-providers-" + std::string(tag) + "-" +
                                               std::to_string(stamp) + "-" + std::to_string(rd()));
        fs::create_directories(m_path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
};

} // namespace pmm::test
