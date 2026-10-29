#include "platform/fs.hpp"
#include "platform/paths.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdlib>
#endif

using namespace pmm::platform;

namespace {

void setVariable(const std::string& name, const std::string* value) {
#if defined(_WIN32)
    const fs::path wideName = pathFromUtf8(name);
    const fs::path wideValue = value ? pathFromUtf8(*value) : fs::path();
    SetEnvironmentVariableW(wideName.c_str(), value ? wideValue.c_str() : nullptr);
#else
    if (value)
        ::setenv(name.c_str(), value->c_str(), 1);
    else
        ::unsetenv(name.c_str());
#endif
}

EnvironmentLookup fakeEnvironment(std::map<std::string, std::string> values) {
    return [values = std::move(values)](const std::string& name) -> std::optional<std::string> {
        const auto it = values.find(name);
        if (it == values.end())
            return std::nullopt;
        return it->second;
    };
}

TextFileReader fakeFiles(std::map<std::string, std::string> files) {
    return [files = std::move(files)](const fs::path& path) -> std::optional<std::string> {
        const auto it = files.find(toGenericUtf8(path));
        if (it == files.end())
            return std::nullopt;
        return it->second;
    };
}

} // namespace

TEST_CASE("standardDirectories returns per-user locations for this OS", "[platform][paths]") {
    const StandardDirectories dirs = standardDirectories();
    CHECK(fs::is_directory(dirs.home));
    CHECK_FALSE(dirs.documents.empty());
    CHECK(dirs.appData.filename() == kAppFolderName);
#if defined(_WIN32)
    CHECK(dirs.cache.filename() == "cache");
    CHECK(dirs.cache.parent_path().filename() == kAppFolderName);
    CHECK(dirs.logs.filename() == "logs");
    CHECK(dirs.xdgDataHome.empty());
#elif defined(__APPLE__)
    CHECK(toGenericUtf8(dirs.appData).find("Library/Application Support") != std::string::npos);
    CHECK(dirs.xdgDataHome.empty());
#else
    CHECK_FALSE(dirs.xdgDataHome.empty());
    CHECK(dirs.logs.filename() == "logs");
#endif
}

TEST_CASE("environmentVariable reads UTF-8 values and distinguishes unset", "[platform][paths]") {
    const std::string name = "PMM_PLATFORM_TEST_VARIABLE";
    const std::string value = "välue with spaces 日本語";
    setVariable(name, &value);
    CHECK(environmentVariable(name) == value);

    const std::string longValue(5000, 'L'); // larger than the initial buffer
    setVariable(name, &longValue);
    CHECK(environmentVariable(name) == longValue);

    setVariable(name, nullptr);
    CHECK_FALSE(environmentVariable(name).has_value());
    CHECK_FALSE(environmentVariable("").has_value());
}

TEST_CASE("parseXdgUserDir follows user-dirs.dirs syntax", "[platform][paths]") {
    const fs::path home = pathFromUtf8("/home/example");
    const std::string file = "# This file is written by xdg-user-dirs-update\n"
                             "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
                             "XDG_DOCUMENTS_DIR=\"$HOME/Dokumente mit Leerzeichen\"\n"
                             "XDG_DOWNLOAD_DIR=\"/mnt/data/Downloads\"\n"
                             "XDG_MUSIC_DIR=\"$HOME/\"\n"
                             "XDG_VIDEOS_DIR=\"relative/path\"\n"
                             "XDG_PICTURES_DIR=\"$HOME/Bilder \\\"quoted\\\"\"\n";
    const auto documents = parseXdgUserDir(file, "XDG_DOCUMENTS_DIR", home);
    REQUIRE(documents);
    CHECK(toGenericUtf8(*documents) == "/home/example/Dokumente mit Leerzeichen");
    CHECK(toGenericUtf8(*parseXdgUserDir(file, "XDG_DOWNLOAD_DIR", home)) == "/mnt/data/Downloads");
    CHECK(toGenericUtf8(*parseXdgUserDir(file, "XDG_PICTURES_DIR", home)) ==
          "/home/example/Bilder \"quoted\"");
    CHECK_FALSE(parseXdgUserDir(file, "XDG_MUSIC_DIR", home));  // $HOME means disabled
    CHECK_FALSE(parseXdgUserDir(file, "XDG_VIDEOS_DIR", home)); // relative is invalid
    CHECK_FALSE(parseXdgUserDir(file, "XDG_TEMPLATES_DIR", home));
    CHECK_FALSE(parseXdgUserDir(file, "XDG_DOCUMENTS", home)); // no prefix matches
    CHECK_FALSE(parseXdgUserDir("XDG_DOCUMENTS_DIR=\"/home/example\"", "XDG_DOCUMENTS_DIR", home));
    // Later assignments win, as when a shell sources the file.
    CHECK(toGenericUtf8(
              *parseXdgUserDir("XDG_DOCUMENTS_DIR=\"$HOME/A\"\r\nXDG_DOCUMENTS_DIR=\"$HOME/B\"\r\n",
                               "XDG_DOCUMENTS_DIR", home)) == "/home/example/B");
}

TEST_CASE("xdgStandardDirectories applies XDG defaults and overrides", "[platform][paths]") {
    const fs::path home = pathFromUtf8("/home/example");

    SECTION("defaults") {
        const auto dirs = xdgStandardDirectories(home, fakeEnvironment({}), fakeFiles({}));
        CHECK(toGenericUtf8(dirs.home) == "/home/example");
        CHECK(toGenericUtf8(dirs.xdgDataHome) == "/home/example/.local/share");
        CHECK(toGenericUtf8(dirs.appData) == "/home/example/.local/share/ParadoxModManager");
        CHECK(toGenericUtf8(dirs.cache) == "/home/example/.cache/ParadoxModManager");
        CHECK(toGenericUtf8(dirs.logs) == "/home/example/.local/state/ParadoxModManager/logs");
        CHECK(toGenericUtf8(dirs.documents) == "/home/example/Documents");
    }

    SECTION("absolute overrides and user-dirs.dirs from XDG_CONFIG_HOME") {
        const auto dirs = xdgStandardDirectories(
            home,
            fakeEnvironment({{"XDG_DATA_HOME", "/data/ünï"},
                             {"XDG_CACHE_HOME", "/var/cache/example"},
                             {"XDG_STATE_HOME", "/state"},
                             {"XDG_CONFIG_HOME", "/cfg"}}),
            fakeFiles({{"/cfg/user-dirs.dirs", "XDG_DOCUMENTS_DIR=\"$HOME/Документы\"\n"}}));
        CHECK(toGenericUtf8(dirs.xdgDataHome) == "/data/ünï");
        CHECK(toGenericUtf8(dirs.appData) == "/data/ünï/ParadoxModManager");
        CHECK(toGenericUtf8(dirs.cache) == "/var/cache/example/ParadoxModManager");
        CHECK(toGenericUtf8(dirs.logs) == "/state/ParadoxModManager/logs");
        CHECK(toGenericUtf8(dirs.documents) == "/home/example/Документы");
    }

    SECTION("relative or empty XDG values are ignored") {
        const auto dirs = xdgStandardDirectories(
            home, fakeEnvironment({{"XDG_DATA_HOME", "relative/data"}, {"XDG_CACHE_HOME", ""}}),
            fakeFiles(
                {{"/home/example/.config/user-dirs.dirs", "XDG_DOCUMENTS_DIR=\"$HOME/\"\n"}}));
        CHECK(toGenericUtf8(dirs.xdgDataHome) == "/home/example/.local/share");
        CHECK(toGenericUtf8(dirs.cache) == "/home/example/.cache/ParadoxModManager");
        CHECK(toGenericUtf8(dirs.documents) == "/home/example/Documents");
    }
}

TEST_CASE("macStandardDirectories uses ~/Library", "[platform][paths]") {
    const auto dirs = macStandardDirectories(pathFromUtf8("/Users/Example"));
    CHECK(toGenericUtf8(dirs.documents) == "/Users/Example/Documents");
    CHECK(toGenericUtf8(dirs.appData) ==
          "/Users/Example/Library/Application Support/ParadoxModManager");
    CHECK(toGenericUtf8(dirs.cache) == "/Users/Example/Library/Caches/ParadoxModManager");
    CHECK(toGenericUtf8(dirs.logs) == "/Users/Example/Library/Logs/ParadoxModManager");
    CHECK(dirs.xdgDataHome.empty());
}
