#include "persistence/installation_repository.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace pmm;
using namespace pmm::persistence;
using namespace pmm::persistence::test;

namespace {

GameInstallation stellaris() {
    GameInstallation g;
    g.game = "stellaris";
    g.installPath = fs::path(u8"C:/Program Files (x86)/Steam/steamapps/common/Stellaris");
    g.userDataPath =
        fs::path(u8"C:/Users/Ünïcødé User/Documents/Paradox Interactive/Stellaris 日本語");
    g.executablePath = g.installPath / "stellaris.exe";
    g.defaultArguments = {"-gdpr-compliant", "--flag with spaces"};
    g.distribution = DistributionSource::Steam;
    g.platform = OsPlatform::Windows;
    g.steamAppId = "281990";
    g.steamLibraryPath = fs::path(u8"C:/Program Files (x86)/Steam");
    g.version = "v4.5.2";
    g.versionLabel = "Cygnus v4.5.2 (9776)";
    g.manuallyConfigured = false;
    return g;
}

void checkEqual(const GameInstallation& a, const GameInstallation& b) {
    CHECK(a.game == b.game);
    CHECK(a.installPath == b.installPath);
    CHECK(a.userDataPath == b.userDataPath);
    CHECK(a.executablePath == b.executablePath);
    CHECK(a.defaultArguments == b.defaultArguments);
    CHECK(a.distribution == b.distribution);
    CHECK(a.platform == b.platform);
    CHECK(a.steamAppId == b.steamAppId);
    CHECK(a.steamLibraryPath == b.steamLibraryPath);
    CHECK(a.version == b.version);
    CHECK(a.versionLabel == b.versionLabel);
    CHECK(a.manuallyConfigured == b.manuallyConfigured);
}

} // namespace

TEST_CASE("Installations round-trip every field", "[persistence][installations]") {
    auto db = openMemory();
    InstallationRepository repo(*db);
    CHECK_FALSE(require(repo.get("stellaris")).has_value());

    const auto original = stellaris();
    requireOk(repo.upsert(original));
    auto loaded = require(repo.get("stellaris"));
    REQUIRE(loaded.has_value());
    checkEqual(*loaded, original);
}

TEST_CASE("Upserting replaces an installation and list is ordered by game",
          "[persistence][installations]") {
    auto db = openMemory();
    InstallationRepository repo(*db);
    requireOk(repo.upsert(stellaris()));

    GameInstallation eu4;
    eu4.game = "eu4";
    eu4.installPath = fs::path(u8"/home/example/Games/Europa Universalis IV");
    eu4.userDataPath = fs::path(u8"/home/example/.local/share/Paradox Interactive/Europa "
                                u8"Universalis IV");
    eu4.executablePath = eu4.installPath / "eu4";
    eu4.distribution = DistributionSource::Manual;
    eu4.platform = OsPlatform::Linux;
    eu4.manuallyConfigured = true;
    requireOk(repo.upsert(eu4));

    auto changed = stellaris();
    changed.version = "v4.6.0";
    changed.defaultArguments.clear();
    changed.distribution = DistributionSource::Unknown;
    changed.platform = OsPlatform::MacOS;
    requireOk(repo.upsert(changed));

    auto all = require(repo.list());
    REQUIRE(all.size() == 2);
    checkEqual(all[0], eu4);
    checkEqual(all[1], changed);

    requireOk(repo.remove("eu4"));
    requireOk(repo.remove("eu4"));
    CHECK(require(repo.list()).size() == 1);
}

TEST_CASE("Installations require a game key", "[persistence][installations]") {
    auto db = openMemory();
    auto status = InstallationRepository(*db).upsert(GameInstallation{});
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().code == ErrorCode::InvalidArgument);
}

#if defined(_WIN32)
TEST_CASE("An installation path that is not valid UTF-16 is rejected instead of throwing",
          "[persistence][installations]") {
    auto db = openMemory();
    InstallationRepository repo(*db);
    const auto original = stellaris();
    requireOk(repo.upsert(original));

    auto changed = original;
    changed.userDataPath = loneSurrogatePath(L"C:/Users/Example/Documents/Stellaris", L"");
    Status status;
    REQUIRE_NOTHROW(status = repo.upsert(changed));
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().code == ErrorCode::InvalidArgument);
    CHECK(status.error().detail.find("Stellaris\xEF\xBF\xBD") != std::string::npos);

    auto loaded = require(repo.get("stellaris"));
    REQUIRE(loaded.has_value());
    checkEqual(*loaded, original);
}
#endif
