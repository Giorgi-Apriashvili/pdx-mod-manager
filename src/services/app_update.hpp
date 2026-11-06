#pragma once

// What the update check decides without a network (docs/updates.md): strict
// version numbers, the GitHub "latest release" reply, the checksum file and the
// allow-list of addresses. Everything fed in here came from the network and is
// treated as hostile: sizes are bounded, wrong types are errors and nothing but
// the version, the repository's validated name and the two expected assets is
// ever taken from a reply. The network itself lives in app::UpdateClient.

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
    // Download hops only; the API request follows no redirect. A host is
    // allowed when it is listed, or when it is one or more DNS labels followed
    // by one of the suffixes (each starting with a dot).
    std::vector<std::string> redirectHosts = {"github.com"};
    std::vector<std::string> redirectHostSuffixes = {".githubusercontent.com"};
    int port = -1; // -1: URLs must not name a port
};

// The repository slug from CMake ("owner/name"; root CMakeLists.txt). The check
// asks for the id, and a reply's own owner and name decide where its assets and
// its release page are; the slug only gives the list of releases while no reply
// has named the repository.
std::string_view updateRepository() noexcept;
// The repository's numeric GitHub id (PMM_GITHUB_REPOSITORY_ID in the root
// CMakeLists.txt), which survives renames and transfers.
std::string_view updateRepositoryId() noexcept;
// <scheme>://<apiHost>/repositories/<id>/releases/latest.
std::string latestReleaseApiUrl(const UpdateEndpoints& endpoints = {});
// From the slug and a parsed version: the release's page, or the list of
// releases without a version. For when no reply named the repository (none
// was published yet, or no check got an answer); otherwise the overloads
// below build it from the repository the reply named.
std::string releasePageUrl(const std::optional<AppVersion>& version);
std::string installerAssetName(const AppVersion& version);
// The arguments of an update run; the installer's side of this contract is
// packaging/windows.
std::vector<std::string> updateInstallerArguments();

// Exactly <scheme>://<downloadHost>/<repository>/releases/download/v<version>/
// followed by one plain file name, for a repository "<owner>/<name>" in
// GitHub's name grammar.
bool isAllowedAssetUrl(std::string_view url, std::string_view repository, const AppVersion& version,
                       const UpdateEndpoints& endpoints = {});
// An absolute URL with the expected scheme on an allowed redirect host: no
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
    // "<owner>/<name>" as the reply's html_url spells it, once the reply's url
    // and html_url were found to name this one repository. May differ from
    // updateRepository() after a rename or a transfer.
    std::string repository;
};

// https://github.com/<repository>/releases/tag/v<version>, built from the
// validated repository and version of a parsed release, never copied from it.
std::string releasePageUrl(const ReleaseInfo& release);
// The same for a repository a reply named (ReleaseInfo::repository): the page
// of `version`, or the list of releases without one. The slug's when
// `repository` is empty (no reply named one) or not "<owner>/<name>".
std::string releasePageUrl(std::string_view repository, const std::optional<AppVersion>& version);

// A reply of the releases/latest endpoint. Empty for a release flagged as draft
// or prerelease, which is ignored. An error for anything malformed: oversized,
// not JSON, wrong or missing fields, a tag that is not exactly v<x.y.z>, a url
// that is not <scheme>://<apiHost>/repos/<owner>/<name>/releases/<id>, an
// html_url that is not https://github.com/<owner>/<name>/releases/tag/<tag>,
// the two naming different repositories (compared without case, like GitHub
// does), or an expected asset whose URL is not in that repository's release
// or that is listed twice.
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
