#pragma once

// Standard per-user OS locations. Implemented natively (Known Folders on
// Windows, XDG on Linux, ~/Library on macOS) so lower layers stay Qt-free.

#include <filesystem>
#include <optional>
#include <string>

namespace pmm::platform {

namespace fs = std::filesystem;

struct StandardDirectories {
    fs::path home;
    fs::path documents;    // FOLDERID_Documents (honours OneDrive/folder redirection), XDG_DOCUMENTS_DIR, ~/Documents
    fs::path appData;      // %APPDATA%\ParadoxModManager, $XDG_DATA_HOME/ParadoxModManager, ~/Library/Application Support/ParadoxModManager
    fs::path cache;        // %LOCALAPPDATA%\ParadoxModManager\cache, $XDG_CACHE_HOME/..., ~/Library/Caches/...
    fs::path logs;         // %LOCALAPPDATA%\ParadoxModManager\logs, $XDG_STATE_HOME/.../logs, ~/Library/Logs/...
    fs::path xdgDataHome;  // Linux only: $XDG_DATA_HOME or ~/.local/share (Paradox user data lives here)
};

// Application folder name used inside the standard locations.
inline constexpr const char* kAppFolderName = "ParadoxModManager";

StandardDirectories standardDirectories();

// Environment variable as UTF-8; nullopt when unset.
std::optional<std::string> environmentVariable(const std::string& name);

} // namespace pmm::platform
