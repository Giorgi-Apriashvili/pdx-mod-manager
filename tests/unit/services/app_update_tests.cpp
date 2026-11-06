// The update check's pure logic against hostile input: versions, the release
// JSON, the checksum file and the address allow-list. No network, no Qt.

#include "core/sha256.hpp"
#include "services/app_update.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>

using namespace pmm;
using namespace pmm::services;

namespace {

const std::string kRepo = "Giorgi-Apriashvili/pdx-mod-manager";
const std::string kBase = "https://github.com/" + kRepo + "/releases/download/v1.2.3/";
const std::string kInstallerName = "ParadoxModManager-1.2.3-windows-x86_64-setup.exe";
const AppVersion kVersion{1, 2, 3};
const std::string kDigest(64, 'a');

nlohmann::json assetJson(const std::string& name, const std::string& url) {
    return {{"name", name}, {"browser_download_url", url}, {"size", 123}};
}

// As GitHub answers for a release of `repository`.
nlohmann::json validRelease(const std::string& repository = kRepo) {
    const std::string base = "https://github.com/" + repository + "/releases/download/v1.2.3/";
    return {{"url", "https://api.github.com/repos/" + repository + "/releases/261946542"},
            {"html_url", "https://github.com/" + repository + "/releases/tag/v1.2.3"},
            {"tag_name", "v1.2.3"},
            {"draft", false},
            {"prerelease", false},
            {"body", "<img src=\"https://evil.example/x.png\"> notes are never read"},
            {"assets",
             nlohmann::json::array(
                 {assetJson("ParadoxModManager-1.2.3-linux-x86_64.tar.gz", base + "linux.tar.gz"),
                  assetJson(kInstallerName, base + kInstallerName),
                  assetJson("SHA256SUMS.txt", base + "SHA256SUMS.txt")})}};
}

// The value of set(<name> <value>) in the root CMakeLists.txt.
std::string rootCMakeValue(const std::string& name) {
    std::ifstream file(std::filesystem::path(PMM_FIXTURES_DIR) / ".." / ".." / "CMakeLists.txt");
    REQUIRE(file);
    const std::string prefix = "set(" + name + " ";
    for (std::string line; std::getline(file, line);) {
        if (line.ends_with('\r'))
            line.pop_back();
        if (line.starts_with(prefix) && line.ends_with(')'))
            return line.substr(prefix.size(), line.size() - prefix.size() - 1);
    }
    FAIL("no " << prefix << "...) in CMakeLists.txt");
    return {};
}

Result<std::optional<ReleaseInfo>> parse(const nlohmann::json& json) {
    return parseLatestRelease(json.dump());
}

std::string checksumLine(const std::string& name, const std::string& digest = kDigest) {
    return digest + "  " + name;
}

} // namespace

TEST_CASE("Versions are strict semantic versions", "[services][update]") {
    CHECK(parseAppVersion("0.1.0") == AppVersion{0, 1, 0});
    CHECK(parseAppVersion("10.20.30") == AppVersion{10, 20, 30});
    CHECK(parseAppVersion("4294967295.0.0") == AppVersion{4294967295u, 0, 0});

    const std::string bad = GENERATE(
        as<std::string>{}, "", "1", "1.2", "1.2.3.4", "1.2.", ".1.2", "1..2", "01.2.3", "1.02.3",
        "1.2.03", "-1.2.3", "+1.2.3", "1.2.3-rc1", "1.2.3+build", " 1.2.3", "1.2.3 ", "1.2.3\n",
        "a.b.c", "1.2.x", "1,2,3", "4294967296.0.0", "99999999999999999999.0.0");
    INFO(bad);
    CHECK_FALSE(parseAppVersion(bad));
    CHECK_FALSE(parseAppVersion(std::string_view("1.2.3\0", 6)));
}

TEST_CASE("Version comparison is numeric", "[services][update]") {
    CHECK(AppVersion{0, 2, 0} > AppVersion{0, 1, 9});
    CHECK(AppVersion{0, 10, 0} > AppVersion{0, 9, 0});
    CHECK(AppVersion{1, 0, 0} > AppVersion{0, 99, 99});
    CHECK(AppVersion{0, 1, 1} > AppVersion{0, 1, 0});
    CHECK_FALSE(AppVersion{0, 1, 0} > AppVersion{0, 1, 0});
    CHECK(AppVersion{0, 1, 0} == AppVersion{0, 1, 0});
    CHECK(AppVersion{1, 2, 3}.toString() == "1.2.3");
}

TEST_CASE("Release tags are exactly v<x.y.z>", "[services][update]") {
    CHECK(parseReleaseTag("v1.2.3") == AppVersion{1, 2, 3});
    const std::string bad =
        GENERATE(as<std::string>{}, "", "v", "1.2.3", "V1.2.3", "vv1.2.3", "v1.2", "v1.2.3-rc1",
                 "v0.1.0-rc1", "v01.2.3", "release-1.2.3", " v1.2.3", "v1.2.3 ", "v1.2.3/");
    INFO(bad);
    CHECK_FALSE(parseReleaseTag(bad));
}

TEST_CASE("The check asks for the repository by its frozen id", "[services][update]") {
    // Every released copy asks exactly this; changing it strands them all.
    CHECK(rootCMakeValue("PMM_GITHUB_REPOSITORY_ID") == "1439071956");
    CHECK(updateRepositoryId() == rootCMakeValue("PMM_GITHUB_REPOSITORY_ID"));
    CHECK(latestReleaseApiUrl() ==
          "https://api.github.com/repositories/1439071956/releases/latest");
    // No name in it: whoever registers an old name gets no say.
    CHECK(latestReleaseApiUrl().find(updateRepository()) == std::string::npos);
}

TEST_CASE("The fixed addresses come from the repository slug", "[services][update]") {
    CHECK(updateRepository() == "Giorgi-Apriashvili/pdx-mod-manager");
    CHECK(releasePageUrl(kVersion) ==
          "https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/tag/v1.2.3");
    CHECK(releasePageUrl(std::nullopt) ==
          "https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases");
    CHECK(installerAssetName(kVersion) == kInstallerName);
    CHECK(updateInstallerArguments() ==
          std::vector<std::string>{"/SILENT", "/SP-", "/NORESTART", "/PMMUPDATE=1"});
}

TEST_CASE("Asset URLs must be in the release's own folder", "[services][update]") {
    CHECK(isAllowedAssetUrl(kBase + kInstallerName, kRepo, kVersion));
    CHECK(isAllowedAssetUrl(kBase + "SHA256SUMS.txt", kRepo, kVersion));
    // The repository the reply named, which a rename may have changed.
    CHECK(isAllowedAssetUrl(
        "https://github.com/New-Owner/renamed.app_2/releases/download/v1.2.3/a.exe",
        "New-Owner/renamed.app_2", kVersion));
    CHECK_FALSE(isAllowedAssetUrl(kBase + kInstallerName, "New-Owner/renamed.app_2", kVersion));

    // A repository that is not one: the URL cannot be in it.
    const std::string repository = GENERATE(
        as<std::string>{}, "", "Giorgi-Apriashvili", "/pdx-mod-manager", "Giorgi-Apriashvili/",
        "Giorgi-Apriashvili/pdx-mod-manager/releases", "../pdx-mod-manager",
        "Giorgi-Apriashvili/..", "Giorgi-Apriashvili/.", "-x/pdx-mod-manager");
    INFO(repository);
    CHECK_FALSE(
        isAllowedAssetUrl("https://github.com/" + repository + "/releases/download/v1.2.3/a.exe",
                          repository, kVersion));
}

TEST_CASE("Asset URLs outside the release's folder are refused", "[services][update]") {

    const std::string bad = GENERATE_COPY(
        as<std::string>{}, "", kBase,
        // Scheme.
        "http://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/download/v1.2.3/a.exe",
        "HTTPS://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/download/v1.2.3/a.exe",
        // Lookalike hosts.
        "https://github.com.evil.example/Giorgi-Apriashvili/pdx-mod-manager/releases/download/"
        "v1.2.3/a.exe",
        "https://evil-github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/download/v1.2.3/a.exe",
        "https://github.com@evil.example/Giorgi-Apriashvili/pdx-mod-manager/releases/download/"
        "v1.2.3/a.exe",
        "https://github.com:443/Giorgi-Apriashvili/pdx-mod-manager/releases/download/v1.2.3/a.exe",
        "https://api.github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/download/v1.2.3/a.exe",
        // Wrong repository, tag or folder.
        "https://github.com/Giorgi-Apriashvili/other/releases/download/v1.2.3/a.exe",
        "https://github.com/evil/pdx-mod-manager/releases/download/v1.2.3/a.exe",
        "https://github.com/giorgi-apriashvili/pdx-mod-manager/releases/download/v1.2.3/a.exe",
        "https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/download/v1.2.4/a.exe",
        "https://github.com/Giorgi-Apriashvili/pdx-mod-manager/archive/v1.2.3/a.exe",
        // Traversal, nesting and decoration behind a correct start.
        kBase + "../v1.2.4/a.exe", kBase + "..", kBase + ".", kBase + "..%2fa.exe",
        kBase + "%2e%2e/a.exe", kBase + "a/b.exe", kBase + "a\\b.exe", kBase + "a.exe?x=1",
        kBase + "a.exe#x", kBase + "a b.exe", kBase + "a.exe\n");
    INFO(bad);
    CHECK_FALSE(isAllowedAssetUrl(bad, kRepo, kVersion));
}

TEST_CASE("Redirect targets are https on the listed hosts only", "[services][update]") {
    CHECK(isAllowedRedirectTarget("https://github.com/x/y"));
    CHECK(isAllowedRedirectTarget("https://release-assets.githubusercontent.com/"
                                  "github-production-release-asset/1/2?sig=a%2Fb"));
    CHECK(isAllowedRedirectTarget("https://objects.githubusercontent.com/x"));

    const std::string bad = GENERATE(
        as<std::string>{}, "", "/relative", "github.com/x", "//github.com/x", "https://github.com",
        "https:/github.com/x", "https//github.com/x", "http://github.com/x", "ftp://github.com/x",
        "HTTPS://github.com/x", "file:///c:/x",
        // Not on the list, or only looking like it.
        "https://api.github.com/x", "https://evil.example/x", "https://github.com.evil.example/x",
        "https://evil-github.com/x", "https://github.community/x",
        "https://githubusercontent.com/x", "https://evil.githubusercontent.com/x",
        "https://objects.githubusercontent.com.evil.example/x", "https://GitHub.com/x",
        "https://github.com./x", "https://xn--github-5ya.com/x",
        // Authority tricks.
        "https://github.com@evil.example/x", "https://evil.example@github.com/x",
        "https://user:pw@github.com/x", "https://github.com:443/x", "https://github.com:8443/x",
        "https://github.com:/x", "https://github.com%2f@evil.example/x",
        "https://github.com\\@evil.example/x", "https://[::1]/x",
        "https://github.com#@evil.example/", "https://github.com?@evil.example/",
        // Characters that parsers disagree about.
        "https://github.com/x y", "https://github.com/x\ty", " https://github.com/x",
        "https://github.com/x#fragment", "https://github.com/\xc3\xa9");
    INFO(bad);
    CHECK_FALSE(isAllowedRedirectTarget(bad));
}

TEST_CASE("Test endpoints replace the hosts, the scheme and the port", "[services][update]") {
    UpdateEndpoints stub;
    stub.scheme = "http";
    stub.apiHost = "127.0.0.1";
    stub.downloadHost = "127.0.0.1";
    stub.redirectHosts = {"127.0.0.1"};
    stub.port = 8123;
    CHECK(latestReleaseApiUrl(stub) ==
          "http://127.0.0.1:8123/repositories/1439071956/releases/latest");
    CHECK(isAllowedAssetUrl(
        "http://127.0.0.1:8123/Giorgi-Apriashvili/pdx-mod-manager/releases/download/v1.2.3/a.exe",
        kRepo, kVersion, stub));
    CHECK(isAllowedRedirectTarget("http://127.0.0.1:8123/x", stub));
    CHECK_FALSE(isAllowedRedirectTarget("http://127.0.0.1:8124/x", stub));
    CHECK_FALSE(isAllowedRedirectTarget("http://127.0.0.1/x", stub));
    CHECK_FALSE(isAllowedRedirectTarget("http://localhost:8123/x", stub));
    CHECK_FALSE(isAllowedRedirectTarget("https://127.0.0.1:8123/x", stub));
    CHECK_FALSE(isAllowedRedirectTarget("https://github.com/x", stub));
}

TEST_CASE("A valid release gives the version and the two exact assets", "[services][update]") {
    const auto release = parse(validRelease());
    REQUIRE(release.ok());
    REQUIRE(release.value());
    const ReleaseInfo& info = *release.value();
    CHECK(info.version == kVersion);
    REQUIRE(info.installer);
    CHECK(info.installer->name == kInstallerName);
    CHECK(info.installer->url == kBase + kInstallerName);
    REQUIRE(info.checksums);
    CHECK(info.checksums->name == "SHA256SUMS.txt");
    CHECK(info.checksums->url == kBase + "SHA256SUMS.txt");
    CHECK(info.repository == kRepo);
    CHECK(releasePageUrl(info) == "https://github.com/" + kRepo + "/releases/tag/v1.2.3");
}

TEST_CASE("A renamed or transferred repository is followed", "[services][update]") {
    // The id still finds it; its reply names it as it is called now.
    const std::string renamed = "Someone-Else/renamed.manager_2";
    const auto release = parse(validRelease(renamed));
    REQUIRE(release.ok());
    REQUIRE(release.value());
    const ReleaseInfo& info = *release.value();
    CHECK(info.repository == renamed);
    REQUIRE(info.installer);
    CHECK(info.installer->url ==
          "https://github.com/" + renamed + "/releases/download/v1.2.3/" + kInstallerName);
    REQUIRE(info.checksums);
    CHECK(info.checksums->url ==
          "https://github.com/" + renamed + "/releases/download/v1.2.3/SHA256SUMS.txt");
    CHECK(releasePageUrl(info) == "https://github.com/" + renamed + "/releases/tag/v1.2.3");

    // Assets left behind in the old name's folder are not that release's.
    auto json = validRelease(renamed);
    json["assets"][1]["browser_download_url"] = kBase + kInstallerName;
    const auto stale = parse(json);
    REQUIRE_FALSE(stale.ok());
    CHECK(stale.error().code == ErrorCode::ValidationFailed);
}

TEST_CASE("The release page is built, never copied from the reply", "[services][update]") {
    ReleaseInfo info;
    info.version = kVersion;
    info.repository = "Other-Owner/other-name";
    CHECK(releasePageUrl(info) == "https://github.com/Other-Owner/other-name/releases/tag/v1.2.3");
    // Without a validated repository it cannot be built from one.
    const std::string bad = GENERATE(as<std::string>{}, "", "x", "a/b/c", "evil.example/x/y",
                                     "../x", "a/b?c", "a/b#c", "a/..");
    INFO(bad);
    info.repository = bad;
    CHECK(releasePageUrl(info) ==
          "https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/tag/v1.2.3");
}

TEST_CASE("The reply's url and html_url must name one repository", "[services][update]") {
    const std::string api = "https://api.github.com/repos/";
    const std::string page = "https://github.com/";
    const auto parseWith = [](const std::string& url, const std::string& htmlUrl) {
        auto json = validRelease();
        json["url"] = url;
        json["html_url"] = htmlUrl;
        return parse(json);
    };
    const std::string goodApi = api + kRepo + "/releases/261946542";
    const std::string goodPage = page + kRepo + "/releases/tag/v1.2.3";

    SECTION("names compare without case, and html_url spells the result") {
        const std::string upper = "GIORGI-APRIASHVILI/PDX-MOD-MANAGER";
        const std::string lower = "giorgi-apriashvili/pdx-mod-manager";
        for (const std::string& other : {upper, lower}) {
            const auto release = parseWith(api + other + "/releases/261946542", goodPage);
            REQUIRE(release.ok());
            REQUIRE(release.value());
            CHECK(release.value()->repository == kRepo);
            CHECK(release.value()->installer);
        }
        // The assets must then be spelled as html_url spells the repository.
        const auto release = parseWith(goodApi, page + upper + "/releases/tag/v1.2.3");
        REQUIRE_FALSE(release.ok());
        CHECK(release.error().code == ErrorCode::ValidationFailed);
        auto json = validRelease(upper);
        json["url"] = goodApi;
        const auto respelled = parse(json);
        REQUIRE(respelled.ok());
        REQUIRE(respelled.value());
        CHECK(respelled.value()->repository == upper);
        CHECK(respelled.value()->installer);
    }
    SECTION("anything else is refused") {
        const auto [url, htmlUrl] = GENERATE_COPY(table<std::string, std::string>({
            // Two different repositories.
            {api + "Giorgi-Apriashvili/other/releases/1", goodPage},
            {api + "Someone-Else/pdx-mod-manager/releases/1", goodPage},
            {goodApi, page + "Someone-Else/pdx-mod-manager/releases/tag/v1.2.3"},
            {api + "pdx-mod-manager/Giorgi-Apriashvili/releases/1", goodPage},
            {api + "Giorgi-Apriashvili/pdx-mod-manage/releases/1", goodPage},
            {api + "Giorgi-Apriashvili/pdx-mod-manager-/releases/1", goodPage},
            // Lookalike, other or decorated hosts.
            {"https://api.github.com.evil.example/repos/" + kRepo + "/releases/1", goodPage},
            {"https://evil-api.github.com/repos/" + kRepo + "/releases/1", goodPage},
            {"https://github.com/repos/" + kRepo + "/releases/1", goodPage},
            {"https://uploads.github.com/repos/" + kRepo + "/releases/1", goodPage},
            {goodApi, "https://github.com.evil.example/" + kRepo + "/releases/tag/v1.2.3"},
            {goodApi, "https://www.github.com/" + kRepo + "/releases/tag/v1.2.3"},
            {goodApi, "https://api.github.com/" + kRepo + "/releases/tag/v1.2.3"},
            {goodApi, "https://evil.example/" + kRepo + "/releases/tag/v1.2.3"},
            {"https://API.GITHUB.COM/repos/" + kRepo + "/releases/1", goodPage},
            {goodApi, "https://GitHub.com/" + kRepo + "/releases/tag/v1.2.3"},
            // Scheme, port, user information.
            {"http://api.github.com/repos/" + kRepo + "/releases/1", goodPage},
            {goodApi, "http://github.com/" + kRepo + "/releases/tag/v1.2.3"},
            {"HTTPS://api.github.com/repos/" + kRepo + "/releases/1", goodPage},
            {"https://api.github.com:443/repos/" + kRepo + "/releases/1", goodPage},
            {goodApi, "https://github.com:443/" + kRepo + "/releases/tag/v1.2.3"},
            {"https://user@api.github.com/repos/" + kRepo + "/releases/1", goodPage},
            {goodApi, "https://user:pw@github.com/" + kRepo + "/releases/tag/v1.2.3"},
            {"https://api.github.com@evil.example/repos/" + kRepo + "/releases/1", goodPage},
            {goodApi, "https://github.com@evil.example/" + kRepo + "/releases/tag/v1.2.3"},
            // Path tricks in url.
            {api + kRepo + "/releases/", goodPage},
            {api + kRepo + "/releases", goodPage},
            {api + kRepo + "/releases/1/", goodPage},
            {api + kRepo + "/releases/1/assets", goodPage},
            {api + kRepo + "/releases/abc", goodPage},
            {api + kRepo + "/releases/-1", goodPage},
            {api + kRepo + "/releases/1?x=1", goodPage},
            {api + kRepo + "/releases/1#x", goodPage},
            {api + kRepo + "/releases/latest", goodPage},
            {api + kRepo + "/releases/tags/v1.2.3", goodPage},
            {api + kRepo + "/releases/" + std::string(21, '1'), goodPage},
            {api + "/" + kRepo + "/releases/1", goodPage},
            {api + "Giorgi-Apriashvili//pdx-mod-manager/releases/1", goodPage},
            {api + "Giorgi-Apriashvili/../" + kRepo + "/releases/1", goodPage},
            {api + kRepo + "/../pdx-mod-manager/releases/1", goodPage},
            {api + "Giorgi-Apriashvili%2Fpdx-mod-manager/releases/1", goodPage},
            {api + "Giorgi-Apriashvili/pdx-mod-manager%20/releases/1", goodPage},
            {api + "Giorgi-Apriashvili\\pdx-mod-manager/releases/1", goodPage},
            {"https://api.github.com/repositories/1439071956/releases/1", goodPage},
            {"https://api.github.com/repos/" + kRepo, goodPage},
            {" " + goodApi, goodPage},
            {goodApi + " ", goodPage},
            {goodApi + "\n", goodPage},
            // Path tricks in html_url, or another release's page.
            {goodApi, page + kRepo + "/releases/tag/v1.2.4"},
            {goodApi, page + kRepo + "/releases/tag/1.2.3"},
            {goodApi, page + kRepo + "/releases/tag/v1.2.3/"},
            {goodApi, page + kRepo + "/releases/tag/v1.2.3?x=1"},
            {goodApi, page + kRepo + "/releases/tag/v1.2.3#x"},
            {goodApi, page + kRepo + "/releases/tag/v01.2.3"},
            {goodApi, page + kRepo + "/releases/tag/v1.2.3-rc1"},
            {goodApi, page + kRepo + "/releases/latest"},
            {goodApi, page + kRepo + "/releases"},
            {goodApi, page + kRepo + "/tree/v1.2.3"},
            {goodApi, page + kRepo + "/releases/download/v1.2.3"},
            {goodApi, page + kRepo + "/releases/tag/../tag/v1.2.3"},
            {goodApi, page + "Giorgi-Apriashvili/./pdx-mod-manager/releases/tag/v1.2.3"},
            {goodApi, page + "/" + kRepo + "/releases/tag/v1.2.3"},
            // Names outside GitHub's grammar, alike on both sides.
            {api + "-Giorgi/x/releases/1", page + "-Giorgi/x/releases/tag/v1.2.3"},
            {api + "Giorgi-/x/releases/1", page + "Giorgi-/x/releases/tag/v1.2.3"},
            {api + "Gi--orgi/x/releases/1", page + "Gi--orgi/x/releases/tag/v1.2.3"},
            {api + "Gi_orgi/x/releases/1", page + "Gi_orgi/x/releases/tag/v1.2.3"},
            {api + "Gi.orgi/x/releases/1", page + "Gi.orgi/x/releases/tag/v1.2.3"},
            {api + std::string(40, 'a') + "/x/releases/1",
             page + std::string(40, 'a') + "/x/releases/tag/v1.2.3"},
            {api + "a/" + std::string(101, 'x') + "/releases/1",
             page + "a/" + std::string(101, 'x') + "/releases/tag/v1.2.3"},
            {api + "a/./releases/1", page + "a/./releases/tag/v1.2.3"},
            {api + "a/../releases/1", page + "a/../releases/tag/v1.2.3"},
            {api + "a/.../releases/1", page + "a/.../releases/tag/v1.2.3"},
            {api + "a/b c/releases/1", page + "a/b c/releases/tag/v1.2.3"},
            {api + "a/b+c/releases/1", page + "a/b+c/releases/tag/v1.2.3"},
            {api + "a/\xc3\xa9/releases/1", page + "a/\xc3\xa9/releases/tag/v1.2.3"},
            {api + "a/b;c/releases/1", page + "a/b;c/releases/tag/v1.2.3"},
            {api + "/x/releases/1", page + "/x/releases/tag/v1.2.3"},
            {api + "a//releases/1", page + "a//releases/tag/v1.2.3"},
            {"", goodPage},
            {goodApi, ""},
        }));
        INFO("url: " << url << "\nhtml_url: " << htmlUrl);
        const auto release = parseWith(url, htmlUrl);
        REQUIRE_FALSE(release.ok());
        CHECK(release.error().code == ErrorCode::ValidationFailed);
    }
    SECTION("missing or of the wrong type") {
        using Corrupt = std::function<void(nlohmann::json&)>;
        const auto corrupt = GENERATE(
            as<Corrupt>{}, +[](nlohmann::json& j) { j.erase("url"); },
            +[](nlohmann::json& j) { j.erase("html_url"); },
            +[](nlohmann::json& j) {
                j.erase("url");
                j.erase("html_url");
            },
            +[](nlohmann::json& j) { j["url"] = nullptr; },
            +[](nlohmann::json& j) { j["url"] = 261946542; },
            +[](nlohmann::json& j) { j["html_url"] = nlohmann::json::array({"x"}); },
            +[](nlohmann::json& j) { j["html_url"] = nlohmann::json::object(); },
            +[](nlohmann::json& j) { j["html_url"] = false; });
        auto json = validRelease();
        corrupt(json);
        INFO(json.dump());
        const auto release = parse(json);
        REQUIRE_FALSE(release.ok());
        CHECK(release.error().code == ErrorCode::ParseError);
    }
}

TEST_CASE("Draft and prerelease items are ignored", "[services][update]") {
    const char* flag = GENERATE("draft", "prerelease");
    auto json = validRelease();
    json[flag] = true;
    // Their tag does not have to be a release version: they are not looked at.
    json["tag_name"] = "v0.1.0-rc1";
    const auto release = parse(json);
    REQUIRE(release.ok());
    CHECK_FALSE(release.value());
}

TEST_CASE("A release without the expected assets has none of them", "[services][update]") {
    auto json = validRelease();
    json["assets"] = nlohmann::json::array();
    auto release = parse(json);
    REQUIRE(release.ok());
    REQUIRE(release.value());
    CHECK_FALSE(release.value()->installer);
    CHECK_FALSE(release.value()->checksums);

    // Almost the right names: other files of the release, whatever their URL.
    json["assets"] = nlohmann::json::array(
        {assetJson("../" + kInstallerName, "https://evil.example/a.exe"),
         assetJson(kInstallerName + ".exe", "https://evil.example/a.exe"),
         assetJson("ParadoxModManager-1.2.4-windows-x86_64-setup.exe",
                   "https://evil.example/a.exe"),
         assetJson("paradoxmodmanager-1.2.3-windows-x86_64-setup.exe",
                   "https://evil.example/a.exe"),
         assetJson("sha256sums.txt", "https://evil.example/a.txt"),
         assetJson("SHA256SUMS.txt/../SHA256SUMS.txt", "https://evil.example/a.txt")});
    release = parse(json);
    REQUIRE(release.ok());
    REQUIRE(release.value());
    CHECK_FALSE(release.value()->installer);
    CHECK_FALSE(release.value()->checksums);
}

TEST_CASE("An expected asset with a foreign URL rejects the release", "[services][update]") {
    const std::string url = GENERATE_COPY(
        as<std::string>{},
        // A correct start that does not name the asset itself.
        kBase + "other.exe", kBase + "../v1.2.4/" + kInstallerName,
        "http://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/download/v1.2.3/" +
            kInstallerName,
        "https://github.com.evil.example/Giorgi-Apriashvili/pdx-mod-manager/releases/download/"
        "v1.2.3/" +
            kInstallerName,
        "https://github.com/evil/pdx-mod-manager/releases/download/v1.2.3/" + kInstallerName,
        "https://github.com/Giorgi-Apriashvili/pdx-mod-manager/releases/download/v1.2.2/" +
            kInstallerName,
        "https://release-assets.githubusercontent.com/" + kInstallerName, "");
    INFO(url);
    auto json = validRelease();
    json["assets"][1]["browser_download_url"] = url;
    const auto release = parse(json);
    REQUIRE_FALSE(release.ok());
    CHECK(release.error().code == ErrorCode::ValidationFailed);
}

TEST_CASE("Malformed release replies are errors", "[services][update]") {
    SECTION("not JSON") {
        const std::string text = GENERATE(as<std::string>{}, "", "null", "[]", "1", "\"x\"", "{",
                                          "<html>rate limited</html>", "{\"tag_name\":\"v1.2.3\"");
        INFO(text);
        CHECK_FALSE(parseLatestRelease(text).ok());
    }
    SECTION("wrong or missing fields") {
        using Corrupt = std::function<void(nlohmann::json&)>;
        const auto corrupt = GENERATE(
            as<Corrupt>{}, +[](nlohmann::json& j) { j.erase("tag_name"); },
            +[](nlohmann::json& j) { j["tag_name"] = 123; },
            +[](nlohmann::json& j) { j["tag_name"] = nullptr; },
            +[](nlohmann::json& j) { j["tag_name"] = nlohmann::json::array({"v1.2.3"}); },
            +[](nlohmann::json& j) { j["tag_name"] = "v0.1.0-rc1"; },
            +[](nlohmann::json& j) { j["tag_name"] = "1.2.3"; },
            +[](nlohmann::json& j) { j.erase("draft"); },
            +[](nlohmann::json& j) { j["draft"] = "false"; },
            +[](nlohmann::json& j) { j["draft"] = 0; },
            +[](nlohmann::json& j) { j.erase("prerelease"); },
            +[](nlohmann::json& j) { j["prerelease"] = nullptr; },
            +[](nlohmann::json& j) { j.erase("assets"); },
            +[](nlohmann::json& j) { j["assets"] = "none"; },
            +[](nlohmann::json& j) { j["assets"] = nlohmann::json::object(); },
            +[](nlohmann::json& j) { j["assets"].push_back(1); },
            +[](nlohmann::json& j) { j["assets"].push_back(nullptr); },
            +[](nlohmann::json& j) { j["assets"].push_back({{"browser_download_url", "x"}}); },
            +[](nlohmann::json& j) { j["assets"].push_back({{"name", 5}}); },
            +[](nlohmann::json& j) { j["assets"][1].erase("browser_download_url"); },
            +[](nlohmann::json& j) { j["assets"][1]["browser_download_url"] = 5; },
            // The same name twice: which one would be downloaded?
            +[](nlohmann::json& j) { j["assets"].push_back(j["assets"][1]); });
        auto json = validRelease();
        corrupt(json);
        INFO(json.dump());
        CHECK_FALSE(parse(json).ok());
    }
    SECTION("oversized") {
        auto json = validRelease();
        json["body"] = std::string(kMaxReleaseJsonBytes, 'x');
        const auto release = parse(json);
        REQUIRE_FALSE(release.ok());
        CHECK(release.error().code == ErrorCode::LimitExceeded);
    }
    SECTION("nested beyond reason") {
        // No stack overflow while parsing or destroying the value.
        std::string deep(kMaxReleaseJsonBytes / 2, '[');
        deep += std::string(kMaxReleaseJsonBytes / 2, ']');
        CHECK_FALSE(parseLatestRelease(deep).ok());
    }
}

TEST_CASE("Checksum files are parsed strictly", "[services][update]") {
    const std::string other(64, 'b');
    SECTION("valid, with or without the final line end") {
        const std::string text =
            checksumLine(kInstallerName) + "\n" + checksumLine("SHA-2_x+y.tar.gz", other);
        for (const std::string& file : {text, text + "\n"}) {
            const auto entries = parseChecksumFile(file);
            REQUIRE(entries.ok());
            REQUIRE(entries.value().size() == 2);
            CHECK(entries.value()[0].sha256 == kDigest);
            CHECK(entries.value()[0].fileName == kInstallerName);
            CHECK(entries.value()[1].sha256 == other);
            CHECK(entries.value()[1].fileName == "SHA-2_x+y.tar.gz");
        }
    }
    SECTION("finding the installer") {
        const std::string text = checksumLine("a.zip", other) + "\n" + checksumLine(kInstallerName);
        const auto found = findChecksum(text, kInstallerName);
        REQUIRE(found.ok());
        CHECK(found.value() == kDigest);
        const auto missing = findChecksum(text, "b.zip");
        REQUIRE_FALSE(missing.ok());
        CHECK(missing.error().code == ErrorCode::NotFound);
        CHECK_FALSE(findChecksum("garbage", kInstallerName).ok());
    }
    SECTION("anything else rejects the whole file") {
        const std::string good = checksumLine("a.zip");
        const std::string upper(64, 'A');
        const std::string bad =
            GENERATE_COPY(as<std::string>{}, "", "\n", "\n\n", good + "\n\n", "\n" + good,
                          good + "\n" + good,                // the same name twice
                          good + "\r\n",                     // CRLF
                          std::string(63, 'a') + "  a.zip",  // short digest
                          std::string(65, 'a') + "  a.zip",  // long digest
                          std::string(63, 'a') + "g  a.zip", // not hex
                          upper + "  a.zip",                 // uppercase
                          std::string(32, 'a') + std::string(32, 'A') + "  a.zip",
                          kDigest + " a.zip",   // one space
                          kDigest + "   a.zip", // three spaces
                          kDigest + " *a.zip",  // binary mode marker
                          kDigest + "  *a.zip", kDigest + "\ta.zip", kDigest + "  ", kDigest,
                          " " + good, good + " ",
                          kDigest + "  ../a.zip", // traversal
                          kDigest + "  ..", kDigest + "  .", kDigest + "  a/b.zip",
                          kDigest + "  a\\b.zip", kDigest + "  a..zip", kDigest + "  /a.zip",
                          kDigest + "  C:a.zip", kDigest + "  a b.zip", kDigest + "  a.zip\t",
                          "# comment\n" + good, kDigest + "  \xc3\xa9.zip");
        INFO(bad);
        CHECK_FALSE(parseChecksumFile(bad).ok());
    }
    SECTION("oversized") {
        std::string big;
        for (int i = 0; big.size() <= kMaxChecksumFileBytes; ++i)
            big += checksumLine("file" + std::to_string(i) + ".zip") + "\n";
        const auto result = parseChecksumFile(big);
        CHECK_FALSE(result.ok());
    }
}

TEST_CASE("An installed copy is told by the uninstaller's place", "[services][update]") {
    namespace fs = std::filesystem;
    static std::atomic<unsigned> counter{0};
    const fs::path root =
        fs::temp_directory_path() /
        ("pmm-update-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         std::to_string(counter++));
    fs::create_directories(root / "bin");
    CHECK_FALSE(hasInstallerLayout(root / "bin"));
    // Next to the program is not where the contract puts it.
    std::ofstream(root / "bin" / "unins000.exe") << "x";
    CHECK_FALSE(hasInstallerLayout(root / "bin"));
    std::ofstream(root / "unins000.exe") << "x";
    CHECK(hasInstallerLayout(root / "bin"));
    // A folder of that name is not an uninstaller.
    fs::remove(root / "unins000.exe");
    fs::create_directories(root / "unins000.exe");
    CHECK_FALSE(hasInstallerLayout(root / "bin"));
    std::error_code ec;
    fs::remove_all(root, ec);
}
