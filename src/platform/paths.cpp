#include "platform/paths.hpp"

#include "platform/fs.hpp"

#if defined(_WIN32)
#include "platform/detail/os_util.hpp"

#include <windows.h>
// Need <windows.h> first.
#include <knownfolders.h>
#include <shlobj.h>
#else
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdlib>
#include <vector>
#endif

namespace pmm::platform {
namespace {

std::string_view trim(std::string_view text) {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return text;
}

std::string withoutTrailingSlashes(std::string text) {
    while (text.size() > 1 && text.back() == '/')
        text.pop_back();
    return text;
}

// XDG variables count only when set to an absolute path; anything else is
// treated as unset, as the Base Directory specification requires.
std::optional<fs::path> absoluteXdgValue(const EnvironmentLookup& env, const char* name) {
    if (!env)
        return std::nullopt;
    const std::optional<std::string> value = env(name);
    if (!value || value->empty() || (*value)[0] != '/')
        return std::nullopt;
    return pathFromUtf8(*value);
}

#if defined(_WIN32)
fs::path knownFolder(const KNOWNFOLDERID& id) {
    PWSTR raw = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) && raw != nullptr)
        result = fs::path(raw);
    CoTaskMemFree(raw);
    return result;
}

fs::path environmentPath(const char* name) {
    const std::optional<std::string> value = environmentVariable(name);
    return value && !value->empty() ? pathFromUtf8(*value) : fs::path();
}
#else
fs::path posixHome() {
    if (const std::optional<std::string> home = environmentVariable("HOME"); home && !home->empty())
        return pathFromUtf8(*home);
    struct passwd entry{};
    struct passwd* found = nullptr;
    std::vector<char> buffer(16384);
    if (::getpwuid_r(::getuid(), &entry, buffer.data(), buffer.size(), &found) == 0 &&
        found != nullptr && found->pw_dir != nullptr)
        return fs::path(found->pw_dir);
    return {};
}

#if !defined(__APPLE__)
std::optional<std::string> readSmallTextFile(const fs::path& path) {
    Result<std::string> contents = readFile(path, 1024 * 1024);
    if (!contents)
        return std::nullopt;
    return std::move(contents).value();
}
#endif
#endif

} // namespace

std::optional<fs::path> parseXdgUserDir(std::string_view contents, std::string_view key,
                                        const fs::path& home) {
    std::optional<std::string> last;
    std::size_t pos = 0;
    while (pos < contents.size()) {
        std::size_t end = contents.find('\n', pos);
        if (end == std::string_view::npos)
            end = contents.size();
        std::string_view line = trim(contents.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line.front() == '#' || line.substr(0, key.size()) != key)
            continue;
        line = trim(line.substr(key.size()));
        if (line.empty() || line.front() != '=')
            continue;
        line = trim(line.substr(1));
        if (line.size() < 2 || line.front() != '"')
            continue;
        std::string value;
        bool closed = false;
        for (std::size_t i = 1; i < line.size(); ++i) {
            const char c = line[i];
            if (c == '\\' && i + 1 < line.size()) {
                value += line[++i];
            } else if (c == '"') {
                closed = true;
                break;
            } else {
                value += c;
            }
        }
        // The file is shell syntax, so a later assignment overrides an earlier one.
        if (closed)
            last = std::move(value);
    }
    if (!last)
        return std::nullopt;

    const std::string_view value = *last;
    if (value.substr(0, 5) == "$HOME") {
        std::string_view rest = value.substr(5);
        if (!rest.empty() && rest.front() != '/')
            return std::nullopt;
        while (!rest.empty() && rest.front() == '/')
            rest.remove_prefix(1);
        while (!rest.empty() && rest.back() == '/')
            rest.remove_suffix(1);
        if (rest.empty() || home.empty())
            return std::nullopt;
        return home / pathFromUtf8(rest);
    }
    if (value.empty() || value.front() != '/')
        return std::nullopt;
    if (!home.empty() &&
        withoutTrailingSlashes(std::string(value)) == withoutTrailingSlashes(toGenericUtf8(home)))
        return std::nullopt;
    return pathFromUtf8(value);
}

StandardDirectories xdgStandardDirectories(const fs::path& home, const EnvironmentLookup& env,
                                           const TextFileReader& readTextFile) {
    StandardDirectories dirs;
    dirs.home = home;
    const fs::path dataHome =
        absoluteXdgValue(env, "XDG_DATA_HOME").value_or(home / ".local" / "share");
    const fs::path cacheHome = absoluteXdgValue(env, "XDG_CACHE_HOME").value_or(home / ".cache");
    const fs::path stateHome =
        absoluteXdgValue(env, "XDG_STATE_HOME").value_or(home / ".local" / "state");
    const fs::path configHome = absoluteXdgValue(env, "XDG_CONFIG_HOME").value_or(home / ".config");

    dirs.xdgDataHome = dataHome;
    dirs.appData = dataHome / kAppFolderName;
    dirs.cache = cacheHome / kAppFolderName;
    dirs.logs = stateHome / kAppFolderName / "logs";
    dirs.documents = home / "Documents";
    if (readTextFile) {
        if (const std::optional<std::string> text = readTextFile(configHome / "user-dirs.dirs")) {
            if (std::optional<fs::path> documents =
                    parseXdgUserDir(*text, "XDG_DOCUMENTS_DIR", home))
                dirs.documents = std::move(*documents);
        }
    }
    return dirs;
}

StandardDirectories macStandardDirectories(const fs::path& home) {
    StandardDirectories dirs;
    dirs.home = home;
    dirs.documents = home / "Documents";
    dirs.appData = home / "Library" / "Application Support" / kAppFolderName;
    dirs.cache = home / "Library" / "Caches" / kAppFolderName;
    dirs.logs = home / "Library" / "Logs" / kAppFolderName;
    return dirs;
}

StandardDirectories standardDirectories() {
#if defined(_WIN32)
    StandardDirectories dirs;
    dirs.home = knownFolder(FOLDERID_Profile);
    if (dirs.home.empty())
        dirs.home = environmentPath("USERPROFILE");
    // FOLDERID_Documents follows OneDrive backup and user folder redirection.
    dirs.documents = knownFolder(FOLDERID_Documents);
    if (dirs.documents.empty())
        dirs.documents = dirs.home / "Documents";
    fs::path roaming = knownFolder(FOLDERID_RoamingAppData);
    if (roaming.empty())
        roaming = environmentPath("APPDATA");
    if (roaming.empty())
        roaming = dirs.home / "AppData" / "Roaming";
    fs::path local = knownFolder(FOLDERID_LocalAppData);
    if (local.empty())
        local = environmentPath("LOCALAPPDATA");
    if (local.empty())
        local = dirs.home / "AppData" / "Local";
    dirs.appData = roaming / kAppFolderName;
    dirs.cache = local / kAppFolderName / "cache";
    dirs.logs = local / kAppFolderName / "logs";
    return dirs;
#elif defined(__APPLE__)
    return macStandardDirectories(posixHome());
#else
    return xdgStandardDirectories(posixHome(), environmentVariable, readSmallTextFile);
#endif
}

std::optional<std::string> environmentVariable(const std::string& name) {
    if (name.empty())
        return std::nullopt;
#if defined(_WIN32)
    const std::wstring wideName = detail::utf8ToWide(name);
    std::wstring buffer(256, L'\0');
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetEnvironmentVariableW(wideName.c_str(), buffer.data(),
                                                     static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            if (GetLastError() == ERROR_ENVVAR_NOT_FOUND)
                return std::nullopt;
            return std::string(); // set but empty
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            return detail::wideToUtf8(buffer);
        }
        // Too small: `length` is the required size including the terminator.
        buffer.resize(length);
    }
#else
    const char* value = std::getenv(name.c_str());
    if (value == nullptr)
        return std::nullopt;
    return std::string(value);
#endif
}

} // namespace pmm::platform
