#pragma once

// Steam client discovery: Valve KeyValues (VDF/ACF) parsing, Steam root and
// library discovery, app and Workshop manifests, and locating the Steamworks
// runtime library next to a game. Read-only: nothing here writes into a Steam
// folder.
//
// Paths read from Steam files are decoded as UTF-8. App ids are validated as
// decimal strings before they are used to build file names.

#include "core/result.hpp"
#include "platform/process.hpp"

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::platform::steam {

namespace fs = std::filesystem;

// ---- KeyValues text ---------------------------------------------------------

// One node of a KeyValues document: either a leaf ("key" "value") or a block
// ("key" { ... }). Duplicate keys are kept in file order.
struct KeyValue {
    std::string key;
    std::string value;              // leaves only
    std::vector<KeyValue> children; // blocks only
    bool isBlock = false;
    int line = 0; // 1-based line of the key

    // First direct child whose key matches ASCII case-insensitively.
    const KeyValue* child(std::string_view name) const noexcept;
    // Follows nested blocks, e.g. find({"AppState", "UserConfig", "language"}).
    const KeyValue* find(std::initializer_list<std::string_view> path) const noexcept;
    // Value of a direct leaf child; nullopt when missing or a block.
    std::optional<std::string> childValue(std::string_view name) const;
    std::string childValueOr(std::string_view name, std::string_view fallback = {}) const;
};

// ASCII case-insensitive comparison used for all KeyValues key lookups.
bool keyEquals(std::string_view a, std::string_view b) noexcept;

// Parses KeyValues text. Accepts a UTF-8 BOM, CRLF line endings, // comments,
// unquoted tokens, the escapes \\ \" \n \t (other backslashes stay literal) and
// trailing [$CONDITION] markers (ignored). The result is a synthetic root block
// whose children are the top-level pairs. Errors are ParseError with the line.
Result<KeyValue> parseKeyValues(std::string_view text);
Result<KeyValue> readKeyValuesFile(const fs::path& path);

// ---- Steam client and libraries ---------------------------------------------

// Places to look for the Steam client, most authoritative first:
// HKCU\Software\Valve\Steam\SteamPath, then
// HKLM\SOFTWARE\WOW6432Node\Valve\Steam\InstallPath, then
// C:\Program Files (x86)\Steam.
std::vector<fs::path> defaultSteamRootCandidates();

// True if `directory` contains a steamapps folder.
bool looksLikeSteamRoot(const fs::path& directory);

// First candidate that looks like a Steam root, canonicalised. NotFound lists
// the candidates that were checked.
Result<fs::path> findSteamRoot(const std::vector<fs::path>& candidates);
Result<fs::path> findSteamRoot();

struct LibraryFolder {
    fs::path path; // library root; contains steamapps/
    std::string label;
    std::vector<std::string> appIds; // from the "apps" block; empty for the legacy format
};

// Libraries from <steamRoot>/steamapps/libraryfolders.vdf. Understands the
// modern format ("0" { "path" "..." "apps" { ... } }) and the legacy one
// ("1" "D:\\Library"). The Steam root itself is always the first entry; a
// missing file yields just the root. Duplicates are removed.
Result<std::vector<LibraryFolder>> readLibraryFolders(const fs::path& steamRoot);
Result<std::vector<LibraryFolder>> parseLibraryFolders(const KeyValue& document,
                                                       const fs::path& steamRoot);

// Steam's EAppState bits used here.
inline constexpr std::uint32_t kAppStateUpdateRequired = 2;
inline constexpr std::uint32_t kAppStateFullyInstalled = 4;

// steamapps/appmanifest_<appid>.acf
struct AppManifest {
    std::string appId;
    std::string name;
    std::string installDir; // folder name under steamapps/common
    std::string buildId;
    std::uint32_t stateFlags = 0;
    std::int64_t lastUpdated = 0; // unix seconds
    std::uint64_t sizeOnDisk = 0;
    fs::path libraryRoot; // library containing steamapps/
    fs::path manifestPath;

    fs::path installPath() const; // <libraryRoot>/steamapps/common/<installDir>
    bool fullyInstalled() const noexcept { return (stateFlags & kAppStateFullyInstalled) != 0; }
    bool updateRequired() const noexcept { return (stateFlags & kAppStateUpdateRequired) != 0; }
};

// Reads an app manifest; libraryRoot is derived from the manifest location.
// ValidationFailed when installdir is empty or tries to leave steamapps/common.
Result<AppManifest> readAppManifest(const fs::path& acfPath);

// Finds appmanifest_<appId>.acf in the given libraries. When several
// libraries have one (stale leftovers), a fully installed app whose folder
// exists wins. NotFound when no library has it.
Result<AppManifest> findInstalledApp(const std::vector<LibraryFolder>& libraries,
                                     std::string_view appId);

// True for a non-empty string of at most 20 decimal digits.
bool isValidAppId(std::string_view appId) noexcept;

// <libraryRoot>/steamapps/workshop/content/<appId>
fs::path workshopContentDir(const fs::path& libraryRoot, std::string_view appId);
// <libraryRoot>/steamapps/workshop/appworkshop_<appId>.acf
fs::path appWorkshopManifestPath(const fs::path& libraryRoot, std::string_view appId);

// One Workshop item as recorded by the Steam client.
struct WorkshopItemState {
    std::string id;
    bool installed = false; // listed under WorkshopItemsInstalled
    std::uint64_t size = 0;
    std::int64_t timeUpdated = 0; // unix seconds of the installed version
    std::string manifest;         // installed content manifest id
    // From WorkshopItemDetails, when present:
    std::int64_t timeTouched = 0;
    std::string subscribedBy; // account id; empty when no longer subscribed
    std::int64_t latestTimeUpdated = 0;
    std::string latestManifest;

    // A newer version is known to Steam but not installed yet.
    bool hasPendingUpdate() const noexcept {
        return !latestManifest.empty() && latestManifest != manifest;
    }
};

// steamapps/workshop/appworkshop_<appid>.acf
struct AppWorkshopManifest {
    std::string appId;
    std::uint64_t sizeOnDisk = 0;
    bool needsUpdate = false;
    bool needsDownload = false;
    std::int64_t timeLastUpdated = 0;
    std::string lastBuildId;
    // Installed items first (file order), then items only present in
    // WorkshopItemDetails (known to Steam but not installed).
    std::vector<WorkshopItemState> items;

    const WorkshopItemState* findItem(std::string_view id) const noexcept;
};

Result<AppWorkshopManifest> parseAppWorkshopManifest(const KeyValue& document);
Result<AppWorkshopManifest> readAppWorkshopManifest(const fs::path& acfPath);

// ---- Steam client process and runtime ---------------------------------------

// Best effort: a steam.exe process, or the pid recorded in
// HKCU\Software\Valve\Steam\ActiveProcess when that process still exists.
bool isSteamRunning();
bool isSteamRunning(const std::vector<RunningProcess>& processes, std::int64_t registeredPid = 0);

// "steam_api64.dll".
const char* steamApiLibraryName() noexcept;

// Looks for the Steamworks runtime in a game installation: the root and the
// usual subfolders (bin, binaries, win64).
std::optional<fs::path> findSteamApiLibrary(const fs::path& gameInstallDir);

} // namespace pmm::platform::steam
