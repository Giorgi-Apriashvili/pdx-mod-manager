#include "services/app_update.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <system_error>

namespace pmm::services {
namespace {

using Json = nlohmann::json;

constexpr bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

constexpr bool isRepositoryId(std::string_view id) {
    return !id.empty() && id.size() <= 20 && id.front() != '0' &&
           std::all_of(id.begin(), id.end(), isDigit);
}

constexpr std::string_view kRepository = PMM_GITHUB_REPOSITORY;
constexpr std::string_view kRepositoryId = PMM_GITHUB_REPOSITORY_ID;
static_assert(isRepositoryId(kRepositoryId), "PMM_GITHUB_REPOSITORY_ID is not a repository id");

// Where release pages are: opened in the browser and never fetched, so test
// endpoints do not replace it.
constexpr std::string_view kWebOrigin = "https://github.com";

bool isAsciiAlnum(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

char asciiLower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoringCase(std::string_view a, std::string_view b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](char x, char y) { return asciiLower(x) == asciiLower(y); });
}

// GitHub's login grammar: letters, digits and single hyphens, neither first nor
// last, at most 39 characters.
bool isOwnerName(std::string_view name) {
    if (name.empty() || name.size() > 39 || name.front() == '-' || name.back() == '-' ||
        name.find("--") != std::string_view::npos)
        return false;
    return std::all_of(name.begin(), name.end(),
                       [](char c) { return isAsciiAlnum(c) || c == '-'; });
}

// GitHub's repository names: letters, digits, '.', '_' and '-', at most 100
// characters, not only dots.
bool isRepositoryName(std::string_view name) {
    if (name.empty() || name.size() > 100 || name.find_first_not_of('.') == std::string_view::npos)
        return false;
    return std::all_of(name.begin(), name.end(),
                       [](char c) { return isAsciiAlnum(c) || c == '.' || c == '_' || c == '-'; });
}

struct Repository {
    std::string_view owner;
    std::string_view name;
};

bool sameRepository(const Repository& a, const Repository& b) {
    return equalsIgnoringCase(a.owner, b.owner) && equalsIgnoringCase(a.name, b.name);
}

std::optional<Repository> splitRepository(std::string_view slug) {
    const std::size_t slash = slug.find('/');
    if (slash == std::string_view::npos)
        return std::nullopt;
    const Repository repository{slug.substr(0, slash), slug.substr(slash + 1)};
    if (!isOwnerName(repository.owner) || !isRepositoryName(repository.name))
        return std::nullopt;
    return repository;
}

// The '/'-separated segments of `url` after `prefix`, empty ones included.
std::optional<std::vector<std::string_view>> segmentsAfter(std::string_view url,
                                                           std::string_view prefix) {
    if (!url.starts_with(prefix))
        return std::nullopt;
    std::vector<std::string_view> segments;
    std::string_view rest = url.substr(prefix.size());
    for (;;) {
        const std::size_t slash = rest.find('/');
        segments.push_back(rest.substr(0, slash));
        if (slash == std::string_view::npos)
            return segments;
        rest.remove_prefix(slash + 1);
    }
}

struct RepositoryPath {
    Repository repository;
    std::vector<std::string_view> rest; // the segments after <owner>/<name>
};

// <prefix><owner>/<name>/<rest...>
std::optional<RepositoryPath> repositoryPath(std::string_view url, std::string_view prefix) {
    auto segments = segmentsAfter(url, prefix);
    if (!segments || segments->size() < 2)
        return std::nullopt;
    RepositoryPath path{{(*segments)[0], (*segments)[1]}, {}};
    if (!isOwnerName(path.repository.owner) || !isRepositoryName(path.repository.name))
        return std::nullopt;
    path.rest.assign(segments->begin() + 2, segments->end());
    return path;
}

bool isReleaseId(std::string_view id) {
    return !id.empty() && id.size() <= 20 && std::all_of(id.begin(), id.end(), isDigit);
}

// One DNS label as a URL parser writes it: lowercase, no leading or trailing
// hyphen.
bool isHostLabel(std::string_view label) {
    if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-')
        return false;
    return std::all_of(label.begin(), label.end(),
                       [](char c) { return isDigit(c) || (c >= 'a' && c <= 'z') || c == '-'; });
}

// One or more labels, then `suffix`: the suffix starts at a label boundary.
bool isBelowSuffix(std::string_view host, std::string_view suffix) {
    if (suffix.size() < 2 || suffix.front() != '.' || host.size() <= suffix.size() ||
        !host.ends_with(suffix))
        return false;
    std::string_view labels = host.substr(0, host.size() - suffix.size());
    for (;;) {
        const std::size_t dot = labels.find('.');
        if (!isHostLabel(labels.substr(0, dot)))
            return false;
        if (dot == std::string_view::npos)
            return true;
        labels.remove_prefix(dot + 1);
    }
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

std::string assetUrlPrefix(std::string_view repository, const AppVersion& version,
                           const UpdateEndpoints& endpoints) {
    std::string out = origin(endpoints, endpoints.downloadHost) + "/";
    out += repository;
    out += "/releases/download/v" + version.toString() + "/";
    return out;
}

Error malformedRelease(std::string detail) {
    return makeError(ErrorCode::ParseError,
                     "GitHub's answer about the latest release is not valid.", std::move(detail));
}

Error untrustedRelease(std::string detail) {
    return makeError(ErrorCode::ValidationFailed,
                     "The latest release points to an address this application does not trust, "
                     "so it was ignored.",
                     std::move(detail));
}

// The one repository the reply's url and html_url both name, spelled as in
// html_url.
Result<std::string> releaseRepository(const Json& root, const AppVersion& version,
                                      const UpdateEndpoints& endpoints) {
    const auto api = root.find("url");
    const auto page = root.find("html_url");
    if (api == root.end() || !api->is_string() || page == root.end() || !page->is_string())
        return malformedRelease("url or html_url missing or not a string");
    const std::string tag = "v" + version.toString();
    const auto fromApi = repositoryPath(api->get_ref<const std::string&>(),
                                        origin(endpoints, endpoints.apiHost) + "/repos/");
    if (!fromApi || fromApi->rest.size() != 2 || fromApi->rest[0] != "releases" ||
        !isReleaseId(fromApi->rest[1]))
        return untrustedRelease("url is not the API address of a release");
    const auto fromPage =
        repositoryPath(page->get_ref<const std::string&>(), std::string(kWebOrigin) + "/");
    if (!fromPage ||
        fromPage->rest != std::vector<std::string_view>{"releases", "tag", std::string_view(tag)})
        return untrustedRelease("html_url is not the page of release " + tag);
    if (!sameRepository(fromApi->repository, fromPage->repository))
        return untrustedRelease("url and html_url name different repositories");
    std::string out(fromPage->repository.owner);
    out += '/';
    out += fromPage->repository.name;
    return out;
}

// The exact contract name in the assets list, once, with its exact URL.
Result<std::optional<ReleaseAsset>> pickAsset(const Json& assets, const std::string& name,
                                              const std::string& repository,
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
        if (text != assetUrlPrefix(repository, version, endpoints) + name ||
            !isAllowedAssetUrl(text, repository, version, endpoints))
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

std::string_view updateRepositoryId() noexcept {
    return kRepositoryId;
}

std::string latestReleaseApiUrl(const UpdateEndpoints& endpoints) {
    std::string out = origin(endpoints, endpoints.apiHost) + "/repositories/";
    out += kRepositoryId;
    out += "/releases/latest";
    return out;
}

std::string releasePageUrl(const std::optional<AppVersion>& version) {
    return releasePageUrl(kRepository, version);
}

std::string releasePageUrl(const ReleaseInfo& release) {
    return releasePageUrl(release.repository, release.version);
}

std::string releasePageUrl(std::string_view repository, const std::optional<AppVersion>& version) {
    std::string out(kWebOrigin);
    out += '/';
    out += splitRepository(repository) ? repository : kRepository; // else not from a reply
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

bool isAllowedAssetUrl(std::string_view url, std::string_view repository, const AppVersion& version,
                       const UpdateEndpoints& endpoints) {
    if (!splitRepository(repository))
        return false;
    const std::string prefix = assetUrlPrefix(repository, version, endpoints);
    if (url.size() <= prefix.size() || !url.starts_with(prefix))
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
    const std::string_view host = parts->host;
    return std::find(endpoints.redirectHosts.begin(), endpoints.redirectHosts.end(), host) !=
               endpoints.redirectHosts.end() ||
           std::any_of(endpoints.redirectHostSuffixes.begin(), endpoints.redirectHostSuffixes.end(),
                       [host](const std::string& suffix) { return isBelowSuffix(host, suffix); });
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

    auto repository = releaseRepository(root, *version, endpoints);
    if (!repository)
        return std::move(repository).error();

    ReleaseInfo release;
    release.version = *version;
    release.repository = std::move(repository).value();
    auto installer =
        pickAsset(*assets, installerAssetName(*version), release.repository, *version, endpoints);
    if (!installer)
        return std::move(installer).error();
    auto checksums = pickAsset(*assets, std::string(kChecksumAssetName), release.repository,
                               *version, endpoints);
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
