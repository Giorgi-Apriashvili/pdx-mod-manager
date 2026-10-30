#pragma once

// What the update check decides without a network (docs/updates.md): strict
// version numbers, the GitHub "latest release" reply, the checksum file and the
// allow-list of addresses. Everything fed in here came from the network and is
// treated as hostile: sizes are bounded, wrong types are errors and nothing but
// the version and the two expected assets is ever taken from a reply. The
// network itself lives in app::UpdateClient.

#include "core/result.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::services {

inline constexpr std::size_t kMaxReleaseJsonBytes = std::size_t{1024} * 1024;
inline constexpr std::size_t kMaxChecksumFileBytes = std::size_t{64} * 1024;
inline constexpr std::int64_t kMaxInstallerBytes = 512ll * 1024 * 1024;
inline constexpr int kMaxUpdateRedirects = 5;
inline constexpr std::string_view kChecksumAssetName = "SHA256SUMS.txt";

struct AppVersion {
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    auto operator<=>(const AppVersion&) const = default;
    std::string toString() const;
};

// Exactly <major>.<minor>.<patch>: decimal digits only, no leading zeros, no
// sign, suffix or surrounding whitespace.
std::optional<AppVersion> parseAppVersion(std::string_view text);
// Exactly v<major>.<minor>.<patch>.
std::optional<AppVersion> parseReleaseTag(std::string_view tag);

// Where the updater may connect. The defaults are the only values a shipped
// build uses: no environment variable or command-line switch reaches this
// struct, only the C++ tests do (a local stub).
struct UpdateEndpoints {
    std::string scheme = "https";
    std::string apiHost = "api.github.com";
    std::string downloadHost = "github.com";
    std::vector<std::string> redirectHosts = {"github.com", "release-assets.githubusercontent.com",
                                              "objects.githubusercontent.com"};
    int port = -1; // -1: URLs must not name a port
};

// The repository slug from CMake ("owner/name"; root CMakeLists.txt).
std::string_view updateRepository() noexcept;
std::string latestReleaseApiUrl(const UpdateEndpoints& endpoints = {});
// Built from the fixed repository and a parsed version, never from a reply:
// the release's page, or the list of releases without a version.
std::string releasePageUrl(const std::optional<AppVersion>& version);
std::string installerAssetName(const AppVersion& version);
// The arguments of an update run; the installer's side of this contract is
// packaging/windows.
std::vector<std::string> updateInstallerArguments();

// Exactly <scheme>://<downloadHost>/<repository>/releases/download/v<version>/
// followed by one plain file name.
bool isAllowedAssetUrl(std::string_view url, const AppVersion& version,
                       const UpdateEndpoints& endpoints = {});
// An absolute URL with the expected scheme on one of the redirect hosts: no
// user information, other port, backslash, control or non-ASCII character.
bool isAllowedRedirectTarget(std::string_view url, const UpdateEndpoints& endpoints = {});

struct ReleaseAsset {
    std::string name;
    std::string url;
};

struct ReleaseInfo {
    AppVersion version;
    // Each is present only under its exact contract name with an allowed URL.
    std::optional<ReleaseAsset> installer;
    std::optional<ReleaseAsset> checksums;
};

// A reply of the releases/latest endpoint. Empty for a release flagged as draft
// or prerelease, which is ignored. An error for anything malformed: oversized,
// not JSON, wrong or missing fields, a tag that is not exactly v<x.y.z>, or an
// expected asset whose URL is not allowed or that is listed twice.
Result<std::optional<ReleaseInfo>> parseLatestRelease(std::string_view json,
                                                      const UpdateEndpoints& endpoints = {});

struct ChecksumEntry {
    std::string sha256; // 64 lowercase hex digits
    std::string fileName;
};

// sha256sum text format, strictly: "<64 lowercase hex>  <name>" on every line,
// separated by LF, plain file names only, each name once. Any other line makes
// the whole file an error.
Result<std::vector<ChecksumEntry>> parseChecksumFile(std::string_view text);
// The digest of `fileName`; NotFound when the file is valid but does not list it.
Result<std::string> findChecksum(std::string_view text, std::string_view fileName);

// An installed copy has the uninstaller at <folder of pmm-app.exe>/../unins000.exe.
bool hasInstallerLayout(const std::filesystem::path& appDir);

} // namespace pmm::services
