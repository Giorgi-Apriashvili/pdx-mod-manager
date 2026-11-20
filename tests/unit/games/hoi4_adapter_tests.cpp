// Hearts of Iron IV adapter against a fixture modelled on the real HOI4
// 1.20.0.0 Steam installation of the development machine (read-only
// observations, see docs/games/hoi4.md): hoi4.exe, launcher-settings.json and
// steam_appid.txt in the installation folder, the launcher's pretty-printed
// dlc_load.json, launcher-written registrations with dependencies and a
// repeated replace_path, a "1.*.*" pattern, a Workshop descriptor.mod with
// its author's stale path=, one registration older than its content, and an
// item that is not registered yet.

#include "core/log.hpp"
#include "games/ck3.hpp"
#include "games/eu4.hpp"
#include "games/hoi4.hpp"
#include "games/stellaris.hpp"
#include "games/vic3.hpp"
#include "platform/steam.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace pmm;
using namespace pmm::games;
using test::findMod;
using test::samePath;
using test::u8path;

namespace {

constexpr const char* kContent = "Steam/steamapps/workshop/content/394360/";

struct Hoi4Fixture : test::GameFixture {
    Hoi4Adapter adapter;

    Hoi4Fixture() : GameFixture("hoi4") {}

    fs::path userData() const { return path("Documents/Paradox Interactive/Hearts of Iron IV"); }
    fs::path install() const { return path("Steam/steamapps/common/Hearts of Iron IV"); }
    fs::path settingsFile() const { return install() / "launcher-settings.json"; }
    fs::path content(std::string_view id) const { return path(kContent + std::string(id)); }
    std::string genericContent(std::string_view id) const {
        return generic(kContent + std::string(id));
    }

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
    // Rewrites the gameDataPath of launcher-settings.json.
    void setGameDataPath(std::string_view value) const {
        test::writeText(
            settingsFile(),
            test::replaceAll(test::readText(settingsFile()),
                             "\"%USER_DOCUMENTS%/Paradox Interactive/Hearts of Iron IV\"",
                             "\"" + std::string(value) + "\""));
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

bool logged(const std::vector<LogRecord>& records, std::string_view message) {
    return std::any_of(records.begin(), records.end(), [&](const LogRecord& record) {
        return record.message.find(message) != std::string::npos;
    });
}

} // namespace

TEST_CASE("Hearts of Iron IV is discovered through the injected Steam environment",
          "[games][hoi4]") {
    Hoi4Fixture f;
    const GameInstallation inst = f.discovered();
    CHECK(inst.game == "hoi4");
    CHECK(samePath(inst.installPath, f.install()));
    CHECK(samePath(inst.userDataPath, f.userData()));
    // Everything in the installation folder, as for Stellaris: no launcher/
    // or binaries/ folder.
    CHECK(samePath(inst.executablePath, f.install() / "hoi4.exe"));
    CHECK_FALSE(fs::exists(f.install() / "launcher"));
    CHECK_FALSE(fs::exists(f.install() / "binaries"));
    CHECK(inst.defaultArguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(inst.distribution == DistributionSource::Steam);
    CHECK(inst.steamAppId == "394360");
    CHECK(samePath(inst.steamLibraryPath, f.path("Steam")));
    // rawVersion: no leading "v", four components; the label adds the
    // codename and a fifth part.
    CHECK(inst.version == "1.20.0.0");
    CHECK(inst.versionLabel == "Operation Postern v1.20.0.0.8873 (0c4f)");
    CHECK_FALSE(inst.manuallyConfigured);

    Capabilities caps = f.adapter.capabilities(inst);
    CHECK(caps.directLaunch);
    CHECK(caps.localMods);
    CHECK_FALSE(caps.workshopAcquisition); // no Steam API library in the fixture
    test::writeText(f.install() / u8path(platform::steam::steamApiLibraryName()),
                    "placeholder, never loaded");
    CHECK(f.adapter.capabilities(inst).workshopAcquisition);

    // A manual selection of the same folder.
    auto manual = f.adapter.inspectInstallation(f.install(), {}, f.env);
    REQUIRE(manual.ok());
    CHECK(manual->distribution == DistributionSource::Steam);
    CHECK(manual->steamAppId == "394360");
    CHECK(manual->manuallyConfigured);
    CHECK(samePath(manual->executablePath, f.install() / "hoi4.exe"));
    CHECK(samePath(manual->userDataPath, f.userData()));
    CHECK(manual->version == "1.20.0.0");

    // A subfolder chosen by mistake.
    fs::create_directories(f.install() / "dlc");
    auto wrong = f.adapter.inspectInstallation(f.install() / "dlc", {}, f.env);
    REQUIRE_FALSE(wrong.ok());
    CHECK(wrong.error().code == ErrorCode::NotFound);
    CHECK(wrong.error().message ==
          "The selected folder does not contain the Hearts of Iron IV executable (hoi4.exe). "
          "Choose the folder above it, \"Hearts of Iron IV\", instead.");
}

TEST_CASE("Hearts of Iron IV and the other games are told apart", "[games][hoi4]") {
    Hoi4Fixture f;
    CHECK(StellarisAdapter().discover(f.env).empty());
    CHECK(Eu4Adapter().discover(f.env).empty());
    CHECK(Ck3Adapter().discover(f.env).empty());
    CHECK(Vic3Adapter().discover(f.env).empty());
    // The layout is Stellaris's; launcher-settings.json's gameId tells them apart.
    auto asStellaris = StellarisAdapter().inspectInstallation(f.install(), {}, f.env);
    REQUIRE_FALSE(asStellaris.ok());
    CHECK(asStellaris.error().code == ErrorCode::WrongGame);
    CHECK(asStellaris.error().message ==
          "The selected folder contains another game (\"hoi4\"), not Stellaris.");
    CHECK(Eu4Adapter().inspectInstallation(f.install(), {}, f.env).error().code ==
          ErrorCode::WrongGame);
    CHECK(Ck3Adapter().inspectInstallation(f.install(), {}, f.env).error().code ==
          ErrorCode::WrongGame);
    CHECK(Vic3Adapter().inspectInstallation(f.install(), {}, f.env).error().code ==
          ErrorCode::WrongGame);

    const auto rejects = [&](std::string_view fixture, std::string_view install,
                             std::string_view gameId) {
        INFO(fixture);
        test::GameFixture other(fixture);
        CHECK(f.adapter.discover(other.env).empty());
        auto wrong = f.adapter.inspectInstallation(other.path(install), {}, other.env);
        REQUIRE_FALSE(wrong.ok());
        CHECK(wrong.error().code == ErrorCode::WrongGame);
        CHECK(wrong.error().message == "The selected folder contains another game (\"" +
                                           std::string(gameId) + "\"), not Hearts of Iron IV.");
    };
    rejects("stellaris", "Steam/steamapps/common/Stellaris", "stellaris");
    rejects("eu4", "Steam/steamapps/common/Europa Universalis IV", "eu4");
    rejects("ck3", "Steam/steamapps/common/Crusader Kings III", "ck3");
    rejects("vic3", "Steam/steamapps/common/Victoria 3", "victoria3");
}

TEST_CASE("Hearts of Iron IV falls back to hoi4.exe without launcher-settings.json",
          "[games][hoi4]") {
    Hoi4Fixture f;
    fs::remove(f.settingsFile());
    CHECK(f.adapter.defaultExecutable() == u8path("hoi4.exe"));

    const GameInstallation inst = f.discovered();
    CHECK(samePath(inst.executablePath, f.install() / "hoi4.exe"));
    CHECK(inst.defaultArguments.empty());
    CHECK(inst.version.empty());
    CHECK(samePath(inst.userDataPath, f.userData())); // the default folder
    auto plan = f.adapter.buildLaunchPlan(inst, {});
    REQUIRE(plan.ok());
    CHECK(samePath(plan->workingDirectory, f.install()));

    fs::remove(f.install() / "hoi4.exe");
    CHECK_FALSE(f.adapter.capabilities(inst).directLaunch);
    CHECK(f.adapter.buildLaunchPlan(inst, {}).error().code == ErrorCode::NotFound);
    auto manual = f.adapter.inspectInstallation(f.install(), {}, f.env);
    REQUIRE_FALSE(manual.ok());
    CHECK(manual.error().code == ErrorCode::NotFound);
    CHECK(manual.error().message ==
          "The selected folder does not contain the Hearts of Iron IV executable (hoi4.exe).");
}

TEST_CASE("Hearts of Iron IV takes gameDataPath before userdir.txt", "[games][hoi4]") {
    Hoi4Fixture f;
    test::LogCapture capture;
    const fs::path userDirFile = f.install() / "userdir.txt";
    const fs::path custom = f.path("Custom Data/HOI4 dätä");
    const fs::path elsewhere = f.path("Documents/Paradox Interactive/HOI4 Elsewhere");
    fs::create_directories(custom);

    // None ships with 1.20.0.0: gameDataPath decides.
    CHECK_FALSE(fs::exists(userDirFile));
    CHECK(samePath(f.discovered().userDataPath, f.userData()));
    CHECK_FALSE(logged(capture.records(), "userdir.txt"));

    // A userdir.txt in the game folder is "old style": gameDataPath overrides it.
    test::writeText(userDirFile, f.generic("Custom Data/HOI4 dätä") + "\n");
    CHECK(samePath(f.discovered().userDataPath, f.userData()));
    CHECK(logged(capture.records(), "Ignoring the game's userdir.txt: gameDataPath in "
                                    "launcher-settings.json overrides it"));
    f.setGameDataPath("%USER_DOCUMENTS%/Paradox Interactive/HOI4 Elsewhere");
    CHECK(samePath(f.discovered().userDataPath, elsewhere));
    auto manual = f.adapter.inspectInstallation(f.install(), {}, f.env);
    REQUIRE(manual.ok());
    CHECK(samePath(manual->userDataPath, elsewhere));
    // A folder chosen by the user still wins.
    auto chosen = f.adapter.inspectInstallation(f.install(), f.userData(), f.env);
    REQUIRE(chosen.ok());
    CHECK(samePath(chosen->userDataPath, f.userData()));

    // hoi4.exe tells users to point gameDataPath at their custom user folder,
    // so an absolute one is taken too (normalized), before userdir.txt.
    const auto setGameDataPath = [&](std::string_view from, const std::string& to) {
        test::writeText(f.settingsFile(),
                        test::replaceAll(test::readText(f.settingsFile()),
                                         "\"" + std::string(from) + "\"", "\"" + to + "\""));
    };
    const fs::path absolute = f.path("Absolute Data/HOI4 dätä");
    setGameDataPath("%USER_DOCUMENTS%/Paradox Interactive/HOI4 Elsewhere",
                    f.generic("Absolute Data") + "/./HOI4 dätä/");
    capture.clear();
    CHECK(f.discovered().userDataPath == absolute.lexically_normal());
    CHECK(logged(capture.records(), "Using the absolute gameDataPath of launcher-settings.json"));
    CHECK(logged(capture.records(), "Ignoring the game's userdir.txt: gameDataPath in "
                                    "launcher-settings.json overrides it"));
    manual = f.adapter.inspectInstallation(f.install(), {}, f.env);
    REQUIRE(manual.ok());
    CHECK(manual->userDataPath == absolute.lexically_normal());

    // A gameDataPath that does not resolve (here with a ".." part) is ignored
    // with a warning, and userdir.txt is used.
    const std::string unsupported = f.generic("Absolute Data") + "/../HOI4 dätä";
    setGameDataPath(f.generic("Absolute Data") + "/./HOI4 dätä/", unsupported);
    capture.clear();
    CHECK(samePath(f.discovered().userDataPath, custom));
    CHECK(logged(capture.records(), "Ignoring an unsupported gameDataPath"));
    CHECK(logged(capture.records(), "Using the user data folder from userdir.txt"));

    // Without gameDataPath, userdir.txt decides; without either, the default.
    test::writeText(f.settingsFile(),
                    test::replaceAll(test::readText(f.settingsFile()),
                                     "\t\"gameDataPath\": \"" + unsupported + "\",\n", ""));
    REQUIRE(test::readText(f.settingsFile()).find("gameDataPath") == std::string::npos);
    CHECK(samePath(f.discovered().userDataPath, custom));
    // A relative path is ignored with a warning.
    capture.clear();
    test::writeText(userDirFile, "Custom Data/HOI4 dätä");
    CHECK(samePath(f.discovered().userDataPath, f.userData()));
    CHECK(logged(capture.records(), "Ignoring the game's userdir.txt"));
    fs::remove(userDirFile);
    CHECK(samePath(f.discovered().userDataPath, f.userData()));

    // CK3's place for the file is not read for Hearts of Iron IV.
    test::writeText(f.install() / "game" / "userdir.txt", f.generic("Custom Data/HOI4 dätä"));
    CHECK(samePath(f.discovered().userDataPath, f.userData()));
}

TEST_CASE("The other games keep userdir.txt before gameDataPath", "[games][hoi4]") {
    // The order Hearts of Iron IV reverses: unchanged for Stellaris.
    CHECK_FALSE(stellarisTraits().gameDataPathOverridesUserDirFile);
    test::GameFixture f("stellaris");
    const fs::path install = f.path("Steam/steamapps/common/Stellaris");
    const fs::path settings = install / "launcher-settings.json";
    const fs::path custom = f.path("Custom Data/Stellaris dätä");
    fs::create_directories(custom);
    test::writeText(install / "userdir.txt", f.generic("Custom Data/Stellaris dätä"));
    REQUIRE(test::readText(settings).find("gameDataPath") != std::string::npos);
    const auto userData = [&] {
        const auto found = StellarisAdapter().discover(f.env);
        REQUIRE(found.size() == 1);
        return found.front().userDataPath;
    };
    CHECK(samePath(userData(), custom));

    // Nor does Stellaris take an absolute gameDataPath: without userdir.txt
    // it is ignored, and the default folder is used.
    fs::remove(install / "userdir.txt");
    test::writeText(settings, test::replaceAll(test::readText(settings),
                                               "%USER_DOCUMENTS%/Paradox Interactive/Stellaris",
                                               f.generic("Custom Data/Stellaris dätä")));
    CHECK(samePath(userData(), f.path("Documents/Paradox Interactive/Stellaris")));
}

TEST_CASE("Hearts of Iron IV scan applies the identity rules", "[games][hoi4]") {
    Hoi4Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    std::vector<std::string> keys;
    for (const InstalledMod& m : mods)
        keys.push_back(m.identity.source + ":" + m.identity.id);
    CHECK(keys == std::vector<std::string>{"local:local_focus_tree", "steam-workshop:6000000001",
                                           "steam-workshop:6000000002", "steam-workshop:6000000003",
                                           "steam-workshop:6000000004", "steam-workshop:6000000005",
                                           "steam-workshop:6000000006"});
    for (const InstalledMod& m : mods) {
        CHECK(m.identity.game == "hoi4");
        CHECK(m.state == InstallState::Ready);
        CHECK(m.problems.empty());
    }

    const InstalledMod& local = f.mod(mods, "local_focus_tree");
    CHECK(local.descriptorRef == "mod/local_focus_tree.mod");
    CHECK(samePath(local.contentPath, f.userData() / "mod" / "local_focus_tree"));
    CHECK(local.descriptor.name == "Local Focus Tree");

    // dependencies are names, kept in the descriptor; repeated replace_path
    // entries are kept in file order.
    const InstalledMod& campaign = f.mod(mods, "6000000002");
    CHECK(campaign.descriptorRef == "mod/ugc_6000000002.mod");
    CHECK(samePath(campaign.contentPath, f.content("6000000002")));
    CHECK(campaign.descriptor.name == "Example Grand Campaign");
    CHECK(campaign.descriptor.dependencies == std::vector<std::string>{"Example Core Library"});
    CHECK(campaign.descriptor.extra ==
          std::vector<std::pair<std::string, std::string>>{{"replace_path", "common/ideas"},
                                                           {"replace_path", "history/countries"}});
    CHECK(campaign.contentUpdatedAt == 1795000002);

    // The registration's path=, not the author's stale one in descriptor.mod.
    const InstalledMod& designer = f.mod(mods, "6000000003");
    CHECK(designer.descriptor.supportedVersion == "1.*.*");
    CHECK(designer.descriptor.path == f.genericContent("6000000003"));

    // A registration older than its content: what the launcher registered is
    // shown (its path= still matches, so nothing is rewritten at launch).
    const InstalledMod& naval = f.mod(mods, "6000000004");
    CHECK(naval.descriptor.name == "Example Naval Rework");
    CHECK(naval.descriptor.supportedVersion == "1.19.4.1");

    // Not registered: described by the content's descriptor.mod.
    const InstalledMod& music = f.mod(mods, "6000000006");
    CHECK(music.descriptorPath.empty());
    CHECK(music.descriptorRef == "mod/ugc_6000000006.mod");
    CHECK(music.descriptor.name == "Example Music Pack");
}

TEST_CASE("Hearts of Iron IV reads the launcher's pretty-printed dlc_load.json and writes "
          "nothing for it",
          "[games][hoi4]") {
    Hoi4Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const std::string launcherWritten = test::readText(f.userData() / "dlc_load.json");
    REQUIRE(launcherWritten.find("{\n  \"enabled_mods\": [\n    \"mod/") == 0);
    REQUIRE(launcherWritten.back() == '}'); // no trailing newline

    auto entries = f.adapter.readCurrentConfiguration(inst, mods);
    REQUIRE(entries.ok());
    REQUIRE(entries->size() == 5);
    const auto& e = *entries;
    CHECK(e[0].identity == ModIdentity{"hoi4", "steam-workshop", "6000000002"});
    CHECK(e[0].nameHint == "Example Grand Campaign");
    CHECK(e[0].versionHint == "3.0");
    CHECK(e[1].identity == ModIdentity{"hoi4", "steam-workshop", "6000000001"});
    CHECK(e[2].identity == ModIdentity{"hoi4", "local", "local_focus_tree"});
    CHECK(e[2].nameHint == "Local Focus Tree");
    CHECK(e[3].identity == ModIdentity{"hoi4", "steam-workshop", "6000000004"});
    CHECK(e[4].identity == ModIdentity{"hoi4", "steam-workshop", "6000000003"});
    for (const PlaysetEntry& entry : e)
        CHECK(entry.enabled);

    // The same selection in the same order: compared by meaning, the
    // launcher's layout needs no write.
    std::vector<InstalledMod> ordered;
    for (const PlaysetEntry& entry : e)
        ordered.push_back(f.mod(mods, entry.identity.id));
    auto plan = f.adapter.buildLaunchPlan(inst, ordered);
    REQUIRE(plan.ok());
    CHECK(plan->configWrites.empty());
    CHECK(plan->warnings.empty());
    CHECK(test::readText(f.userData() / "dlc_load.json") == launcherWritten);
}

TEST_CASE("Hearts of Iron IV launch plan writes compact JSON and the missing registration",
          "[games][hoi4]") {
    Hoi4Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const fs::path modDir = f.userData() / "mod";
    const std::string staleRegistration = test::readText(modDir / "ugc_6000000004.mod");
    const std::initializer_list<std::string_view> order{"6000000001", "6000000002", "6000000006",
                                                        "local_focus_tree", "6000000004"};

    const LaunchPlan plan = f.plan(inst, mods, order);
    CHECK(plan.game == "hoi4");
    CHECK(samePath(plan.executable, f.install() / "hoi4.exe"));
    // The executable's folder, which is the installation folder; its
    // steam_appid.txt lies there.
    CHECK(samePath(plan.workingDirectory, f.install()));
    CHECK(test::readText(plan.workingDirectory / "steam_appid.txt") == "394360");
    CHECK(plan.arguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(plan.environment == std::vector<std::pair<std::string, std::string>>{
                                  {"SteamAppId", "394360"}, {"SteamGameId", "394360"}});
    CHECK(plan.warnings.empty());

    // Only the unregistered item gets a descriptor: registrations whose path=
    // matches are reused, the stale one too.
    REQUIRE(plan.configWrites.size() == 2);
    CHECK(samePath(plan.configWrites[0].target, modDir / "ugc_6000000006.mod"));
    CHECK(plan.configWrites[0].contents == "version=\"0.9\"\n"
                                           "tags={\n\t\"Sound\"\n}\n"
                                           "name=\"Example Music Pack\"\n"
                                           "picture=\"thumbnail.png\"\n"
                                           "supported_version=\"1.20.0.0\"\n"
                                           "path=\"" +
                                               f.genericContent("6000000006") +
                                               "\"\n"
                                               "remote_file_id=\"6000000006\"");
    // Compact, not in the launcher's pretty layout; disabled_dlcs is kept.
    CHECK(samePath(plan.configWrites[1].target, f.userData() / "dlc_load.json"));
    CHECK(plan.configWrites[1].contents ==
          "{\"enabled_mods\":[\"mod/ugc_6000000001.mod\",\"mod/ugc_6000000002.mod\","
          "\"mod/ugc_6000000006.mod\",\"mod/local_focus_tree.mod\",\"mod/ugc_6000000004.mod\"],"
          "\"disabled_dlcs\":[]}");
    CHECK(test::readText(modDir / "ugc_6000000004.mod") == staleRegistration);

    // Once applied, the same plan needs no writes, also after a rescan.
    apply(plan);
    CHECK(f.plan(inst, mods, order).configWrites.empty());
    const std::vector<InstalledMod> rescanned = f.scan(inst);
    CHECK(samePath(f.mod(rescanned, "6000000006").descriptorPath, modDir / "ugc_6000000006.mod"));
    CHECK(f.plan(inst, rescanned, order).configWrites.empty());

    // Back to the launcher's selection: one compact write, by meaning the
    // launcher's file again.
    const LaunchPlan back =
        f.plan(inst, rescanned,
               {"6000000002", "6000000001", "local_focus_tree", "6000000004", "6000000003"});
    REQUIRE(back.configWrites.size() == 1);
    CHECK(back.configWrites[0].contents ==
          "{\"enabled_mods\":[\"mod/ugc_6000000002.mod\",\"mod/ugc_6000000001.mod\","
          "\"mod/local_focus_tree.mod\",\"mod/ugc_6000000004.mod\",\"mod/ugc_6000000003.mod\"],"
          "\"disabled_dlcs\":[]}");
}

TEST_CASE("Generated HOI4 Workshop descriptors match the official launcher's", "[games][hoi4]") {
    Hoi4Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    // Plain; with dependencies and a repeated replace_path; from a
    // descriptor.mod whose author's path= is stale (dropped).
    for (const char* id : {"6000000001", "6000000002", "6000000003", "6000000005"}) {
        INFO(id);
        const fs::path registered =
            f.userData() / "mod" / u8path("ugc_" + std::string(id) + ".mod");
        const std::string launcherWritten = test::readText(registered);
        fs::remove(registered);
        const LaunchPlan plan = f.plan(inst, mods, {id});
        const ConfigWrite* write = findWrite(plan, registered);
        REQUIRE(write != nullptr);
        CHECK(write->contents == launcherWritten);
    }
    CHECK(test::readText(f.content("6000000003") / "descriptor.mod").find("C:/Users/Author/") !=
          std::string::npos);
}

TEST_CASE("Hearts of Iron IV compatibility warnings follow supported_version", "[games][hoi4]") {
    Hoi4Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const auto warnings = [&](std::string_view id) {
        return f.adapter.compatibilityWarnings(inst, f.mod(mods, id));
    };

    CHECK(warnings("6000000001").empty()); // "1.20.*"
    CHECK(warnings("6000000006").empty()); // "1.20.0.0"
    CHECK(warnings("local_focus_tree").empty());
    CHECK(warnings("6000000002") ==
          std::vector<std::string>{
              "Made for Hearts of Iron IV 1.19.*; the installed version is 1.20.0.0."});
    // The registration older than its content: what the game reads.
    CHECK(warnings("6000000004") ==
          std::vector<std::string>{
              "Made for Hearts of Iron IV 1.19.4.1; the installed version is 1.20.0.0."});
    // error.log of 1.19.5.0: "Invalid supported_version" for "1.*.*" only.
    CHECK(warnings("6000000003") ==
          std::vector<std::string>{"Hearts of Iron IV reports supported_version \"1.*.*\" as "
                                   "invalid (wildcards are only accepted in the patch number)."});

    InstalledMod custom = f.mod(mods, "6000000001");
    for (const char* accepted : {"1.20.0.*", "1.20.0.0", "1.20.*", "1.20"}) {
        INFO(accepted);
        custom.descriptor.supportedVersion = accepted;
        CHECK(f.adapter.compatibilityWarnings(inst, custom).empty());
    }
    // Untested for Hearts of Iron IV; the Stellaris and CK3 rule is assumed.
    for (const char* early : {"*", "1.*"}) {
        INFO(early);
        custom.descriptor.supportedVersion = early;
        CHECK(f.adapter.compatibilityWarnings(inst, custom).size() == 1);
    }
    custom.descriptor.supportedVersion = "0.*.*";
    CHECK(f.adapter.compatibilityWarnings(inst, custom).size() == 2);
}

TEST_CASE("Hearts of Iron IV download check accepts what its scan describes", "[games][hoi4]") {
    Hoi4Fixture f;
    for (const char* id :
         {"6000000001", "6000000002", "6000000003", "6000000004", "6000000005", "6000000006"}) {
        INFO(id);
        CHECK(f.adapter.verifyWorkshopContent(f.content(id)).ok());
    }
    // The download ends with a folder that has no descriptor.mod.
    const fs::path partial = f.path(std::string(kContent) + "6000000007");
    test::writeText(partial / "events" / "example_events.txt", "add_namespace = example\n");
    auto missing = f.adapter.verifyWorkshopContent(partial);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::ValidationFailed);
    CHECK(missing.error().message.find("descriptor.mod") != std::string::npos);
    test::writeText(partial / "descriptor.mod", "");
    CHECK_FALSE(f.adapter.verifyWorkshopContent(partial).ok());
    CHECK(f.adapter.verifyWorkshopContent(f.path(std::string(kContent) + "6000000008"))
              .error()
              .code == ErrorCode::NotFound);
}

TEST_CASE("Hearts of Iron IV process names and load-order information", "[games][hoi4]") {
    Hoi4Adapter adapter;
    GameInstallation inst;
    inst.executablePath = u8path("C:/Games/Hearts of Iron IV/hoi4.exe");
    ProcessNames names = adapter.processNames(inst);
    CHECK(names.game == std::vector<std::string>{"hoi4.exe"});
    CHECK(names.conflictingWriters ==
          std::vector<std::string>{"Paradox Launcher.exe", "dowser.exe"});
    inst.executablePath.clear();
    CHECK(adapter.processNames(inst).game == std::vector<std::string>{"hoi4.exe"});

    DiscoveryEnvironment env;
    env.documentsDir = u8path("/docs");
    CHECK(adapter.defaultUserDataPath(env) ==
          u8path("/docs/Paradox Interactive/Hearts of Iron IV"));

    // Not probed in the game yet: the playset order is written as for every
    // game, and a note says that it may not be the order the game applies.
    const LoadOrderInfo info = adapter.loadOrderInfo();
    CHECK_FALSE(info.verifiedInGame);
    CHECK(info.userNote == "Hearts of Iron IV may not follow this order when two mods change the "
                           "same file; this is not verified yet.");
    CHECK(info.summary.find("index 0 first") != std::string::npos);
    CHECK(info.summary.find("not verified for Hearts of Iron IV") != std::string::npos);
    CHECK(info.evidence.find("docs/games/hoi4.md") != std::string::npos);
}
