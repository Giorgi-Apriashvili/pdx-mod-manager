#pragma once

// Shared helpers for the game adapter tests: hermetic temporary directories
// whose names contain spaces and non-ASCII characters, and installation of the
// fixture trees under tests/fixtures/games with "@ROOT@" replaced by the
// temporary root, so that paths inside descriptors and Steam files point into
// the copy.

#include "core/model.hpp"
#include "games/game_adapter.hpp"
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
    explicit TempDir(std::string_view tag = "pmm games") {
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
        fs::remove_all(m_path, ec);
    }

    const fs::path& path() const noexcept { return m_path; }

private:
    fs::path m_path;
};

inline fs::path u8path(std::string_view utf8) {
    return platform::pathFromUtf8(utf8);
}

inline fs::path fixturePath(std::string_view relative) {
    return u8path(PMM_FIXTURES_DIR) / u8path(relative);
}

inline std::string readText(const fs::path& path) {
    auto contents = platform::readFile(path);
    REQUIRE(contents.ok());
    return *contents;
}

inline void writeText(const fs::path& path, std::string_view text) {
    REQUIRE(platform::writeFileAtomic(path, text).ok());
}

inline std::string replaceAll(std::string text, std::string_view from, std::string_view to) {
    for (std::size_t pos = text.find(from); pos != std::string::npos;
         pos = text.find(from, pos + to.size()))
        text.replace(pos, from.size(), to);
    return text;
}

// Copies tests/fixtures/games/<name> into `destination`, replacing "@ROOT@" in
// text files with the generic UTF-8 form of `destination`.
inline void installFixture(std::string_view name, const fs::path& destination) {
    const fs::path source = fixturePath("games/" + std::string(name));
    const std::string root = platform::toGenericUtf8(destination);
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        const fs::path target = destination / entry.path().lexically_relative(source);
        if (entry.is_directory()) {
            fs::create_directories(target);
            continue;
        }
        std::string contents = readText(entry.path());
        const std::string extension = platform::pathToUtf8(entry.path().extension());
        if (extension == ".mod" || extension == ".json" || extension == ".vdf" ||
            extension == ".acf")
            contents = replaceAll(std::move(contents), "@ROOT@", root);
        writeText(target, contents);
    }
}

// A fixture tree installed below "<temp>/Ünïcødé dir/日本語 root" plus a
// DiscoveryEnvironment pointing into it.
struct GameFixture {
    TempDir temp;
    fs::path root;
    games::DiscoveryEnvironment env;

    explicit GameFixture(std::string_view fixture) {
        root = temp.path() / u8path("Ünïcødé dir/日本語 root");
        installFixture(fixture, root);
        env.steamRoot = root / "Steam";
        env.documentsDir = root / "Documents";
        env.home = root;
    }

    // Generic absolute form, as written into descriptors.
    std::string generic(std::string_view relative = {}) const {
        return platform::toGenericUtf8(
            (relative.empty() ? root : root / u8path(relative)).lexically_normal());
    }
    fs::path path(std::string_view relative) const { return root / u8path(relative); }
};

inline bool samePath(const fs::path& a, const fs::path& b) {
    return a.lexically_normal() == b.lexically_normal();
}

template <class Mods> const InstalledMod* findMod(const Mods& mods, std::string_view id) {
    const auto it = std::find_if(mods.begin(), mods.end(),
                                 [&](const InstalledMod& m) { return m.identity.id == id; });
    return it == mods.end() ? nullptr : &*it;
}

inline bool containsText(const std::vector<std::string>& lines, std::string_view needle) {
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) {
        return line.find(needle) != std::string::npos;
    });
}

} // namespace pmm::test
