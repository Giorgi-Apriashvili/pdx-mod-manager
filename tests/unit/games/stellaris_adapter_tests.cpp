#include "games/eu4.hpp"
#include "games/stellaris.hpp"
#include "platform/steam.hpp"
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

constexpr const char* kContent = "Steam/steamapps/workshop/content/281990/";

struct StellarisFixture : test::GameFixture {
    StellarisAdapter adapter;

    StellarisFixture() : GameFixture("stellaris") {
        // Local mod with a non-ASCII name and an old-style relative path.
        test::writeText(userData() / u8path("mod/Ünïcødé mod.mod"),
                        "name=\"Ünïcødé Local\"\nsupported_version=\"v4.5.*\"\n"
                        "path=\"mod/Ünïcødé mod\"");
        test::writeText(userData() / u8path("mod/Ünïcødé mod/descriptor.mod"),
                        "name=\"Ünïcødé Local\"");
    }

    fs::path userData() const { return path("Documents/Paradox Interactive/Stellaris"); }
    fs::path install() const { return path("Steam/steamapps/common/Stellaris"); }
    fs::path content(std::string_view id) const { return path(kContent + std::string(id)); }

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
    LaunchPlan plan(const GameInstallation& inst, const std::vector<InstalledMod>& mods,
                    std::initializer_list<std::string_view> ids) const {
        std::vector<InstalledMod> ordered;
        for (const std::string_view id : ids)
            ordered.push_back(mod(mods, id));
        auto result = adapter.buildLaunchPlan(inst, ordered);
        REQUIRE(result.ok());
        return std::move(result).value();
    }
};

void apply(const LaunchPlan& plan) {
    for (const ConfigWrite& write : plan.configWrites)
        test::writeText(write.target, write.contents);
}

const ConfigWrite* findWrite(const LaunchPlan& plan, const fs::path& target) {
    for (const ConfigWrite& write : plan.configWrites) {
        if (samePath(write.target, target))
            return &write;
    }
    return nullptr;
}

} // namespace

TEST_CASE("Stellaris is discovered through the injected Steam environment", "[games][stellaris]") {
    StellarisFixture f;
    const GameInstallation inst = f.discovered();
    CHECK(inst.game == "stellaris");
    CHECK(samePath(inst.installPath, f.install()));
    CHECK(samePath(inst.userDataPath, f.userData()));
    CHECK(samePath(inst.executablePath, f.install() / "stellaris.exe"));
    CHECK(inst.defaultArguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(inst.distribution == DistributionSource::Steam);
    CHECK(inst.steamAppId == "281990");
    CHECK(samePath(inst.steamLibraryPath, f.path("Steam")));
    CHECK(inst.version == "v4.5.2");
    CHECK(inst.versionLabel == "Cygnus v4.5.2 (9776)");
    CHECK(inst.platform == f.env.platform);
    CHECK_FALSE(inst.manuallyConfigured);

    Capabilities caps = f.adapter.capabilities(inst);
    CHECK(caps.directLaunch);
    CHECK(caps.localMods);
    CHECK_FALSE(caps.workshopAcquisition); // no Steam API library in the fixture
    test::writeText(f.install() / u8path(platform::steam::steamApiLibraryName(inst.platform)),
                    "placeholder, never loaded");
    CHECK(f.adapter.capabilities(inst).workshopAcquisition);
}

TEST_CASE("Stellaris discovery handles missing or partial installations", "[games][stellaris]") {
    StellarisFixture f;
    SECTION("no Steam") {
        f.env.steamRoot.reset();
        CHECK(f.adapter.discover(f.env).empty());
    }
    SECTION("not fully installed") {
        const fs::path acf = f.path("Steam/steamapps/appmanifest_281990.acf");
        test::writeText(acf, test::replaceAll(test::readText(acf), "\"StateFlags\"\t\t\"4\"",
                                              "\"StateFlags\"\t\t\"2\""));
        CHECK(f.adapter.discover(f.env).empty());
    }
    SECTION("no app manifest") {
        fs::remove(f.path("Steam/steamapps/appmanifest_281990.acf"));
        CHECK(f.adapter.discover(f.env).empty());
    }
    SECTION("another game's adapter finds nothing") {
        CHECK(Eu4Adapter().discover(f.env).empty());
    }
}

TEST_CASE("Stellaris discovery falls back without launcher-settings.json", "[games][stellaris]") {
    StellarisFixture f;
    fs::remove(f.install() / "launcher-settings.json");

    SECTION("Windows default executable") {
        f.env.platform = OsPlatform::Windows;
        const GameInstallation inst = f.discovered();
        CHECK(samePath(inst.executablePath, f.install() / "stellaris.exe"));
        CHECK(inst.defaultArguments.empty());
        CHECK(inst.version.empty());
        CHECK(samePath(inst.userDataPath, f.userData()));
    }
    SECTION("Linux uses XDG data and the native executable name") {
        f.env.platform = OsPlatform::Linux;
        f.env.xdgDataHome = f.path("xdg data");
        const GameInstallation inst = f.discovered();
        CHECK(samePath(inst.executablePath, f.install() / "stellaris"));
        CHECK(samePath(inst.userDataPath, f.path("xdg data/Paradox Interactive/Stellaris")));
        CHECK_FALSE(f.adapter.capabilities(inst).directLaunch); // placeholder missing
        CHECK(f.adapter.buildLaunchPlan(inst, {}).error().code == ErrorCode::NotFound);
        test::writeText(f.install() / "stellaris", "placeholder");
        CHECK(f.adapter.capabilities(inst).directLaunch);
    }
    SECTION("macOS app bundle") {
        f.env.platform = OsPlatform::MacOS;
        const GameInstallation inst = f.discovered();
        CHECK(samePath(inst.executablePath,
                       f.install() / "stellaris.app" / "Contents" / "MacOS" / "stellaris"));
    }
}

TEST_CASE("gameDataPath maps onto XDG data on Linux", "[games][stellaris]") {
    StellarisFixture f;
    f.env.platform = OsPlatform::Linux;
    f.env.xdgDataHome = f.path("xdg data");
    const GameInstallation inst = f.discovered();
    CHECK(samePath(inst.userDataPath, f.path("xdg data/Paradox Interactive/Stellaris")));
    // exePath "./stellaris.exe" is a regular file inside the folder; Linux has
    // no extension rule.
    CHECK(samePath(inst.executablePath, f.install() / "stellaris.exe"));
}

TEST_CASE("Unsafe launcher-settings executables are ignored", "[games][stellaris]") {
    StellarisFixture f;
    f.env.platform = OsPlatform::Windows;
    const fs::path settings = f.install() / "launcher-settings.json";
    const std::string original = test::readText(settings);

    SECTION("outside the installation") {
        test::writeText(f.path("Steam/steamapps/evil.exe"), "placeholder");
        test::writeText(settings, test::replaceAll(original, "./stellaris.exe", "../../evil.exe"));
    }
    SECTION("the Paradox launcher itself") {
        test::writeText(f.install() / "dowser.exe", "placeholder");
        test::writeText(settings, test::replaceAll(original, "./stellaris.exe", "./dowser.exe"));
    }
    SECTION("not an executable") {
        test::writeText(f.install() / "start.cmd", "placeholder");
        test::writeText(settings, test::replaceAll(original, "./stellaris.exe", "./start.cmd"));
    }
    const GameInstallation inst = f.discovered();
    CHECK(samePath(inst.executablePath, f.install() / "stellaris.exe"));
    CHECK(inst.defaultArguments.empty()); // arguments belong to the rejected entry
    CHECK(inst.version == "v4.5.2");
}

TEST_CASE("Stellaris drops -enabletelemetry and honours userdir.txt", "[games][stellaris]") {
    StellarisFixture f;
    const fs::path settings = f.install() / "launcher-settings.json";
    test::writeText(settings, test::replaceAll(test::readText(settings), "\"-gdpr-compliant\"",
                                               "\"-ENABLETELEMETRY\", \"-gdpr-compliant\""));
    CHECK(f.discovered().defaultArguments == std::vector<std::string>{"-gdpr-compliant"});

    // No userdir.txt ships with Stellaris; an absolute one is honoured.
    CHECK_FALSE(fs::exists(f.install() / "userdir.txt"));
    const fs::path custom = f.path("Stellaris Data");
    fs::create_directories(custom);
    test::writeText(f.install() / "userdir.txt", f.generic("Stellaris Data") + "\n");
    CHECK(samePath(f.discovered().userDataPath, custom));
}

TEST_CASE("Manual Stellaris locations are inspected", "[games][stellaris]") {
    StellarisFixture f;
    const DiscoveryEnvironment& env = f.env;

    SECTION("inside a Steam library") {
        auto inst = f.adapter.inspectInstallation(f.install(), {}, env);
        REQUIRE(inst.ok());
        CHECK(inst->distribution == DistributionSource::Steam);
        CHECK(inst->steamAppId == "281990");
        CHECK(samePath(inst->steamLibraryPath, f.path("Steam")));
        CHECK(inst->manuallyConfigured);
        CHECK(samePath(inst->userDataPath, f.userData()));
    }
    SECTION("outside any Steam library, with a chosen user data folder") {
        const fs::path manual = f.path("Games/Stellaris Manual");
        fs::create_directories(manual);
        fs::copy(f.install(), manual, fs::copy_options::recursive);
        const fs::path data = f.path("Custom Data/Stellaris");
        fs::create_directories(data);
        auto inst = f.adapter.inspectInstallation(manual, data, env);
        REQUIRE(inst.ok());
        CHECK(inst->distribution == DistributionSource::Manual);
        CHECK(inst->steamAppId.empty());
        CHECK(inst->steamLibraryPath.empty());
        CHECK(samePath(inst->userDataPath, data));
        CHECK(samePath(inst->executablePath, manual / "stellaris.exe"));
        CHECK(inst->version == "v4.5.2");
    }
    SECTION("errors") {
        CHECK(f.adapter.inspectInstallation(f.path("nope"), {}, env).error().code ==
              ErrorCode::NotFound);
        fs::create_directories(f.path("Empty"));
        CHECK(f.adapter.inspectInstallation(f.path("Empty"), {}, env).error().code ==
              ErrorCode::NotFound);
        CHECK(
            f.adapter.inspectInstallation(f.install(), f.path("missing data"), env).error().code ==
            ErrorCode::NotFound);
        CHECK(Eu4Adapter().inspectInstallation(f.install(), {}, env).error().code ==
              ErrorCode::WrongGame);
        test::writeText(f.install() / "launcher-settings.json", "{ broken");
        CHECK(f.adapter.inspectInstallation(f.install(), {}, env).error().code ==
              ErrorCode::ParseError);
    }
}

TEST_CASE("Stellaris scan applies the identity rules", "[games][stellaris]") {
    StellarisFixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    std::vector<std::string> keys;
    for (const InstalledMod& m : mods)
        keys.push_back(m.identity.source + ":" + m.identity.id);
    // 1000000008 and 1000000009 are registered but not downloaded: not installed.
    CHECK(keys == std::vector<std::string>{
                      "local:bad_syntax", "local:local_tweaks", "local:missing_folder",
                      "local:Ünïcødé mod", "steam-workshop:1000000001", "steam-workshop:1000000002",
                      "steam-workshop:1000000003", "steam-workshop:1000000004",
                      "steam-workshop:1000000005", "steam-workshop:1000000006"});
    for (const InstalledMod& m : mods)
        CHECK(m.identity.game == "stellaris");

    // A developer's local copy keeps its remote_file_id but stays local.
    const InstalledMod& local = f.mod(mods, "local_tweaks");
    CHECK(local.identity.source == source::Local);
    CHECK(local.state == InstallState::Ready);
    CHECK(local.problems.empty());
    CHECK(local.descriptor.remoteFileId == "1000000007");
    CHECK(local.descriptorRef == "mod/local_tweaks.mod");
    CHECK(samePath(local.descriptorPath, f.userData() / "mod" / "local_tweaks.mod"));
    CHECK(samePath(local.contentPath, f.userData() / "mod" / "local_tweaks"));
    CHECK(local.contentUpdatedAt > 0);

    const InstalledMod& unicode = f.mod(mods, "Ünïcødé mod");
    CHECK(unicode.state == InstallState::Ready);
    CHECK(unicode.descriptor.name == "Ünïcødé Local");
    CHECK(samePath(unicode.contentPath, f.userData() / u8path("mod/Ünïcødé mod")));
    CHECK(unicode.descriptorRef == "mod/Ünïcødé mod.mod");

    const InstalledMod& registered = f.mod(mods, "1000000001");
    CHECK(registered.state == InstallState::Ready);
    CHECK(registered.problems.empty());
    CHECK(registered.descriptorRef == "mod/ugc_1000000001.mod");
    CHECK(samePath(registered.descriptorPath, f.userData() / "mod" / "ugc_1000000001.mod"));
    CHECK(samePath(registered.contentPath, f.content("1000000001")));
    CHECK(registered.descriptor.name == "Example Overhaul");
    CHECK(registered.descriptor.tags == std::vector<std::string>{"Fixes", "Graphics", "Overhaul"});
    CHECK(registered.contentUpdatedAt == 1782489192);

    // Registered with a stale library path: usable, rewritten at launch.
    const InstalledMod& stale = f.mod(mods, "1000000002");
    CHECK(stale.state == InstallState::Ready);
    CHECK(samePath(stale.contentPath, f.content("1000000002")));
    REQUIRE(stale.problems.size() == 1);
    CHECK(stale.problems[0].find("points to another location") != std::string::npos);
    CHECK(stale.descriptor.dependencies ==
          std::vector<std::string>{"Example Overhaul", "Missing Dependency"});

    // Not registered: descriptor from the content folder, registered at launch.
    const InstalledMod& unregistered = f.mod(mods, "1000000003");
    CHECK(unregistered.state == InstallState::Ready);
    CHECK(unregistered.descriptorPath.empty());
    CHECK(unregistered.descriptorRef == "mod/ugc_1000000003.mod");
    CHECK(unregistered.descriptor.name == "Example Battles");
    CHECK(unregistered.descriptor.extra ==
          std::vector<std::pair<std::string, std::string>>{
              {"replace_path", "common/example_battles"}, {"user_list{}", "\"alpha\" \"beta\""}});

    const InstalledMod& secondLibrary = f.mod(mods, "1000000006");
    CHECK(secondLibrary.state == InstallState::Ready);
    CHECK(samePath(secondLibrary.contentPath,
                   f.path("Second Library/steamapps/workshop/content/281990/1000000006")));
    CHECK(secondLibrary.descriptorRef == "mod/ugc_1000000006.mod");
    CHECK(secondLibrary.contentUpdatedAt > 0); // folder time: not in any Workshop manifest
}

TEST_CASE("Stellaris scan reports broken and pending mods per mod", "[games][stellaris]") {
    StellarisFixture f;
    const std::vector<InstalledMod> mods = f.scan(f.discovered());

    const InstalledMod& badSyntax = f.mod(mods, "bad_syntax");
    CHECK(badSyntax.state == InstallState::Broken);
    REQUIRE(badSyntax.problems.size() == 1);
    CHECK(badSyntax.problems[0].find("bad_syntax.mod") != std::string::npos);
    CHECK(badSyntax.descriptorRef == "mod/bad_syntax.mod");

    const InstalledMod& missingFolder = f.mod(mods, "missing_folder");
    CHECK(missingFolder.state == InstallState::Broken);
    CHECK(test::containsText(missingFolder.problems, "does not exist"));

    const InstalledMod& badContent = f.mod(mods, "1000000004");
    CHECK(badContent.state == InstallState::Broken);
    CHECK(test::containsText(badContent.problems, "line 4"));

    const InstalledMod& outdated = f.mod(mods, "1000000005");
    CHECK(outdated.state == InstallState::NeedsUpdate);

    // Registered Workshop items whose content is in no library are not
    // installed (whether Steam is downloading them or not): they read as
    // missing in playsets and can be acquired.
    CHECK(findMod(mods, "1000000008") == nullptr);
    CHECK(findMod(mods, "1000000009") == nullptr);

    // Content on disk that Steam does not list as installed yet: an update.
    test::writeText(f.content("1000000009") / "descriptor.mod", "name=\"Example Downloading\"");
    const std::vector<InstalledMod> updating = f.scan(f.discovered());
    const InstalledMod& downloading = f.mod(updating, "1000000009");
    CHECK(downloading.state == InstallState::Downloading);
    CHECK(downloading.problems.empty());
    CHECK(findMod(updating, "1000000008") == nullptr);
}

TEST_CASE("Stellaris scan handles unusual registrations", "[games][stellaris]") {
    StellarisFixture f;
    const GameInstallation inst = f.discovered();
    const fs::path modDir = f.userData() / "mod";

    // Workshop item copied to a custom folder: ugc_<id>.mod + remote_file_id.
    test::writeText(f.path("Custom Content/Thing/descriptor.mod"), "name=\"Custom\"");
    test::writeText(modDir / "ugc_1000000077.mod", "name=\"Custom\"\npath=\"" +
                                                       f.generic("Custom Content/Thing") +
                                                       "\"\nremote_file_id=\"1000000077\"");
    // ugc_ name whose remote_file_id does not match: local.
    test::writeText(modDir / "ugc_1000000078.mod", "name=\"Impostor\"\npath=\"" +
                                                       f.generic("Custom Content/Thing") +
                                                       "\"\nremote_file_id=\"1\"");
    // Second registration of an installed Workshop item.
    test::writeText(modDir / "another_name.mod",
                    "name=\"Alias\"\npath=\"" + f.generic(std::string(kContent) + "1000000001") +
                        "\"");

    const std::vector<InstalledMod> mods = f.scan(inst);
    const InstalledMod& custom = f.mod(mods, "1000000077");
    CHECK(custom.identity.source == source::SteamWorkshop);
    CHECK(custom.state == InstallState::Ready);
    CHECK(custom.problems.empty());
    CHECK(samePath(custom.contentPath, f.path("Custom Content/Thing")));

    const InstalledMod& impostor = f.mod(mods, "ugc_1000000078");
    CHECK(impostor.identity.source == source::Local);

    const InstalledMod& aliased = f.mod(mods, "1000000001");
    CHECK(aliased.descriptorRef == "mod/ugc_1000000001.mod");
    CHECK(aliased.problems == std::vector<std::string>{"Also registered as mod/another_name.mod."});
    CHECK(findMod(mods, "another_name") == nullptr);

    // Deterministic.
    const std::vector<InstalledMod> again = f.scan(inst);
    REQUIRE(again.size() == mods.size());
    for (std::size_t i = 0; i < mods.size(); ++i) {
        CHECK(again[i].identity == mods[i].identity);
        CHECK(again[i].state == mods[i].state);
        CHECK(again[i].problems == mods[i].problems);
    }

    // No user mod folder: Workshop content is still found.
    fs::remove_all(modDir);
    const std::vector<InstalledMod> contentOnly = f.scan(inst);
    CHECK(contentOnly.size() == 6); // 1000000001..1000000005 plus the second library's
    for (const InstalledMod& m : contentOnly) {
        CHECK(m.identity.source == source::SteamWorkshop);
        CHECK(m.descriptorPath.empty());
    }

    GameInstallation noUserData = inst;
    noUserData.userDataPath.clear();
    CHECK(f.adapter.scanMods(noUserData).error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("Stellaris current configuration is read from dlc_load.json", "[games][stellaris]") {
    StellarisFixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    auto entries = f.adapter.readCurrentConfiguration(inst, mods);
    REQUIRE(entries.ok());
    REQUIRE(entries->size() == 4);
    const auto& e = *entries;
    CHECK(e[0].identity == ModIdentity{"stellaris", "steam-workshop", "1000000001"});
    CHECK(e[0].nameHint == "Example Overhaul");
    CHECK(e[0].versionHint == "4.5.*");
    CHECK(e[1].identity == ModIdentity{"stellaris", "local", "local_tweaks"});
    CHECK(e[2].identity == ModIdentity{"stellaris", "steam-workshop", "1000000002"});
    CHECK(e[3].identity == ModIdentity{"stellaris", "local", "not_installed"});
    CHECK(e[3].nameHint == "mod/not_installed.mod");
    for (const PlaysetEntry& entry : e)
        CHECK(entry.enabled);

    const fs::path dlcLoad = f.userData() / "dlc_load.json";
    SECTION("aliases, duplicates and odd values") {
        test::writeText(f.userData() / "mod" / "another_name.mod",
                        "name=\"Alias\"\npath=\"" +
                            f.generic(std::string(kContent) + "1000000001") + "\"");
        test::writeText(dlcLoad, "{\"enabled_mods\":[\"mod/another_name.mod\",5,"
                                 "\"mod\\\\local_tweaks.mod\",\"mod/ugc_1000000001.mod\","
                                 "\"mod/LOCAL_TWEAKS.MOD\"]}");
        auto aliased = f.adapter.readCurrentConfiguration(inst, mods);
        REQUIRE(aliased.ok());
        REQUIRE(aliased->size() == 2);
        CHECK((*aliased)[0].identity.id == "1000000001");
        CHECK((*aliased)[1].identity.id == "local_tweaks");
    }
    SECTION("unresolved entries keep their identity and a readable name hint") {
        auto unresolved = f.adapter.readCurrentConfiguration(inst, {});
        REQUIRE(unresolved.ok());
        REQUIRE(unresolved->size() == 4);
        CHECK((*unresolved)[1].identity == ModIdentity{"stellaris", "local", "local_tweaks"});
        CHECK((*unresolved)[1].nameHint == "Local Tweaks");
        CHECK((*unresolved)[0].identity ==
              ModIdentity{"stellaris", "steam-workshop", "1000000001"});
        CHECK((*unresolved)[0].nameHint == "Example Overhaul");
    }
    SECTION("registered Workshop items that are not downloaded stay Workshop entries") {
        test::writeText(dlcLoad, "{\"enabled_mods\":[\"mod/ugc_1000000008.mod\","
                                 "\"mod/ugc_1000000009.mod\",\"mod/ugc_1000000077.mod\","
                                 "\"mod/ugc_01.mod\"]}");
        auto missing = f.adapter.readCurrentConfiguration(inst, mods);
        REQUIRE(missing.ok());
        REQUIRE(missing->size() == 4);
        CHECK((*missing)[0].identity == ModIdentity{"stellaris", "steam-workshop", "1000000008"});
        CHECK((*missing)[0].nameHint == "Example Unsubscribed");
        CHECK((*missing)[1].identity == ModIdentity{"stellaris", "steam-workshop", "1000000009"});
        // No descriptor: the ugc_<id>.mod naming convention, canonical ids only.
        CHECK((*missing)[2].identity == ModIdentity{"stellaris", "steam-workshop", "1000000077"});
        CHECK((*missing)[2].nameHint == "mod/ugc_1000000077.mod");
        CHECK((*missing)[3].identity == ModIdentity{"stellaris", "local", "ugc_01"});
    }
    SECTION("missing, empty and malformed files") {
        fs::remove(dlcLoad);
        CHECK(f.adapter.readCurrentConfiguration(inst, mods)->empty());
        test::writeText(dlcLoad, "{\"disabled_dlcs\":[]}");
        CHECK(f.adapter.readCurrentConfiguration(inst, mods)->empty());
        test::writeText(dlcLoad, "{\"enabled_mods\":\"mod/x.mod\"}");
        CHECK(f.adapter.readCurrentConfiguration(inst, mods).error().code == ErrorCode::ParseError);
        test::writeText(dlcLoad, "{\"enabled_mods\":[");
        CHECK(f.adapter.readCurrentConfiguration(inst, mods).error().code == ErrorCode::ParseError);
    }
}

TEST_CASE("Stellaris launch plan writes the minimum configuration", "[games][stellaris]") {
    StellarisFixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    const LaunchPlan plan = f.plan(
        inst, mods, {"1000000001", "local_tweaks", "1000000002", "1000000003", "1000000006"});
    CHECK(plan.game == "stellaris");
    CHECK(samePath(plan.executable, f.install() / "stellaris.exe"));
    CHECK(samePath(plan.workingDirectory, f.install()));
    CHECK(plan.arguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(plan.environment == std::vector<std::pair<std::string, std::string>>{
                                  {"SteamAppId", "281990"}, {"SteamGameId", "281990"}});
    CHECK(plan.warnings.empty());

    // Descriptors first (stale, unregistered, second library), then the order.
    REQUIRE(plan.configWrites.size() == 4);
    const fs::path modDir = f.userData() / "mod";
    CHECK(samePath(plan.configWrites[0].target, modDir / "ugc_1000000002.mod"));
    CHECK(samePath(plan.configWrites[1].target, modDir / "ugc_1000000003.mod"));
    CHECK(samePath(plan.configWrites[2].target, modDir / "ugc_1000000006.mod"));
    CHECK(samePath(plan.configWrites[3].target, f.userData() / "dlc_load.json"));
    CHECK(plan.configWrites[3].purpose == "load order (dlc_load.json)");

    // Unrelated keys and DLC choices are preserved, compact like the game writes it.
    CHECK(plan.configWrites[3].contents ==
          "{\"enabled_mods\":[\"mod/ugc_1000000001.mod\",\"mod/local_tweaks.mod\","
          "\"mod/ugc_1000000002.mod\",\"mod/ugc_1000000003.mod\",\"mod/ugc_1000000006.mod\"],"
          "\"disabled_dlcs\":[\"dlc/dlc004_example/dlc004.dlc\"],"
          "\"pmm_fixture_extra\":{\"keep\":[1,2]}}");

    // The author's stale path is replaced by the actual content folder.
    CHECK(plan.configWrites[1].contents == "version=\"2.6\"\n"
                                           "name=\"Example Battles\"\n"
                                           "tags={\n\t\"Gameplay\"\n\t\"Graphics\"\n}\n"
                                           "picture=\"thumbnail.png\"\n"
                                           "supported_version=\"v4.*.*\"\n"
                                           "replace_path=\"common/example_battles\"\n"
                                           "user_list={\n\t\"alpha\"\n\t\"beta\"\n}\n"
                                           "path=\"" +
                                               f.generic(std::string(kContent) + "1000000003") +
                                               "\"\n"
                                               "remote_file_id=\"1000000003\"");
    CHECK(plan.configWrites[0].contents.find(
              "path=\"" + f.generic(std::string(kContent) + "1000000002") + "\"") !=
          std::string::npos);
    CHECK(plan.configWrites[0].contents.find("C:/Users/Author") == std::string::npos);
    CHECK(plan.configWrites[2].contents.find(
              "path=\"" + f.generic("Second Library/steamapps/workshop/content/281990/1000000006") +
              "\"") != std::string::npos);

    // Once applied, the same plan needs no writes at all.
    apply(plan);
    const LaunchPlan again = f.plan(
        inst, mods, {"1000000001", "local_tweaks", "1000000002", "1000000003", "1000000006"});
    CHECK(again.configWrites.empty());
    // And a rescan sees the generated registrations.
    const std::vector<InstalledMod> rescanned = f.scan(inst);
    CHECK(f.mod(rescanned, "1000000002").problems.empty());
    CHECK(samePath(f.mod(rescanned, "1000000003").descriptorPath, modDir / "ugc_1000000003.mod"));
    const LaunchPlan afterRescan = f.plan(
        inst, rescanned, {"1000000001", "local_tweaks", "1000000002", "1000000003", "1000000006"});
    CHECK(afterRescan.configWrites.empty());

    // Reordering only rewrites dlc_load.json.
    const LaunchPlan reordered = f.plan(inst, rescanned, {"1000000006", "1000000001"});
    REQUIRE(reordered.configWrites.size() == 1);
    CHECK(reordered.configWrites[0].contents ==
          "{\"enabled_mods\":[\"mod/ugc_1000000006.mod\",\"mod/ugc_1000000001.mod\"],"
          "\"disabled_dlcs\":[\"dlc/dlc004_example/dlc004.dlc\"],"
          "\"pmm_fixture_extra\":{\"keep\":[1,2]}}");
}

TEST_CASE("Generated Workshop descriptors match the official launcher's", "[games][stellaris]") {
    StellarisFixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const fs::path registered = f.userData() / "mod" / "ugc_1000000001.mod";
    const std::string launcherWritten = test::readText(registered);

    fs::remove(registered);
    const LaunchPlan plan = f.plan(inst, mods, {"1000000001"});
    const ConfigWrite* write = findWrite(plan, registered);
    REQUIRE(write != nullptr);
    CHECK(write->contents == launcherWritten);
}

TEST_CASE("Stellaris dlc_load.json is created or rebuilt when needed", "[games][stellaris]") {
    StellarisFixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const fs::path dlcLoad = f.userData() / "dlc_load.json";

    SECTION("missing") {
        fs::remove(dlcLoad);
        const LaunchPlan plan = f.plan(inst, mods, {"local_tweaks"});
        REQUIRE(plan.configWrites.size() == 1);
        CHECK(plan.configWrites[0].contents ==
              "{\"enabled_mods\":[\"mod/local_tweaks.mod\"],\"disabled_dlcs\":[]}");
        CHECK(plan.warnings.empty());
    }
    SECTION("malformed") {
        test::writeText(dlcLoad, "{\"enabled_mods\":[\"mod/x.mod\"");
        const LaunchPlan plan = f.plan(inst, mods, {});
        REQUIRE(plan.configWrites.size() == 1);
        CHECK(plan.configWrites[0].contents == "{\"enabled_mods\":[],\"disabled_dlcs\":[]}");
        CHECK(test::containsText(plan.warnings, "could not be preserved"));
    }
    SECTION("without enabled_mods") {
        test::writeText(dlcLoad, "{\"disabled_dlcs\":[\"dlc/x.dlc\"],\"other\":1.5}");
        const LaunchPlan plan = f.plan(inst, mods, {"local_tweaks"});
        REQUIRE(plan.configWrites.size() == 1);
        CHECK(plan.configWrites[0].contents ==
              "{\"enabled_mods\":[\"mod/local_tweaks.mod\"],\"disabled_dlcs\":[\"dlc/x.dlc\"],"
              "\"other\":1.5}");
    }
    SECTION("already correct") {
        test::writeText(dlcLoad, "{\"enabled_mods\":[\"mod/local_tweaks.mod\"],"
                                 "\"disabled_dlcs\":[\"dlc/x.dlc\"]}");
        CHECK(f.plan(inst, mods, {"local_tweaks"}).configWrites.empty());
    }
}

TEST_CASE("Stellaris launch plan refuses what it cannot launch", "[games][stellaris]") {
    StellarisFixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const auto build = [&](std::vector<InstalledMod> ordered) {
        return f.adapter.buildLaunchPlan(inst, ordered);
    };

    InstalledMod unregisteredLocal = f.mod(mods, "local_tweaks");
    unregisteredLocal.descriptorPath.clear();
    auto result = build({unregisteredLocal});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::NotFound);
    CHECK(result.error().message.find("Local Tweaks") != std::string::npos);

    InstalledMod gone = f.mod(mods, "1000000001"); // removed after the scan
    gone.contentPath = f.content("1000000008");
    gone.descriptor.name = "Example Unsubscribed";
    result = build({gone});
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::NotFound);
    CHECK(result.error().message.find("Example Unsubscribed") != std::string::npos);

    result = build({f.mod(mods, "1000000004")}); // no readable descriptor anywhere
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::NotFound);

    CHECK(build({f.mod(mods, "1000000001"), f.mod(mods, "1000000001")}).error().code ==
          ErrorCode::InvalidArgument);
    InstalledMod foreign = f.mod(mods, "1000000001");
    foreign.identity.game = "eu4";
    CHECK(build({foreign}).error().code == ErrorCode::WrongGame);
    InstalledMod badId = f.mod(mods, "1000000001");
    badId.identity.id = "../../evil";
    CHECK(build({badId}).error().code == ErrorCode::InvalidArgument);

    GameInstallation noExe = inst;
    noExe.executablePath = f.install() / "missing.exe";
    CHECK(f.adapter.buildLaunchPlan(noExe, {}).error().code == ErrorCode::NotFound);

    // Nothing was written by any of this.
    CHECK_FALSE(fs::exists(f.userData() / "mod" / "ugc_1000000003.mod"));
}

TEST_CASE("Manual Stellaris installations get no Steam environment", "[games][stellaris]") {
    StellarisFixture f;
    GameInstallation inst = f.discovered();
    inst.distribution = DistributionSource::Manual;
    inst.steamAppId.clear();
    const LaunchPlan plan = f.plan(inst, f.scan(inst), {});
    CHECK(plan.environment.empty());
    CHECK(plan.arguments == std::vector<std::string>{"-gdpr-compliant"});
}

TEST_CASE("Stellaris compatibility warnings follow supported_version", "[games][stellaris]") {
    StellarisFixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    CHECK(f.adapter.compatibilityWarnings(inst, f.mod(mods, "1000000001")).empty()); // v4.5.*
    const auto older = f.adapter.compatibilityWarnings(inst, f.mod(mods, "1000000002"));
    CHECK(older ==
          std::vector<std::string>{"Made for Stellaris v4.4.*; the installed version is v4.5.2."});
    const auto early = f.adapter.compatibilityWarnings(inst, f.mod(mods, "1000000003")); // v4.*.*
    REQUIRE(early.size() == 1);
    CHECK(early[0].find("reports supported_version \"v4.*.*\" as invalid") != std::string::npos);

    InstalledMod custom = f.mod(mods, "1000000001");
    custom.descriptor.supportedVersion = "*";
    CHECK(f.adapter.compatibilityWarnings(inst, custom).size() == 1);
    custom.descriptor.supportedVersion = "latest";
    CHECK(test::containsText(f.adapter.compatibilityWarnings(inst, custom),
                             "is not a version pattern"));
    custom.descriptor.supportedVersion = "v3.*";
    CHECK(f.adapter.compatibilityWarnings(inst, custom).size() == 2);
    custom.descriptor.supportedVersion.clear();
    CHECK(f.adapter.compatibilityWarnings(inst, custom).empty());
}

TEST_CASE("Stellaris process names and load-order information", "[games][stellaris]") {
    StellarisAdapter adapter;
    GameInstallation inst;
    inst.platform = OsPlatform::Windows;
    inst.executablePath = u8path("C:/Games/Stellaris/stellaris.exe");
    ProcessNames names = adapter.processNames(inst);
    CHECK(names.game == std::vector<std::string>{"stellaris.exe"});
    CHECK(names.conflictingWriters ==
          std::vector<std::string>{"Paradox Launcher.exe", "dowser.exe"});

    inst.platform = OsPlatform::Linux;
    inst.executablePath = u8path("/games/Stellaris/stellaris");
    names = adapter.processNames(inst);
    CHECK(names.game == std::vector<std::string>{"stellaris"});
    CHECK(names.conflictingWriters == std::vector<std::string>{"dowser", "paradox-launcher"});

    inst.platform = OsPlatform::MacOS;
    inst.executablePath.clear();
    names = adapter.processNames(inst);
    CHECK(names.game == std::vector<std::string>{"stellaris"});
    CHECK(names.conflictingWriters == std::vector<std::string>{"Paradox Launcher"});

    const LoadOrderInfo info = adapter.loadOrderInfo();
    CHECK_FALSE(info.verifiedInGame);
    CHECK(info.summary.find("index 0 first") != std::string::npos);
    CHECK(info.summary.find("docs/load-order.md") != std::string::npos);
    CHECK(info.evidence.find("docs/load-order.md") != std::string::npos);
}
