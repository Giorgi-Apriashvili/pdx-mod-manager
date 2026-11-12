// Crusader Kings III adapter against a fixture modelled on the real CK3
// 1.20.1.2 Steam installation of the development machine (read-only
// observations, see docs/games/ck3.md): the executable in binaries/,
// launcher-settings.json in launcher/, launcher-written registrations, a
// Workshop item that ships "<Name>.mod" instead of descriptor.mod, and one
// registration whose metadata is older than its content.

#include "core/log.hpp"
#include "games/ck3.hpp"
#include "games/eu4.hpp"
#include "games/stellaris.hpp"
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

constexpr const char* kContent = "Steam/steamapps/workshop/content/1158310/";

struct Ck3Fixture : test::GameFixture {
    Ck3Adapter adapter;

    Ck3Fixture() : GameFixture("ck3") {}

    fs::path userData() const { return path("Documents/Paradox Interactive/Crusader Kings III"); }
    fs::path install() const { return path("Steam/steamapps/common/Crusader Kings III"); }
    fs::path binaries() const { return install() / "binaries"; }
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

TEST_CASE("Crusader Kings III is discovered through the injected Steam environment",
          "[games][ck3]") {
    Ck3Fixture f;
    const GameInstallation inst = f.discovered();
    CHECK(inst.game == "ck3");
    CHECK(samePath(inst.installPath, f.install()));
    CHECK(samePath(inst.userDataPath, f.userData()));
    // launcher/launcher-settings.json names "../binaries/ck3.exe"; the
    // installation folder itself holds neither file.
    CHECK_FALSE(fs::exists(f.install() / "launcher-settings.json"));
    CHECK(samePath(inst.executablePath, f.binaries() / "ck3.exe"));
    CHECK(inst.defaultArguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(inst.distribution == DistributionSource::Steam);
    CHECK(inst.steamAppId == "1158310");
    CHECK(samePath(inst.steamLibraryPath, f.path("Steam")));
    // No leading "v", four components, the codename last.
    CHECK(inst.version == "1.20.1.2");
    CHECK(inst.versionLabel == "1.20.1.2 (Crozier)");
    CHECK_FALSE(inst.manuallyConfigured);

    Capabilities caps = f.adapter.capabilities(inst);
    CHECK(caps.directLaunch);
    CHECK(caps.localMods);
    CHECK_FALSE(caps.workshopAcquisition); // no Steam API library in the fixture
    // CK3 ships it next to its executable in binaries/.
    test::writeText(f.binaries() / u8path(platform::steam::steamApiLibraryName()),
                    "placeholder, never loaded");
    CHECK(f.adapter.capabilities(inst).workshopAcquisition);

    // A manual selection of the same folder.
    auto manual = f.adapter.inspectInstallation(f.install(), {}, f.env);
    REQUIRE(manual.ok());
    CHECK(manual->distribution == DistributionSource::Steam);
    CHECK(manual->steamAppId == "1158310");
    CHECK(manual->manuallyConfigured);
    CHECK(samePath(manual->executablePath, f.binaries() / "ck3.exe"));
    CHECK(manual->version == "1.20.1.2");

    // binaries/ (which holds ck3.exe) or launcher/ chosen by mistake.
    for (const fs::path& subfolder : {f.binaries(), f.install() / "launcher"}) {
        auto wrong = f.adapter.inspectInstallation(subfolder, {}, f.env);
        REQUIRE_FALSE(wrong.ok());
        CHECK(wrong.error().code == ErrorCode::NotFound);
        CHECK(wrong.error().message ==
              "The selected folder does not contain the Crusader Kings III executable "
              "(binaries/ck3.exe). Choose the folder above it, \"Crusader Kings III\", instead.");
    }
}

TEST_CASE("Crusader Kings III and the other games are told apart", "[games][ck3]") {
    Ck3Fixture f;
    CHECK(StellarisAdapter().discover(f.env).empty());
    CHECK(Eu4Adapter().discover(f.env).empty());
    CHECK(StellarisAdapter().inspectInstallation(f.install(), {}, f.env).error().code ==
          ErrorCode::WrongGame);
    CHECK(Eu4Adapter().inspectInstallation(f.install(), {}, f.env).error().code ==
          ErrorCode::WrongGame);

    test::GameFixture stellaris("stellaris");
    CHECK(f.adapter.discover(stellaris.env).empty());
    CHECK(f.adapter
              .inspectInstallation(stellaris.path("Steam/steamapps/common/Stellaris"), {},
                                   stellaris.env)
              .error()
              .code == ErrorCode::WrongGame);
    test::GameFixture eu4("eu4");
    CHECK(f.adapter.discover(eu4.env).empty());
    CHECK(f.adapter
              .inspectInstallation(eu4.path("Steam/steamapps/common/Europa Universalis IV"), {},
                                   eu4.env)
              .error()
              .code == ErrorCode::WrongGame);
}

TEST_CASE("Crusader Kings III falls back to binaries/ck3.exe without launcher-settings.json",
          "[games][ck3]") {
    Ck3Fixture f;
    fs::remove(f.install() / "launcher" / "launcher-settings.json");
    CHECK(f.adapter.defaultExecutable() == u8path("binaries/ck3.exe"));

    const GameInstallation inst = f.discovered();
    CHECK(samePath(inst.executablePath, f.binaries() / "ck3.exe"));
    CHECK(inst.defaultArguments.empty());
    CHECK(inst.version.empty());
    CHECK(samePath(inst.userDataPath, f.userData())); // the default folder
    auto plan = f.adapter.buildLaunchPlan(inst, {});
    REQUIRE(plan.ok());
    CHECK(samePath(plan->workingDirectory, f.binaries()));

    fs::remove(f.binaries() / "ck3.exe");
    CHECK_FALSE(f.adapter.capabilities(inst).directLaunch);
    CHECK(f.adapter.buildLaunchPlan(inst, {}).error().code == ErrorCode::NotFound);
    auto manual = f.adapter.inspectInstallation(f.install(), {}, f.env);
    REQUIRE_FALSE(manual.ok());
    CHECK(manual.error().code == ErrorCode::NotFound);
    CHECK(manual.error().message ==
          "The selected folder does not contain the Crusader Kings III executable "
          "(binaries/ck3.exe).");
}

TEST_CASE("Crusader Kings III reads game/userdir.txt, not the root userdir.txt", "[games][ck3]") {
    Ck3Fixture f;
    test::LogCapture capture;
    const fs::path userDirFile = f.install() / "game" / "userdir.txt";
    const fs::path custom = f.path("Custom Data/CK3 dätä");
    fs::create_directories(custom);

    // None ships with 1.20.1.2: gameDataPath decides.
    CHECK_FALSE(fs::exists(userDirFile));
    CHECK(samePath(f.discovered().userDataPath, f.userData()));
    // The location Stellaris and EU4 use is not read for CK3.
    test::writeText(f.install() / "userdir.txt", f.generic("Custom Data/CK3 dätä"));
    CHECK(samePath(f.discovered().userDataPath, f.userData()));

    // An absolute path ending in a separator, as in the CK3 wiki's example.
    test::writeText(userDirFile, f.generic("Custom Data/CK3 dätä") + "/\n");
    CHECK(samePath(f.discovered().userDataPath, custom));
    auto manual = f.adapter.inspectInstallation(f.install(), {}, f.env);
    REQUIRE(manual.ok());
    CHECK(samePath(manual->userDataPath, custom));
    // A folder chosen by the user still wins.
    auto chosen = f.adapter.inspectInstallation(f.install(), f.userData(), f.env);
    REQUIRE(chosen.ok());
    CHECK(samePath(chosen->userDataPath, f.userData()));

    // A relative path is ignored with a warning; gameDataPath comes next.
    capture.clear();
    test::writeText(userDirFile, "Custom Data/CK3 dätä/");
    CHECK(samePath(f.discovered().userDataPath, f.userData()));
    CHECK(logged(capture.records(), "Ignoring the game's userdir.txt"));
    const fs::path settings = f.install() / "launcher" / "launcher-settings.json";
    test::writeText(settings, test::replaceAll(test::readText(settings),
                                               "Paradox Interactive/Crusader Kings III\"",
                                               "Paradox Interactive/CK3 Elsewhere\""));
    CHECK(samePath(f.discovered().userDataPath,
                   f.path("Documents/Paradox Interactive/CK3 Elsewhere")));
}

TEST_CASE("Crusader Kings III scan applies the identity rules", "[games][ck3]") {
    Ck3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    std::vector<std::string> keys;
    for (const InstalledMod& m : mods)
        keys.push_back(m.identity.source + ":" + m.identity.id);
    CHECK(keys == std::vector<std::string>{"local:local_chronicle", "steam-workshop:4000000001",
                                           "steam-workshop:4000000002", "steam-workshop:4000000003",
                                           "steam-workshop:4000000004",
                                           "steam-workshop:4000000005"});
    for (const InstalledMod& m : mods)
        CHECK(m.identity.game == "ck3");

    const InstalledMod& local = f.mod(mods, "local_chronicle");
    CHECK(local.state == InstallState::Ready);
    CHECK(local.problems.empty());
    CHECK(local.descriptorRef == "mod/local_chronicle.mod");
    CHECK(samePath(local.contentPath, f.userData() / "mod" / "local_chronicle"));

    // Repeated replace_path entries are kept in file order.
    const InstalledMod& registered = f.mod(mods, "4000000001");
    CHECK(registered.state == InstallState::Ready);
    CHECK(registered.problems.empty());
    CHECK(registered.descriptorRef == "mod/ugc_4000000001.mod");
    CHECK(samePath(registered.contentPath, f.content("4000000001")));
    CHECK(registered.descriptor.name == "Example Realm Overhaul");
    CHECK(registered.descriptor.supportedVersion == "1.20.*");
    CHECK(registered.descriptor.extra ==
          std::vector<std::pair<std::string, std::string>>{{"replace_path", "common/landed_titles"},
                                                           {"replace_path", "history/titles"},
                                                           {"replace_path", "history/characters"}});
    CHECK(registered.contentUpdatedAt == 1790000001);

    // A registration older than its content: what the launcher registered is
    // shown (its path= still matches, so nothing is rewritten at launch).
    const InstalledMod& stale = f.mod(mods, "4000000002");
    CHECK(stale.state == InstallState::Ready);
    CHECK(stale.problems.empty());
    CHECK(stale.descriptor.name == "Example Vassals Classic");
    CHECK(stale.descriptor.version == "4.0");

    // Content with "<Name>.mod" instead of descriptor.mod, registered.
    const InstalledMod& sole = f.mod(mods, "4000000003");
    CHECK(sole.state == InstallState::Ready);
    CHECK(sole.problems.empty());
    CHECK(samePath(sole.descriptorPath, f.userData() / "mod" / "ugc_4000000003.mod"));
    CHECK(sole.descriptor.name == "Example Steel");

    // Not registered: described by the content's descriptor.mod.
    const InstalledMod& unregistered = f.mod(mods, "4000000004");
    CHECK(unregistered.state == InstallState::Ready);
    CHECK(unregistered.descriptorPath.empty());
    CHECK(unregistered.descriptorRef == "mod/ugc_4000000004.mod");
    CHECK(unregistered.descriptor.name == "Example Dynasty Tweaks");

    // Not registered and no .mod file at all.
    const InstalledMod& none = f.mod(mods, "4000000005");
    CHECK(none.state == InstallState::Broken);
    CHECK(none.problems == std::vector<std::string>{"The Workshop folder has no descriptor.mod."});
    CHECK(none.descriptorRef == "mod/ugc_4000000005.mod");
}

TEST_CASE("Crusader Kings III current configuration is read from dlc_load.json", "[games][ck3]") {
    Ck3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    auto entries = f.adapter.readCurrentConfiguration(inst, mods);
    REQUIRE(entries.ok());
    REQUIRE(entries->size() == 3);
    const auto& e = *entries;
    CHECK(e[0].identity == ModIdentity{"ck3", "steam-workshop", "4000000001"});
    CHECK(e[0].nameHint == "Example Realm Overhaul");
    CHECK(e[0].versionHint == "2.3");
    CHECK(e[1].identity == ModIdentity{"ck3", "local", "local_chronicle"});
    CHECK(e[1].nameHint == "Local Chronicle");
    CHECK(e[2].identity == ModIdentity{"ck3", "steam-workshop", "4000000003"});
    CHECK(e[2].nameHint == "Example Steel");
    for (const PlaysetEntry& entry : e)
        CHECK(entry.enabled);

    // The launcher's current selection is reproduced byte for byte: no writes.
    std::vector<InstalledMod> ordered;
    for (const PlaysetEntry& entry : e)
        ordered.push_back(f.mod(mods, entry.identity.id));
    auto plan = f.adapter.buildLaunchPlan(inst, ordered);
    REQUIRE(plan.ok());
    CHECK(plan->configWrites.empty());
}

TEST_CASE("Crusader Kings III launch plan writes the minimum configuration", "[games][ck3]") {
    Ck3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const fs::path modDir = f.userData() / "mod";
    const std::string staleRegistration = test::readText(modDir / "ugc_4000000002.mod");

    const LaunchPlan plan = f.plan(
        inst, mods, {"4000000003", "4000000004", "local_chronicle", "4000000002", "4000000001"});
    CHECK(plan.game == "ck3");
    CHECK(samePath(plan.executable, f.binaries() / "ck3.exe"));
    // The executable's folder, as the launcher does; steam_appid.txt lies there.
    CHECK(samePath(plan.workingDirectory, f.binaries()));
    CHECK(fs::exists(plan.workingDirectory / "steam_appid.txt"));
    CHECK(plan.arguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(plan.environment == std::vector<std::pair<std::string, std::string>>{
                                  {"SteamAppId", "1158310"}, {"SteamGameId", "1158310"}});
    CHECK(plan.warnings.empty());

    // Only the unregistered item gets a descriptor: registrations whose path=
    // matches are reused, the stale one too.
    REQUIRE(plan.configWrites.size() == 2);
    CHECK(samePath(plan.configWrites[0].target, modDir / "ugc_4000000004.mod"));
    CHECK(plan.configWrites[0].contents == "version=\"0.9\"\n"
                                           "name=\"Example Dynasty Tweaks\"\n"
                                           "tags={\n\t\"Gameplay\"\n}\n"
                                           "supported_version=\"1.20.1.2\"\n"
                                           "replace_path=\"common/dynasties\"\n"
                                           "replace_path=\"common/dynasty_houses\"\n"
                                           "path=\"" +
                                               f.genericContent("4000000004") +
                                               "\"\n"
                                               "remote_file_id=\"4000000004\"");
    // disabled_dlcs is kept verbatim (its CK3 format is unknown), compact
    // like the launcher writes it.
    CHECK(samePath(plan.configWrites[1].target, f.userData() / "dlc_load.json"));
    CHECK(plan.configWrites[1].contents ==
          "{\"enabled_mods\":[\"mod/ugc_4000000003.mod\",\"mod/ugc_4000000004.mod\","
          "\"mod/local_chronicle.mod\",\"mod/ugc_4000000002.mod\",\"mod/ugc_4000000001.mod\"],"
          "\"disabled_dlcs\":[\"pmm_fixture_opaque_entry\"]}");
    CHECK(test::readText(modDir / "ugc_4000000002.mod") == staleRegistration);

    // Once applied, the same plan needs no writes, also after a rescan.
    apply(plan);
    CHECK(f.plan(inst, mods,
                 {"4000000003", "4000000004", "local_chronicle", "4000000002", "4000000001"})
              .configWrites.empty());
    const std::vector<InstalledMod> rescanned = f.scan(inst);
    CHECK(samePath(f.mod(rescanned, "4000000004").descriptorPath, modDir / "ugc_4000000004.mod"));
    CHECK(f.plan(inst, rescanned,
                 {"4000000003", "4000000004", "local_chronicle", "4000000002", "4000000001"})
              .configWrites.empty());
}

TEST_CASE("Generated CK3 Workshop descriptors match the official launcher's", "[games][ck3]") {
    Ck3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    // From a descriptor.mod with repeated replace_path, and from "<Name>.mod".
    for (const char* id : {"4000000001", "4000000003"}) {
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
}

TEST_CASE("CK3 Workshop folders without descriptor.mod", "[games][ck3]") {
    Ck3Fixture f;
    const GameInstallation inst = f.discovered();
    const fs::path folder = f.content("4000000005");
    const fs::path ugcFile = f.userData() / "mod" / "ugc_4000000005.mod";

    SECTION("no .mod file: broken, never launched") {
        const std::vector<InstalledMod> mods = f.scan(inst);
        const InstalledMod& none = f.mod(mods, "4000000005");
        CHECK(none.state == InstallState::Broken);
        auto result = f.adapter.buildLaunchPlan(inst, std::vector<InstalledMod>{none});
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().code == ErrorCode::NotFound);
        CHECK_FALSE(fs::exists(ugcFile));
    }
    SECTION("exactly one .mod file describes the item") {
        test::writeText(folder / "Example_Sole.mod",
                        "name=\"Example Sole\"\nsupported_version=\"1.20.*\"\n"
                        "path=\"C:/Users/Author/Documents/Paradox Interactive/Crusader Kings "
                        "III/mod/Example Sole\"\nremote_file_id=\"4000000005\"");
        const std::vector<InstalledMod> mods = f.scan(inst);
        const InstalledMod& sole = f.mod(mods, "4000000005");
        CHECK(sole.state == InstallState::Ready);
        CHECK(sole.problems.empty());
        CHECK(sole.descriptorPath.empty());
        CHECK(sole.descriptor.name == "Example Sole");
        CHECK(samePath(sole.contentPath, folder));

        const LaunchPlan plan = f.plan(inst, mods, {"4000000005"});
        const ConfigWrite* write = findWrite(plan, ugcFile);
        REQUIRE(write != nullptr);
        CHECK(write->contents == "name=\"Example Sole\"\nsupported_version=\"1.20.*\"\npath=\"" +
                                     f.genericContent("4000000005") +
                                     "\"\nremote_file_id=\"4000000005\"");
    }
    SECTION("two .mod files: broken, named in the problem") {
        test::writeText(folder / "First.mod", "name=\"First\"");
        test::writeText(folder / "Second.mod", "name=\"Second\"");
        const std::vector<InstalledMod> mods = f.scan(inst);
        const InstalledMod& two = f.mod(mods, "4000000005");
        CHECK(two.state == InstallState::Broken);
        CHECK(two.problems ==
              std::vector<std::string>{"The Workshop folder has no descriptor.mod, and its 2 .mod "
                                       "files (First.mod and Second.mod) leave it open which one "
                                       "describes the mod."});
        CHECK(f.adapter.buildLaunchPlan(inst, std::vector<InstalledMod>{two}).error().code ==
              ErrorCode::NotFound);
    }
}

TEST_CASE("A registered CK3 Workshop folder stays the content next to a stray .zip",
          "[games][ck3]") {
    Ck3Fixture f;
    const GameInstallation inst = f.discovered();
    // Registered with path=<folder>; ships Example_Steel.mod, no descriptor.mod.
    const fs::path folder = f.content("4000000003");
    const fs::path registration = f.userData() / "mod" / "ugc_4000000003.mod";
    test::writeText(folder / "sources.zip", "PK placeholder");

    const std::vector<InstalledMod> mods = f.scan(inst);
    const InstalledMod& registered = f.mod(mods, "4000000003");
    CHECK(registered.state == InstallState::Ready);
    CHECK(registered.problems.empty());
    CHECK(samePath(registered.contentPath, folder));
    // The launcher's registration is reused, not rewritten to archive=.
    CHECK(findWrite(f.plan(inst, mods, {"4000000003"}), registration) == nullptr);

    // Unregistered, the .zip makes it a legacy zipped item, as before.
    fs::remove(registration);
    const std::vector<InstalledMod> rescanned = f.scan(inst);
    const InstalledMod& unregistered = f.mod(rescanned, "4000000003");
    CHECK(unregistered.state == InstallState::Broken);
    CHECK(samePath(unregistered.contentPath, folder / "sources.zip"));
}

TEST_CASE("Crusader Kings III compatibility warnings follow supported_version", "[games][ck3]") {
    Ck3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const auto warnings = [&](std::string_view id) {
        return f.adapter.compatibilityWarnings(inst, f.mod(mods, id));
    };

    CHECK(warnings("4000000001").empty()); // "1.20.*"
    CHECK(warnings("4000000004").empty()); // "1.20.1.2"
    CHECK(warnings("4000000003") ==
          std::vector<std::string>{
              "Made for Crusader Kings III 1.19.*; the installed version is 1.20.1.2."});
    // error.log of a 1.19 build: "Invalid supported_version" for "1.*" only.
    CHECK(warnings("4000000002") ==
          std::vector<std::string>{"Crusader Kings III reports supported_version \"1.*\" as "
                                   "invalid (wildcards are only accepted in the patch number)."});

    InstalledMod custom = f.mod(mods, "4000000001");
    for (const char* accepted : {"1.20.1.*", "1.20.1.2", "1.20"}) {
        custom.descriptor.supportedVersion = accepted;
        CHECK(f.adapter.compatibilityWarnings(inst, custom).empty());
    }
    custom.descriptor.supportedVersion = "1.*.*";
    CHECK(f.adapter.compatibilityWarnings(inst, custom).size() == 1);
    custom.descriptor.supportedVersion = "*";
    CHECK(f.adapter.compatibilityWarnings(inst, custom).size() == 1);
    custom.descriptor.supportedVersion = "0.*";
    CHECK(f.adapter.compatibilityWarnings(inst, custom).size() == 2);
}

TEST_CASE("Crusader Kings III process names and load-order information", "[games][ck3]") {
    Ck3Adapter adapter;
    GameInstallation inst;
    inst.executablePath = u8path("C:/Games/Crusader Kings III/binaries/ck3.exe");
    ProcessNames names = adapter.processNames(inst);
    CHECK(names.game == std::vector<std::string>{"ck3.exe"});
    CHECK(names.conflictingWriters ==
          std::vector<std::string>{"Paradox Launcher.exe", "dowser.exe"});
    inst.executablePath.clear();
    CHECK(adapter.processNames(inst).game == std::vector<std::string>{"ck3.exe"});

    DiscoveryEnvironment env;
    env.documentsDir = u8path("/docs");
    CHECK(adapter.defaultUserDataPath(env) ==
          u8path("/docs/Paradox Interactive/Crusader Kings III"));

    // Not probed in game yet: the playset order is written, but which mod
    // wins a shared file is not claimed, and users are told so.
    const LoadOrderInfo info = adapter.loadOrderInfo();
    CHECK_FALSE(info.verifiedInGame);
    CHECK(info.userNote == "It is not yet verified that Crusader Kings III follows this order "
                           "when two mods change the same file.");
    CHECK(info.summary.find("index 0 first") != std::string::npos);
    CHECK(info.summary.find("not verified for Crusader Kings III") != std::string::npos);
    CHECK(info.evidence.find("docs/games/ck3.md") != std::string::npos);
}
