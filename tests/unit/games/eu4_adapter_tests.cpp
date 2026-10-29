// EU4 adapter against fixtures modelled on the real EU4 v1.37.5.0 Steam
// installation of the development machine (read-only observations, see
// docs/evidence/real-scan-2026-10-29.md): "eu4" with Workshop content on disk,
// "eu4-reinstalled" with the state right after the game was reinstalled
// (registered Workshop items whose content Steam has not downloaded yet).

#include "core/import_plan.hpp"
#include "core/log.hpp"
#include "core/readiness.hpp"
#include "games/eu4.hpp"
#include "games/stellaris.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

using namespace pmm;
using namespace pmm::games;
using test::findMod;
using test::samePath;
using test::u8path;

namespace {

constexpr const char* kContent = "Steam/steamapps/workshop/content/236850/";

struct Eu4Fixture : test::GameFixture {
    Eu4Adapter adapter;

    explicit Eu4Fixture(std::string_view fixture = "eu4") : GameFixture(fixture) {}

    fs::path userData() const {
        return path("Documents/Paradox Interactive/Europa Universalis IV");
    }
    fs::path install() const { return path("Steam/steamapps/common/Europa Universalis IV"); }

    GameInstallation discovered() const {
        auto found = adapter.discover(env);
        REQUIRE(found.size() == 1);
        return found.front();
    }
    std::vector<InstalledMod> scan(const GameInstallation& inst) const {
        auto mods = adapter.scanMods(inst);
        REQUIRE(mods.ok());
        return std::move(mods).value();
    }
    const InstalledMod& mod(const std::vector<InstalledMod>& mods, std::string_view id) const {
        const InstalledMod* found = findMod(mods, id);
        REQUIRE(found != nullptr);
        return *found;
    }
};

} // namespace

TEST_CASE("EU4 is discovered through the injected Steam environment", "[games][eu4]") {
    Eu4Fixture f;
    const GameInstallation inst = f.discovered();
    CHECK(inst.game == "eu4");
    CHECK(samePath(inst.installPath, f.install()));
    CHECK(samePath(inst.userDataPath, f.userData()));
    CHECK(samePath(inst.executablePath, f.install() / "eu4.exe"));
    // exeArgs is ["-enabletelemetry", "-gdpr-compliant"]: telemetry is never
    // enabled on the user's behalf.
    CHECK(inst.defaultArguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(inst.distribution == DistributionSource::Steam);
    CHECK(inst.steamAppId == "236850");
    CHECK(inst.version == "v1.37.5.0");
    CHECK(inst.versionLabel == "EU4 v1.37.5.0 Inca (491d)");

    CHECK(StellarisAdapter().discover(f.env).empty());
    CHECK(StellarisAdapter().inspectInstallation(f.install(), {}, f.env).error().code ==
          ErrorCode::WrongGame);
}

TEST_CASE("EU4 default locations per OS", "[games][eu4]") {
    Eu4Adapter adapter;
    DiscoveryEnvironment env;
    env.documentsDir = u8path("/docs");
    env.xdgDataHome = u8path("/xdg");
    env.platform = OsPlatform::Windows;
    CHECK(adapter.defaultUserDataPath(env) ==
          u8path("/docs/Paradox Interactive/Europa Universalis IV"));
    CHECK(adapter.defaultExecutable(OsPlatform::Windows) == u8path("eu4.exe"));
    env.platform = OsPlatform::MacOS;
    CHECK(adapter.defaultUserDataPath(env) ==
          u8path("/docs/Paradox Interactive/Europa Universalis IV"));
    CHECK(adapter.defaultExecutable(OsPlatform::MacOS) == u8path("eu4.app/Contents/MacOS/eu4"));
    env.platform = OsPlatform::Linux;
    CHECK(adapter.defaultUserDataPath(env) ==
          u8path("/xdg/Paradox Interactive/Europa Universalis IV"));
    CHECK(adapter.defaultExecutable(OsPlatform::Linux) == u8path("eu4"));
    env.xdgDataHome.clear();
    CHECK(adapter.defaultUserDataPath(env).empty());

    GameInstallation inst;
    inst.platform = OsPlatform::Linux;
    const ProcessNames names = adapter.processNames(inst);
    CHECK(names.game == std::vector<std::string>{"eu4"});
    CHECK(names.conflictingWriters == std::vector<std::string>{"dowser", "paradox-launcher"});
    CHECK_FALSE(adapter.loadOrderInfo().verifiedInGame);
}

TEST_CASE("EU4 scan understands archives and relative paths", "[games][eu4]") {
    Eu4Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    std::vector<std::string> keys;
    for (const InstalledMod& m : mods)
        keys.push_back(m.identity.source + ":" + m.identity.id);
    CHECK(keys == std::vector<std::string>{"local:local_mod", "local:zipped_local",
                                           "steam-workshop:2000000001", "steam-workshop:2000000002",
                                           "steam-workshop:2000000003"});

    const InstalledMod& local = f.mod(mods, "local_mod");
    CHECK(local.state == InstallState::Ready);
    CHECK(local.descriptor.remoteFileId == "2000000099"); // local copy, stays local
    CHECK(samePath(local.contentPath, f.userData() / "mod" / "local_mod"));

    const InstalledMod& zippedLocal = f.mod(mods, "zipped_local");
    CHECK(zippedLocal.state == InstallState::Ready);
    CHECK(samePath(zippedLocal.contentPath, f.userData() / "mod" / "zipped_local.zip"));

    const InstalledMod& legacy = f.mod(mods, "2000000002");
    CHECK(legacy.state == InstallState::Ready);
    CHECK(legacy.problems.empty());
    CHECK(
        samePath(legacy.contentPath, f.path(std::string(kContent) + "2000000002/legacy_mod.zip")));
    CHECK(legacy.descriptor.name == "Example Legacy Zipped Mod");
    CHECK(legacy.contentUpdatedAt == 1500000000);

    const InstalledMod& unregistered = f.mod(mods, "2000000003");
    CHECK(unregistered.state == InstallState::Ready);
    CHECK(unregistered.descriptorRef == "mod/ugc_2000000003.mod");

    // A legacy zipped item that was never registered cannot be described.
    test::writeText(f.path(std::string(kContent) + "2000000004/old.zip"), "PK placeholder");
    const std::vector<InstalledMod> withLegacy = f.scan(inst);
    const InstalledMod& orphan = f.mod(withLegacy, "2000000004");
    CHECK(orphan.state == InstallState::Broken);
    CHECK(test::containsText(orphan.problems, "legacy (zipped)"));
    CHECK(samePath(orphan.contentPath, f.path(std::string(kContent) + "2000000004/old.zip")));
}

TEST_CASE("EU4 current configuration and launch plan", "[games][eu4]") {
    Eu4Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    auto entries = f.adapter.readCurrentConfiguration(inst, mods);
    REQUIRE(entries.ok());
    REQUIRE(entries->size() == 3);
    CHECK((*entries)[0].identity == ModIdentity{"eu4", "steam-workshop", "2000000002"});
    CHECK((*entries)[1].identity == ModIdentity{"eu4", "local", "local_mod"});
    CHECK((*entries)[2].identity == ModIdentity{"eu4", "steam-workshop", "2000000001"});

    std::vector<InstalledMod> ordered{f.mod(mods, "2000000002"), f.mod(mods, "local_mod"),
                                      f.mod(mods, "zipped_local"), f.mod(mods, "2000000003")};
    auto plan = f.adapter.buildLaunchPlan(inst, ordered);
    REQUIRE(plan.ok());
    CHECK(plan->arguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(plan->environment == std::vector<std::pair<std::string, std::string>>{
                                   {"SteamAppId", "236850"}, {"SteamGameId", "236850"}});
    REQUIRE(plan->configWrites.size() == 2);
    CHECK(samePath(plan->configWrites[0].target, f.userData() / "mod" / "ugc_2000000003.mod"));
    CHECK(plan->configWrites[1].contents ==
          "{\"enabled_mods\":[\"mod/ugc_2000000002.mod\",\"mod/local_mod.mod\","
          "\"mod/zipped_local.mod\",\"mod/ugc_2000000003.mod\"],"
          "\"disabled_dlcs\":[\"dlc/dlc001_example/dlc001.dlc\"]}");

    // Without its registration a zipped item is registered with archive=.
    fs::remove(f.userData() / "mod" / "ugc_2000000002.mod");
    auto regenerated = f.adapter.buildLaunchPlan(inst, ordered);
    REQUIRE(regenerated.ok());
    REQUIRE(regenerated->configWrites.size() == 3);
    CHECK(regenerated->configWrites[0].contents ==
          "tags={\n\t\"Gameplay\"\n}\n"
          "name=\"Example Legacy Zipped Mod\"\n"
          "supported_version=\"1.30.*\"\n"
          "archive=\"" +
              f.generic(std::string(kContent) + "2000000002/legacy_mod.zip") +
              "\"\n"
              "remote_file_id=\"2000000002\"");
}

TEST_CASE("EU4 compatibility warnings are lenient about wildcards", "[games][eu4]") {
    Eu4Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    CHECK(f.adapter.compatibilityWarnings(inst, f.mod(mods, "2000000001")).empty());   // v1.37.*.*
    CHECK(f.adapter.compatibilityWarnings(inst, f.mod(mods, "local_mod")).empty());    // v1.37.5.0
    CHECK(f.adapter.compatibilityWarnings(inst, f.mod(mods, "zipped_local")).empty()); // v1.37.*
    CHECK(f.adapter.compatibilityWarnings(inst, f.mod(mods, "2000000003")) ==
          std::vector<std::string>{
              "Made for Europa Universalis IV 1.34.5; the installed version is v1.37.5.0."});
}

TEST_CASE("userdir.txt is read conservatively", "[games][eu4]") {
    test::TempDir temp;
    const fs::path file = temp.path() / "userdir.txt";
    const fs::path target = temp.path() / u8path("Ünïcødé data/EU4 user");
    const std::string absolute = platform::toGenericUtf8(target);
    const auto read = [&](std::string_view text) {
        test::writeText(file, text);
        return readUserDirFile(file);
    };

    auto missing = readUserDirFile(file);
    REQUIRE(missing.ok());
    CHECK_FALSE(*missing);
    for (const std::string_view blank : {"", " \r\n\t\n", "\"\"", "\xEF\xBB\xBF  \r\n"}) {
        auto none = read(blank);
        REQUIRE(none.ok());
        CHECK_FALSE(*none);
    }

    // First non-empty line; BOM, CRLF, whitespace, one pair of quotes and a
    // trailing separator are tolerated.
    for (const std::string& text :
         {absolute, "\xEF\xBB\xBF\r\n  \t\r\n  \"" + absolute + "/\"  \r\nsecond line\r\n",
          "'" + absolute + "'", "  " + absolute + "  "}) {
        auto found = read(text);
        REQUIRE(found.ok());
        REQUIRE(*found);
        CHECK(samePath(**found, target));
        CHECK((*found)->has_filename());
    }

    // Relative paths, other encodings and control characters are ignored.
    for (const std::string& text :
         {std::string("Custom Data/EU4"), std::string("./user"), std::string("..\\user"),
          std::string("\xFF\xFE") + absolute, absolute + "\x01", "\"" + absolute + "\x7f\""}) {
        auto ignored = read(text);
        REQUIRE_FALSE(ignored.ok());
        CHECK(ignored.error().code == ErrorCode::ValidationFailed);
        CHECK(ignored.error().message.find("userdir.txt") != std::string::npos);
    }
}

TEST_CASE("EU4 honours an absolute path in userdir.txt", "[games][eu4]") {
    Eu4Fixture f;
    const fs::path userDirFile = f.install() / "userdir.txt";
    const fs::path custom = f.path("Custom Data/EU4 dätä");
    fs::create_directories(custom);

    // The game ships the file empty: the default folder is used.
    CHECK(test::readText(userDirFile).empty());
    CHECK(samePath(f.discovered().userDataPath, f.userData()));

    test::writeText(userDirFile, "\"" + f.generic("Custom Data/EU4 dätä") + "/\"\r\n");
    CHECK(samePath(f.discovered().userDataPath, custom));
    auto manual = f.adapter.inspectInstallation(f.install(), {}, f.env);
    REQUIRE(manual.ok());
    CHECK(samePath(manual->userDataPath, custom));
    // A folder chosen by the user still wins.
    auto chosen = f.adapter.inspectInstallation(f.install(), f.userData(), f.env);
    REQUIRE(chosen.ok());
    CHECK(samePath(chosen->userDataPath, f.userData()));
    // Scanning and the launch plan follow the relocated folder.
    const GameInstallation relocated = f.discovered();
    CHECK(f.scan(relocated).size() == 3); // only the Workshop content folders
    auto plan = f.adapter.buildLaunchPlan(relocated, {});
    REQUIRE(plan.ok());
    REQUIRE(plan->configWrites.size() == 1);
    CHECK(samePath(plan->configWrites[0].target, custom / "dlc_load.json"));

    // Relative paths are not supported: the default folder is used.
    test::writeText(userDirFile, "Custom Data/EU4 dätä");
    CHECK(samePath(f.discovered().userDataPath, f.userData()));
}

TEST_CASE("Launch arguments never enable the game's telemetry", "[games][eu4]") {
    Eu4Fixture f;
    GameInstallation inst = f.discovered();
    CHECK(inst.defaultArguments == std::vector<std::string>{"-gdpr-compliant"});
    auto manual = f.adapter.inspectInstallation(f.install(), {}, f.env);
    REQUIRE(manual.ok());
    CHECK(manual->defaultArguments == std::vector<std::string>{"-gdpr-compliant"});

    // An installation stored before the flag was dropped: filtered at launch,
    // case-insensitively, other arguments kept in order.
    inst.defaultArguments = {"-EnableTelemetry", "-gdpr-compliant", "-debug_mode",
                             "-enabletelemetry"};
    auto plan = f.adapter.buildLaunchPlan(inst, {});
    REQUIRE(plan.ok());
    CHECK(plan->arguments == std::vector<std::string>{"-gdpr-compliant", "-debug_mode"});
    CHECK(f.adapter.launchArguments({"-enabletelemetryfoo"}) ==
          std::vector<std::string>{"-enabletelemetryfoo"});
}

TEST_CASE("EU4 after a reinstall: registered Workshop items without content are not installed",
          "[games][eu4]") {
    Eu4Fixture f("eu4-reinstalled");
    std::vector<LogRecord> logged;
    setLogSink([&](const LogRecord& record) { logged.push_back(record); });

    // StateFlags 4 with BytesToDownload > 0 is a complete installation.
    const GameInstallation inst = f.discovered();
    CHECK(inst.version == "v1.37.5.0");
    CHECK(inst.defaultArguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(samePath(inst.userDataPath, f.userData())); // userdir.txt is empty
    CHECK_FALSE(fs::exists(inst.installPath / "steam_appid.txt"));

    // Only the local mods are installed; the one without its folder is Broken.
    const std::vector<InstalledMod> mods = f.scan(inst);
    std::vector<std::string> keys;
    for (const InstalledMod& m : mods)
        keys.push_back(m.identity.source + ":" + m.identity.id);
    CHECK(keys == std::vector<std::string>{"local:local_balance", "local:missing_local"});
    const InstalledMod& local = f.mod(mods, "local_balance");
    CHECK(local.state == InstallState::Ready);
    CHECK(local.descriptor.remoteFileId == "3000000099");
    const InstalledMod& missingLocal = f.mod(mods, "missing_local");
    CHECK(missingLocal.state == InstallState::Broken);
    CHECK(test::containsText(missingLocal.problems, "does not exist"));

    // The registrations stay visible in the log for diagnostics.
    int notes = 0;
    for (const LogRecord& record : logged) {
        if (record.message.find("is not downloaded") == std::string::npos)
            continue;
        ++notes;
        for (const auto& [key, value] : record.fields) {
            if (key == "id" && value == "3000000001")
                CHECK(test::containsText({formatLogRecord(record)}, "download pending"));
            if (key == "id" && value == "3000000003")
                CHECK(test::containsText({formatLogRecord(record)}, "not in Steam's"));
        }
    }
    CHECK(notes == 4); // 3000000001, ...02, ...03 and the unreadable ...05
    setLogSink({});

    // dlc_load.json keeps Workshop identities for items that are not installed.
    auto entries = f.adapter.readCurrentConfiguration(inst, mods);
    REQUIRE(entries.ok());
    REQUIRE(entries->size() == 6);
    const auto& e = *entries;
    CHECK(e[0].identity == ModIdentity{"eu4", "steam-workshop", "3000000001"});
    CHECK(e[0].nameHint == "Example Font");
    CHECK(e[0].versionHint == "1.37");
    CHECK(e[1].identity == ModIdentity{"eu4", "steam-workshop", "3000000002"});
    CHECK(e[1].nameHint == "Example Water");
    CHECK(e[2].identity == ModIdentity{"eu4", "local", "local_balance"});
    CHECK(e[2].nameHint == "Local Balance");
    // No descriptor at all: the launcher's ugc_<id>.mod naming convention.
    CHECK(e[3].identity == ModIdentity{"eu4", "steam-workshop", "3000000004"});
    CHECK(e[3].nameHint == "mod/ugc_3000000004.mod");
    CHECK(e[4].identity == ModIdentity{"eu4", "local", "missing_local"});
    // Unreadable registration named ugc_<id>.mod.
    CHECK(e[5].identity == ModIdentity{"eu4", "steam-workshop", "3000000005"});

    // Imported as a playset, the Workshop items read as missing and are
    // acquired; the Broken local mod is not.
    InstalledModMap inventory;
    for (const InstalledMod& m : mods)
        inventory.emplace(m.identity, m);
    CHECK(computeReadiness(e[0], nullptr, nullptr).status == EntryStatus::Missing);
    const ImportPlan import =
        buildImportPlan("eu4", "Imported from game", e, inventory, {}, inst.version, {});
    CHECK(import.toAcquire ==
          std::vector<ModIdentity>{e[0].identity, e[1].identity, e[3].identity, e[5].identity});
    CHECK(import.counts.installed == 1);
    CHECK(import.counts.broken == 1);

    // The installed part can be launched; telemetry stays off and the Steam
    // environment replaces the missing steam_appid.txt.
    auto launch = f.adapter.buildLaunchPlan(inst, std::vector<InstalledMod>{local});
    REQUIRE(launch.ok());
    CHECK(launch->arguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(launch->environment == std::vector<std::pair<std::string, std::string>>{
                                     {"SteamAppId", "236850"}, {"SteamGameId", "236850"}});
    REQUIRE(launch->configWrites.size() == 1);
    CHECK(launch->configWrites[0].contents ==
          "{\"enabled_mods\":[\"mod/local_balance.mod\"],\"disabled_dlcs\":[]}");
}

TEST_CASE("EU4 Workshop items become installed once Steam has downloaded them", "[games][eu4]") {
    Eu4Fixture f("eu4-reinstalled");
    const GameInstallation inst = f.discovered();
    const std::string folder = std::string(kContent) + "3000000001";
    test::writeText(f.path(folder + "/descriptor.mod"),
                    "version=\"1.37\"\ntags={\n\t\"Fixes\"\n\t\"Graphics\"\n\t\"Utilities\"\n}\n"
                    "name=\"Example Font\"\nsupported_version=\"v1.37.*.*\"\n"
                    "remote_file_id=\"3000000001\"");

    // Content arriving while Steam still lists the item as not installed.
    const std::vector<InstalledMod> arriving = f.scan(inst);
    CHECK(f.mod(arriving, "3000000001").state == InstallState::Downloading);

    const fs::path acf = f.path("Steam/steamapps/workshop/appworkshop_236850.acf");
    test::writeText(acf, test::replaceAll(test::readText(acf), "\t\"WorkshopItemDetails\"",
                                          "\t\"WorkshopItemsInstalled\"\n\t{\n\t\t\"3000000001\"\n"
                                          "\t\t{\n\t\t\t\"size\"\t\t\"2821267\"\n"
                                          "\t\t\t\"timeupdated\"\t\t\"1715404933\"\n"
                                          "\t\t\t\"manifest\"\t\t\"8055649841122585510\"\n"
                                          "\t\t}\n\t}\n\t\"WorkshopItemDetails\""));
    const std::vector<InstalledMod> mods = f.scan(inst);
    const InstalledMod& font = f.mod(mods, "3000000001");
    CHECK(font.state == InstallState::Ready);
    CHECK(font.problems.empty());
    CHECK(samePath(font.descriptorPath, f.userData() / "mod" / "ugc_3000000001.mod"));
    CHECK(samePath(font.contentPath, f.path(folder)));
    CHECK(font.contentUpdatedAt == 1715404933);
    CHECK(f.adapter.compatibilityWarnings(inst, font).empty()); // v1.37.*.*
    CHECK(findMod(mods, "3000000002") == nullptr);

    // The launcher's registration is reused byte for byte: only the order is
    // written.
    auto plan = f.adapter.buildLaunchPlan(inst, std::vector<InstalledMod>{font});
    REQUIRE(plan.ok());
    REQUIRE(plan->configWrites.size() == 1);
    CHECK(plan->configWrites[0].contents ==
          "{\"enabled_mods\":[\"mod/ugc_3000000001.mod\"],\"disabled_dlcs\":[]}");
}
