#pragma once

// Standard per-user OS locations. Implemented natively (Known Folders on
// Windows, XDG on Linux, ~/Library on macOS) so lower layers stay Qt-free.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

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

// The Linux and macOS rules below are pure functions of their inputs so that
// they are testable on every OS; standardDirectories() feeds them the real
// environment on those systems.
using EnvironmentLookup = std::function<std::optional<std::string>(const std::string&)>;
using TextFileReader = std::function<std::optional<std::string>(const fs::path&)>;

// XDG Base Directory rules: $XDG_DATA_HOME, $XDG_CACHE_HOME and $XDG_STATE_HOME
// are honoured only when absolute; documents come from XDG_DOCUMENTS_DIR in
// $XDG_CONFIG_HOME/user-dirs.dirs, falling back to ~/Documents.
StandardDirectories xdgStandardDirectories(const fs::path& home, const EnvironmentLookup& env,
                                           const TextFileReader& readTextFile);

// ~/Documents and ~/Library/{Application Support,Caches,Logs}.
StandardDirectories macStandardDirectories(const fs::path& home);

// Value of `key` (e.g. "XDG_DOCUMENTS_DIR") in user-dirs.dirs syntax:
// KEY="$HOME/Documents" or KEY="/absolute/path". nullopt when absent, relative,
// or equal to $HOME (which xdg-user-dirs uses to mean "disabled").
std::optional<fs::path> parseXdgUserDir(std::string_view contents, std::string_view key,
                                        const fs::path& home);

} // namespace pmm::platform
