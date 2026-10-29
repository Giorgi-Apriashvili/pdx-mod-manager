#pragma once

// Domain model shared by all layers. These are plain value types: no I/O, no Qt.
//
// Ordering convention used everywhere in this code base: index 0 of an ordered
// mod list is the FIRST mod handed to the game. Each game adapter documents how
// that order maps to the game's override precedence (see docs/load-order.md).

#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pmm {

using GameKey = std::string; // "eu4", "stellaris", ...

namespace source {
// Provider keys. They are part of the share-code protocol: never rename.
inline constexpr const char* SteamWorkshop = "steam-workshop";
inline constexpr const char* Local = "local";
} // namespace source

// Portable identity of a mod: provider + provider-stable id, scoped to a game.
// Display names and absolute paths are never identities. Ids are strings so
// that 64-bit Workshop ids never lose precision.
struct ModIdentity {
    GameKey game;
    std::string source;
    std::string id;

    auto operator<=>(const ModIdentity&) const = default;
    bool operator==(const ModIdentity&) const = default;
};

// "stellaris:steam-workshop:1234567890"; for logs and map keys only.
std::string toString(const ModIdentity& identity);

enum class OsPlatform { Windows, Linux, MacOS };
OsPlatform currentOsPlatform() noexcept;
const char* toString(OsPlatform platform) noexcept;

enum class DistributionSource { Steam, Manual, Unknown };
const char* toString(DistributionSource source) noexcept;

// Capabilities are reported per game *and* per installation, because e.g.
// Workshop acquisition depends on the store the game was installed from.
struct Capabilities {
    bool directLaunch = false;        // adapter can build a validated LaunchPlan
    bool workshopAcquisition = false; // automated subscribe/download available
    bool localMods = false;
};

struct GameInstallation {
    GameKey game;
    std::filesystem::path installPath;  // game root (contains the executable)
    std::filesystem::path userDataPath; // e.g. Documents/Paradox Interactive/Stellaris
    std::filesystem::path executablePath;
    std::vector<std::string> defaultArguments; // from the game's own launcher settings
    DistributionSource distribution = DistributionSource::Unknown;
    OsPlatform platform = currentOsPlatform();
    std::string steamAppId;                 // "281990" when installed through Steam
    std::filesystem::path steamLibraryPath; // library root containing steamapps/, if Steam
    std::string version;                    // short version, e.g. "v4.5.2"
    std::string versionLabel;               // full label, e.g. "Cygnus v4.5.2 (9776)"
    bool manuallyConfigured = false;
};

// Parsed Clausewitz mod descriptor (.mod / descriptor.mod). Unknown keys are kept
// verbatim so regenerated descriptors preserve them.
struct ModDescriptor {
    std::string name;
    std::string version;
    std::string supportedVersion;
    std::vector<std::string> tags;
    std::vector<std::string> dependencies; // display names, as written by authors
    std::string picture;
    std::string path;    // as written (may be stale or from another machine)
    std::string archive; // legacy zipped mods
    std::string remoteFileId;
    std::vector<std::pair<std::string, std::string>> extra; // other scalar keys, in file order
};

enum class InstallState {
    Ready,       // content present and descriptor valid
    NeedsUpdate, // provider reports an update pending
    Downloading, // provider reports download in progress
    Broken,      // content missing or descriptor invalid
};
const char* toString(InstallState state) noexcept;

// A mod present on this machine. Independent from playset membership.
struct InstalledMod {
    ModIdentity identity;
    ModDescriptor descriptor;
    std::filesystem::path contentPath;    // directory (or archive) with mod files
    std::filesystem::path descriptorPath; // user-dir .mod file, empty if not registered yet
    std::string descriptorRef;            // e.g. "mod/ugc_123.mod" as used by the game
    InstallState state = InstallState::Ready;
    std::vector<std::string> problems; // human-readable issues found while scanning
    std::int64_t contentUpdatedAt = 0; // unix seconds, best effort
    std::string fingerprint;           // "sha256:<hex>" for local mods when computed
};

struct PlaysetEntry {
    ModIdentity identity;
    bool enabled = true;
    std::string nameHint;    // display hint only
    std::string versionHint; // informational
    std::string fingerprint; // informational, "sha256:<hex>" (local mods)

    bool operator==(const PlaysetEntry&) const = default;
};

struct Playset {
    std::int64_t id = 0; // local database id; 0 = not persisted
    GameKey game;
    std::string name;
    std::vector<PlaysetEntry> entries; // authoritative order, index 0 first
    std::int64_t createdAt = 0;        // unix milliseconds
    std::int64_t updatedAt = 0;        // unix milliseconds
};

struct PlaysetSummary {
    std::int64_t id = 0;
    GameKey game;
    std::string name;
    int entryCount = 0;
    int enabledCount = 0;
    std::int64_t updatedAt = 0;
};

// Lifecycle of acquiring one missing mod. Distinguishes "subscribed" and
// "download requested" from "installed and ready".
enum class AcquisitionState {
    Pending,           // queued, nothing sent to the provider yet
    Subscribing,       // subscribe request in flight
    Subscribed,        // provider confirmed subscription; no download confirmed
    DownloadRequested, // download requested; waiting for the provider to start
    Downloading,       // provider reports progress
    Installed,         // provider reports installed AND content verified on disk
    Failed,            // retryable failure (offline, timeout, provider error)
    Unavailable,       // item deleted, private, wrong game or invalid id
    NeedsUserAction,   // e.g. local mod mapping, provider not running, manual fallback
    Cancelled,
};
const char* toString(AcquisitionState state) noexcept;
std::optional<AcquisitionState> acquisitionStateFromString(const std::string& text) noexcept;
bool isTerminal(AcquisitionState state) noexcept;

struct AcquisitionJob {
    std::int64_t id = 0;
    ModIdentity identity;
    std::optional<std::int64_t> playsetId; // playset that requested it, if any
    AcquisitionState state = AcquisitionState::Pending;
    std::string errorCode;    // errorCodeName() of the last failure
    std::string errorMessage; // user-facing explanation of the last failure
    int attempts = 0;
    std::uint64_t bytesDownloaded = 0;
    std::uint64_t bytesTotal = 0;
    std::int64_t createdAt = 0; // unix milliseconds
    std::int64_t updatedAt = 0;
};

// Readiness of one playset entry for launching, derived from inventory + jobs.
enum class EntryStatus {
    Ready,
    Missing, // not installed, no job yet
    Pending, // acquisition queued / subscribed / download requested
    Downloading,
    Failed,          // acquisition failed; retry possible
    Unavailable,     // cannot be acquired (deleted, private, wrong game)
    NeedsUserAction, // e.g. unmapped local mod, manual download fallback
    Broken,          // installed but content/descriptor unusable
};
const char* toString(EntryStatus status) noexcept;

struct EntryReadiness {
    EntryStatus status = EntryStatus::Missing;
    std::string detail;                // short explanation for the UI
    std::vector<std::string> warnings; // non-blocking issues (version, dependency)
    double progress = -1.0;            // 0..1 while downloading, otherwise -1
};

// A file the launch must write before starting the game.
struct ConfigWrite {
    std::filesystem::path target;
    std::string contents;
    std::string purpose; // e.g. "load order (dlc_load.json)"
};

struct LaunchPlan {
    GameKey game;
    std::filesystem::path executable;
    std::filesystem::path workingDirectory;
    std::vector<std::string> arguments; // passed as an argv vector, never via a shell
    std::vector<std::pair<std::string, std::string>> environment; // additions/overrides
    std::vector<ConfigWrite> configWrites;
    std::vector<std::string> warnings;
};

} // namespace pmm
