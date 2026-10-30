#pragma once

// Standard per-user OS locations. Implemented natively (Known Folders) so lower
// layers stay Qt-free.

#include <filesystem>
#include <optional>
#include <string>

namespace pmm::platform {

namespace fs = std::filesystem;

struct StandardDirectories {
    fs::path home;
    fs::path documents; // FOLDERID_Documents (honours OneDrive/folder redirection)
    fs::path appData;   // %APPDATA%\ParadoxModManager
    fs::path cache;     // %LOCALAPPDATA%\ParadoxModManager\cache
    fs::path logs;      // %LOCALAPPDATA%\ParadoxModManager\logs
};

// Application folder name used inside the standard locations.
inline constexpr const char* kAppFolderName = "ParadoxModManager";

StandardDirectories standardDirectories();

// Environment variable as UTF-8; nullopt when unset.
std::optional<std::string> environmentVariable(const std::string& name);

} // namespace pmm::platform
