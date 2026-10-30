#include "platform/paths.hpp"

#include "platform/detail/os_util.hpp"
#include "platform/fs.hpp"

#include <windows.h>
// Need <windows.h> first.
#include <knownfolders.h>
#include <shlobj.h>

namespace pmm::platform {
namespace {

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

} // namespace

StandardDirectories standardDirectories() {
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
}

std::optional<std::string> environmentVariable(const std::string& name) {
    if (name.empty())
        return std::nullopt;
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
}

} // namespace pmm::platform
