#include "services/app_update.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <system_error>

namespace pmm::services {
namespace {

using Json = nlohmann::json;

constexpr std::string_view kRepository = PMM_GITHUB_REPOSITORY;

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

std::optional<std::uint32_t> parseComponent(std::string_view text) {
    if (text.empty() || !std::all_of(text.begin(), text.end(), isDigit) ||
        (text.size() > 1 && text.front() == '0'))
        return std::nullopt;
    std::uint32_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

// Asset and checksum names: one path component of ordinary characters.
bool isPlainFileName(std::string_view name) {
    if (name.empty() || name == "." || name == ".." || name.find("..") != std::string_view::npos)
        return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' ||
               c == '_' || c == '-' || c == '+';
    });
}

struct UrlParts {
    std::string_view scheme;
    std::string_view host;
    std::string_view port;
    std::string_view rest; // path and query
};

std::optional<UrlParts> splitUrl(std::string_view url) {
    const bool clean = std::all_of(url.begin(), url.end(), [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte > 0x20 && byte < 0x7f && c != '\\';
    });
    const std::size_t separator = url.find("://");
    if (!clean || separator == std::string_view::npos || separator == 0)
        return std::nullopt;
    UrlParts parts;
    parts.scheme = url.substr(0, separator);
    const std::string_view remainder = url.substr(separator + 3);
    const std::size_t end = remainder.find('/');
    if (end == std::string_view::npos)
        return std::nullopt; // no path: nothing an update could come from
    const std::string_view authority = remainder.substr(0, end);
    parts.rest = remainder.substr(end);
    if (authority.find_first_of("@%[]?#") != std::string_view::npos)
        return std::nullopt;
    const std::size_t colon = authority.find(':');
    parts.host = authority.substr(0, colon);
    if (colon != std::string_view::npos) {
        parts.port = authority.substr(colon + 1);
        if (parts.port.empty() || !std::all_of(parts.port.begin(), parts.port.end(), isDigit))
            return std::nullopt;
    }
    if (parts.host.empty() || parts.rest.find('#') != std::string_view::npos)
        return std::nullopt;
    return parts;
}

std::string origin(const UpdateEndpoints& endpoints, std::string_view host) {
    std::string out = endpoints.scheme + "://";
    out += host;
    if (endpoints.port >= 0)
        out += ":" + std::to_string(endpoints.port);
    return out;
}

std::string assetUrlPrefix(const AppVersion& version, const UpdateEndpoints& endpoints) {
    std::string out = origin(endpoints, endpoints.downloadHost) + "/";
    out += kRepository;
    out += "/releases/download/v" + version.toString() + "/";
    return out;
}

Error malformedRelease(std::string detail) {
    return makeError(ErrorCode::ParseError,
                     "GitHub's answer about the latest release is not valid.", std::move(detail));
}

// The exact contract name in the assets list, once, with its exact URL.
Result<std::optional<ReleaseAsset>> pickAsset(const Json& assets, const std::string& name,
                                              const AppVersion& version,
                                              const UpdateEndpoints& endpoints) {
    std::optional<ReleaseAsset> found;
    for (const Json& asset : assets) {
        const auto assetName = asset.find("name");
        if (assetName->get_ref<const std::string&>() != name)
            continue;
        if (found)
            return malformedRelease("asset listed twice: " + name);
        const auto url = asset.find("browser_download_url");
        if (url == asset.end() || !url->is_string())
            return malformedRelease("asset without a download URL: " + name);
        const auto& text = url->get_ref<const std::string&>();
        if (text != assetUrlPrefix(version, endpoints) + name ||
            !isAllowedAssetUrl(text, version, endpoints))
            return makeError(ErrorCode::ValidationFailed,
                             "The latest release points to a download address this application "
                             "does not trust, so it was ignored.",
                             "asset: " + name);
        found = ReleaseAsset{name, text};
    }
    return found;
}

} // namespace

std::string AppVersion::toString() const {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

std::optional<AppVersion> parseAppVersion(std::string_view text) {
    const std::size_t first = text.find('.');
    if (first == std::string_view::npos)
        return std::nullopt;
    const std::size_t second = text.find('.', first + 1);
    if (second == std::string_view::npos)
        return std::nullopt;
    const auto major = parseComponent(text.substr(0, first));
    const auto minor = parseComponent(text.substr(first + 1, second - first - 1));
    const auto patch = parseComponent(text.substr(second + 1));
    if (!major || !minor || !patch)
        return std::nullopt;
    return AppVersion{*major, *minor, *patch};
}

std::optional<AppVersion> parseReleaseTag(std::string_view tag) {
    if (tag.empty() || tag.front() != 'v')
        return std::nullopt;
    return parseAppVersion(tag.substr(1));
}

std::string_view updateRepository() noexcept {
    return kRepository;
}

std::string latestReleaseApiUrl(const UpdateEndpoints& endpoints) {
    std::string out = origin(endpoints, endpoints.apiHost) + "/repos/";
    out += kRepository;
    out += "/releases/latest";
    return out;
}

std::string releasePageUrl(const std::optional<AppVersion>& version) {
    std::string out = "https://github.com/";
    out += kRepository;
    out += "/releases";
    if (version)
        out += "/tag/v" + version->toString();
    return out;
}

std::string installerAssetName(const AppVersion& version) {
    return "ParadoxModManager-" + version.toString() + "-windows-x86_64-setup.exe";
}

std::vector<std::string> updateInstallerArguments() {
    return {"/SILENT", "/SP-", "/NORESTART", "/PMMUPDATE=1"};
}

bool isAllowedAssetUrl(std::string_view url, const AppVersion& version,
                       const UpdateEndpoints& endpoints) {
    const std::string prefix = assetUrlPrefix(version, endpoints);
    if (url.size() <= prefix.size() || url.compare(0, prefix.size(), prefix) != 0)
        return false;
    return isPlainFileName(url.substr(prefix.size()));
}

bool isAllowedRedirectTarget(std::string_view url, const UpdateEndpoints& endpoints) {
    const auto parts = splitUrl(url);
    if (!parts || parts->scheme != endpoints.scheme)
        return false;
    const std::string port = endpoints.port >= 0 ? std::to_string(endpoints.port) : std::string();
    if (parts->port != port)
        return false;
    return std::find(endpoints.redirectHosts.begin(), endpoints.redirectHosts.end(), parts->host) !=
           endpoints.redirectHosts.end();
}

Result<std::optional<ReleaseInfo>> parseLatestRelease(std::string_view json,
                                                      const UpdateEndpoints& endpoints) {
    if (json.size() > kMaxReleaseJsonBytes)
        return makeError(ErrorCode::LimitExceeded,
                         "GitHub's answer about the latest release is unexpectedly large.");
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return malformedRelease("not a JSON object");

    const auto flag = [&root](const char* key) -> std::optional<bool> {
        const auto it = root.find(key);
        if (it == root.end() || !it->is_boolean())
            return std::nullopt;
        return it->get<bool>();
    };
    const auto draft = flag("draft");
    const auto prerelease = flag("prerelease");
    if (!draft || !prerelease)
        return malformedRelease("draft or prerelease missing or not a boolean");
    if (*draft || *prerelease)
        return std::optional<ReleaseInfo>{};

    const auto tag = root.find("tag_name");
    if (tag == root.end() || !tag->is_string())
        return malformedRelease("tag_name missing or not a string");
    const auto version = parseReleaseTag(tag->get_ref<const std::string&>());
    if (!version)
        return malformedRelease("tag_name is not v<major>.<minor>.<patch>");

    const auto assets = root.find("assets");
    if (assets == root.end() || !assets->is_array())
        return malformedRelease("assets missing or not an array");
    for (const Json& asset : *assets) {
        const auto name = asset.is_object() ? asset.find("name") : asset.end();
        if (!asset.is_object() || name == asset.end() || !name->is_string())
            return malformedRelease("an asset is not an object with a name");
    }

    ReleaseInfo release;
    release.version = *version;
    auto installer = pickAsset(*assets, installerAssetName(*version), *version, endpoints);
    if (!installer)
        return std::move(installer).error();
    auto checksums = pickAsset(*assets, std::string(kChecksumAssetName), *version, endpoints);
    if (!checksums)
        return std::move(checksums).error();
    release.installer = std::move(installer).value();
    release.checksums = std::move(checksums).value();
    return std::optional<ReleaseInfo>(std::move(release));
}

Result<std::vector<ChecksumEntry>> parseChecksumFile(std::string_view text) {
    const auto invalid = [](std::string detail) {
        return makeError(ErrorCode::ParseError,
                         "The checksum file of the release is not valid, so the installer cannot "
                         "be verified.",
                         std::move(detail));
    };
    if (text.size() > kMaxChecksumFileBytes)
        return invalid("larger than the limit");
    if (!text.empty() && text.back() == '\n')
        text.remove_suffix(1);
    if (text.empty())
        return invalid("empty");

    constexpr std::size_t kDigestLength = 64;
    std::vector<ChecksumEntry> entries;
    for (std::size_t begin = 0; begin <= text.size();) {
        const std::size_t end = std::min(text.find('\n', begin), text.size());
        const std::string_view line = text.substr(begin, end - begin);
        begin = end + 1;

        const std::string lineNumber = std::to_string(entries.size() + 1);
        if (line.size() <= kDigestLength + 2 || line.compare(kDigestLength, 2, "  ") != 0)
            return invalid("line " + lineNumber + " is not \"<digest>  <name>\"");
        const std::string_view digest = line.substr(0, kDigestLength);
        const std::string_view name = line.substr(kDigestLength + 2);
        if (!std::all_of(digest.begin(), digest.end(),
                         [](char c) { return isDigit(c) || (c >= 'a' && c <= 'f'); }))
            return invalid("line " + lineNumber + " has no lowercase hex digest");
        if (!isPlainFileName(name))
            return invalid("line " + lineNumber + " has no plain file name");
        for (const ChecksumEntry& entry : entries) {
            if (entry.fileName == name)
                return invalid("a file is listed twice");
        }
        entries.push_back({std::string(digest), std::string(name)});
    }
    return entries;
}

Result<std::string> findChecksum(std::string_view text, std::string_view fileName) {
    auto entries = parseChecksumFile(text);
    if (!entries)
        return std::move(entries).error();
    for (ChecksumEntry& entry : entries.value()) {
        if (entry.fileName == fileName)
            return std::move(entry.sha256);
    }
    return makeError(ErrorCode::NotFound,
                     "The checksum file of the release does not list the installer, so it cannot "
                     "be verified.",
                     std::string(fileName));
}

bool hasInstallerLayout(const std::filesystem::path& appDir) {
    std::error_code ec;
    return std::filesystem::is_regular_file(appDir / ".." / "unins000.exe", ec);
}

} // namespace pmm::services
