// Victoria 3 adapter against a fixture modelled on the real Victoria 3 1.15.2
// Steam installation of the development machine (read-only observations, see
// docs/games/vic3.md): the executable in binaries/, launcher-settings.json in
// launcher/, content_load.json in the launcher's layout, mods described by
// .metadata/metadata.json in the forms seen there, a Paradox Mods folder, a
// folder without metadata and a zipped local mod.

#include "core/log.hpp"
#include "games/ck3.hpp"
#include "games/eu4.hpp"
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

constexpr const char* kContent = "Steam/steamapps/workshop/content/529340/";
constexpr const char* kUserData = "Documents/Paradox Interactive/Victoria 3";

struct Vic3Fixture : test::GameFixture {
    Vic3Adapter adapter;

    Vic3Fixture() : GameFixture("vic3") {}

    fs::path userData() const { return path(kUserData); }
    fs::path modDir() const { return userData() / "mod"; }
    fs::path contentLoad() const { return userData() / "content_load.json"; }
    fs::path install() const { return path("Steam/steamapps/common/Victoria 3"); }
    fs::path binaries() const { return install() / "binaries"; }
    fs::path content(std::string_view id) const { return path(kContent + std::string(id)); }
    // A path as content_load.json holds it, escaped for JSON.
    std::string jsonPath(const fs::path& path) const {
        return test::replaceAll(test::windowsForm(path.lexically_normal()), "\\", "\\\\");
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
    // content_load.json in the launcher's layout.
    std::string layout(const std::vector<fs::path>& enabled,
                       std::string_view rest = "  \"disabledDLC\": [\n"
                                               "    {\n"
                                               "      \"paradoxAppId\": \"dlc_fixture_example\"\n"
                                               "    }\n"
                                               "  ],\n"
                                               "  \"enabledUGC\": []\n") const {
        std::string text = "{\n  \"enabledMods\": [";
        for (std::size_t i = 0; i < enabled.size(); ++i)
            text += std::string(i == 0 ? "" : ",") + "\n    {\n      \"path\": \"" +
                    jsonPath(enabled[i]) + "\"\n    }";
        text += enabled.empty() ? "],\n" : "\n  ],\n";
        return text + std::string(rest) + "}";
    }
};

void apply(const LaunchPlan& plan) {
    for (const ConfigWrite& write : plan.configWrites)
        test::writeText(write.target, write.contents);
}

std::vector<std::string> filesIn(const fs::path& folder) {
    std::vector<std::string> names;
    for (const auto& entry : fs::recursive_directory_iterator(folder))
        names.push_back(platform::toGenericUtf8(entry.path().lexically_relative(folder)));
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

TEST_CASE("Victoria 3 is discovered through the injected Steam environment", "[games][vic3]") {
    Vic3Fixture f;
    const GameInstallation inst = f.discovered();
    CHECK(inst.game == "vic3");
    CHECK(samePath(inst.installPath, f.install()));
    CHECK(samePath(inst.userDataPath, f.userData()));
    // launcher/launcher-settings.json names "../binaries/victoria3.exe", not
    // the console wrapper next to it; the installation folder holds neither.
    CHECK_FALSE(fs::exists(f.install() / "launcher-settings.json"));
    CHECK(samePath(inst.executablePath, f.binaries() / "victoria3.exe"));
    CHECK(inst.defaultArguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(inst.distribution == DistributionSource::Steam);
    CHECK(inst.steamAppId == "529340");
    CHECK(samePath(inst.steamLibraryPath, f.path("Steam")));
    // Three components, no leading "v", the codename in the label.
    CHECK(inst.version == "1.15.2");
    CHECK(inst.versionLabel == "1.15.2 (Ivan-Chai)");
    CHECK_FALSE(inst.manuallyConfigured);

    Capabilities caps = f.adapter.capabilities(inst);
    CHECK(caps.directLaunch);
    CHECK(caps.localMods);
    CHECK_FALSE(caps.workshopAcquisition); // no Steam API library in the fixture
    test::writeText(f.binaries() / u8path(platform::steam::steamApiLibraryName()),
                    "placeholder, never loaded");
    CHECK(f.adapter.capabilities(inst).workshopAcquisition);

    // A manual selection of the same folder.
    auto manual = f.adapter.inspectInstallation(f.install(), {}, f.env);
    REQUIRE(manual.ok());
    CHECK(manual->distribution == DistributionSource::Steam);
    CHECK(manual->steamAppId == "529340");
    CHECK(manual->manuallyConfigured);
    CHECK(samePath(manual->executablePath, f.binaries() / "victoria3.exe"));
    CHECK(manual->version == "1.15.2");

    // binaries/ (which holds victoria3.exe) or launcher/ chosen by mistake.
    for (const fs::path& subfolder : {f.binaries(), f.install() / "launcher"}) {
        auto wrong = f.adapter.inspectInstallation(subfolder, {}, f.env);
        REQUIRE_FALSE(wrong.ok());
        CHECK(wrong.error().code == ErrorCode::NotFound);
        CHECK(wrong.error().message ==
              "The selected folder does not contain the Victoria 3 executable "
              "(binaries/victoria3.exe). Choose the folder above it, \"Victoria 3\", instead.");
    }
}

TEST_CASE("Victoria 3 and the other games are told apart", "[games][vic3]") {
    Vic3Fixture f;
    CHECK(StellarisAdapter().discover(f.env).empty());
    CHECK(Eu4Adapter().discover(f.env).empty());
    CHECK(Ck3Adapter().discover(f.env).empty());
    // The launcher's id "victoria3" (not the catalog key "vic3") is checked.
    CHECK(StellarisAdapter().inspectInstallation(f.install(), {}, f.env).error().code ==
          ErrorCode::WrongGame);
    CHECK(Ck3Adapter().inspectInstallation(f.install(), {}, f.env).error().code ==
          ErrorCode::WrongGame);
    auto asCk3 = Ck3Adapter().inspectInstallation(f.install(), {}, f.env);
    REQUIRE_FALSE(asCk3.ok());
    CHECK(asCk3.error().message ==
          "The selected folder contains another game (\"victoria3\"), not Crusader Kings III.");

    // The Crusader Kings III layout is the closest: binaries/ and launcher/.
    test::GameFixture ck3("ck3");
    CHECK(f.adapter.discover(ck3.env).empty());
    auto wrong = f.adapter.inspectInstallation(
        ck3.path("Steam/steamapps/common/Crusader Kings III"), {}, ck3.env);
    REQUIRE_FALSE(wrong.ok());
    CHECK(wrong.error().code == ErrorCode::WrongGame);
    CHECK(wrong.error().message ==
          "The selected folder contains another game (\"ck3\"), not Victoria 3.");
    test::GameFixture stellaris("stellaris");
    CHECK(f.adapter.discover(stellaris.env).empty());
    CHECK(f.adapter
              .inspectInstallation(stellaris.path("Steam/steamapps/common/Stellaris"), {},
                                   stellaris.env)
              .error()
              .code == ErrorCode::WrongGame);
}

TEST_CASE("Victoria 3 falls back to binaries/victoria3.exe without launcher-settings.json",
          "[games][vic3]") {
    Vic3Fixture f;
    fs::remove(f.install() / "launcher" / "launcher-settings.json");
    CHECK(f.adapter.defaultExecutable() == u8path("binaries/victoria3.exe"));

    const GameInstallation inst = f.discovered();
    CHECK(samePath(inst.executablePath, f.binaries() / "victoria3.exe"));
    CHECK(inst.defaultArguments.empty());
    CHECK(inst.version.empty());
    CHECK(samePath(inst.userDataPath, f.userData())); // the default folder
    auto plan = f.adapter.buildLaunchPlan(inst, {});
    REQUIRE(plan.ok());
    CHECK(samePath(plan->workingDirectory, f.binaries()));

    fs::remove(f.binaries() / "victoria3.exe");
    CHECK_FALSE(f.adapter.capabilities(inst).directLaunch);
    CHECK(f.adapter.buildLaunchPlan(inst, {}).error().code == ErrorCode::NotFound);
    auto manual = f.adapter.inspectInstallation(f.install(), {}, f.env);
    REQUIRE_FALSE(manual.ok());
    CHECK(manual.error().code == ErrorCode::NotFound);
    CHECK(manual.error().message == "The selected folder does not contain the Victoria 3 "
                                    "executable (binaries/victoria3.exe).");
}

TEST_CASE("Victoria 3 reads game/userdir.txt, not the root userdir.txt", "[games][vic3]") {
    Vic3Fixture f;
    const fs::path userDirFile = f.install() / "game" / "userdir.txt";
    const fs::path custom = f.path("Custom Data/Vic3 dätä");
    fs::create_directories(custom);

    // None ships with 1.15.2: gameDataPath decides.
    CHECK_FALSE(fs::exists(userDirFile));
    CHECK(samePath(f.discovered().userDataPath, f.userData()));
    test::writeText(f.install() / "userdir.txt", f.generic("Custom Data/Vic3 dätä"));
    CHECK(samePath(f.discovered().userDataPath, f.userData()));

    test::writeText(userDirFile, f.generic("Custom Data/Vic3 dätä") + "/\n");
    CHECK(samePath(f.discovered().userDataPath, custom));
    // A folder chosen by the user still wins.
    auto chosen = f.adapter.inspectInstallation(f.install(), f.userData(), f.env);
    REQUIRE(chosen.ok());
    CHECK(samePath(chosen->userDataPath, f.userData()));

    // gameDataPath comes next.
    fs::remove(userDirFile);
    const fs::path settings = f.install() / "launcher" / "launcher-settings.json";
    test::writeText(settings,
                    test::replaceAll(test::readText(settings), "Paradox Interactive/Victoria 3\"",
                                     "Paradox Interactive/Vic3 Elsewhere\""));
    CHECK(samePath(f.discovered().userDataPath,
                   f.path("Documents/Paradox Interactive/Vic3 Elsewhere")));
}

TEST_CASE("Victoria 3 scan applies the identity rules", "[games][vic3]") {
    Vic3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    // The entry names in mod/ (the .zip with its extension), without the
    // Paradox Mods folder pdx_123 and the stray notes.txt; Workshop ids.
    std::vector<std::string> keys;
    for (const InstalledMod& m : mods)
        keys.push_back(m.identity.source + ":" + m.identity.id);
    CHECK(keys == std::vector<std::string>{"local:Example Bundle.zip", "local:Local Almanac",
                                           "local:Unfinished Draft", "steam-workshop:5000000001",
                                           "steam-workshop:5000000002", "steam-workshop:5000000003",
                                           "steam-workshop:5000000004",
                                           "steam-workshop:5000000005"});
    for (const InstalledMod& m : mods)
        CHECK(m.identity.game == "vic3");

    const InstalledMod& local = f.mod(mods, "Local Almanac");
    CHECK(local.state == InstallState::Ready);
    CHECK(local.problems.empty());
    CHECK(samePath(local.contentPath, f.modDir() / "Local Almanac"));
    CHECK(samePath(local.descriptorPath,
                   f.modDir() / "Local Almanac" / ".metadata" / "metadata.json"));
    // The path content_load.json holds for it.
    CHECK(local.descriptorRef == test::windowsForm(f.modDir() / "Local Almanac"));
    CHECK(local.descriptor.name == "Local Almanac");
    CHECK(local.descriptor.version == "0.1.0");
    CHECK(local.descriptor.supportedVersion == "1.15.*");
    CHECK(local.descriptor.picture == "thumbnail.png");

    // A zipped mod: listed by its file name, its metadata (inside) not read.
    const InstalledMod& zipped = f.mod(mods, "Example Bundle.zip");
    CHECK(zipped.state == InstallState::Ready);
    CHECK(zipped.problems.empty());
    CHECK(samePath(zipped.contentPath, f.modDir() / "Example Bundle.zip"));
    CHECK(zipped.descriptorPath.empty());
    CHECK(zipped.descriptorRef == test::windowsForm(f.modDir() / "Example Bundle.zip"));
    CHECK(zipped.descriptor.name == "Example Bundle");

    const InstalledMod& draft = f.mod(mods, "Unfinished Draft");
    CHECK(draft.state == InstallState::Broken);
    CHECK(draft.problems == std::vector<std::string>{"The mod folder has no "
                                                     ".metadata/metadata.json, so the game "
                                                     "cannot load it."});

    // Tabs, CRLF, an object-form dependency; Steam's update time.
    const InstalledMod& ledger = f.mod(mods, "5000000001");
    CHECK(ledger.state == InstallState::Ready);
    CHECK(ledger.problems.empty());
    CHECK(samePath(ledger.contentPath, f.content("5000000001")));
    CHECK(ledger.descriptorRef == test::windowsForm(f.content("5000000001")));
    CHECK(ledger.descriptor.name == "Example Ledger");
    CHECK(ledger.descriptor.dependencies == std::vector<std::string>{"Example Core"});
    CHECK(ledger.descriptor.remoteFileId.empty());
    CHECK(ledger.contentUpdatedAt == 1790000101);

    // Steam knows a newer version.
    const InstalledMod& core = f.mod(mods, "5000000002");
    CHECK(core.state == InstallState::NeedsUpdate);
    CHECK(core.descriptor.name == "Example Core");
    CHECK(core.descriptor.supportedVersion == "1.7.*");

    // The older list form of relationships; no game_custom_data.
    const InstalledMod& maps = f.mod(mods, "5000000003");
    CHECK(maps.state == InstallState::Ready);
    CHECK(maps.descriptor.dependencies == std::vector<std::string>{"Example Core"});
    CHECK(maps.problems ==
          std::vector<std::string>{"Its .metadata/metadata.json has no \"game_custom_data\" "
                                   "entry; Victoria 3 reportedly leaves such a mod out."});

    const InstalledMod& broken = f.mod(mods, "5000000004");
    CHECK(broken.state == InstallState::Broken);
    REQUIRE(broken.problems.size() == 1);
    CHECK(broken.problems[0].rfind("The mod metadata \".metadata/metadata.json\" is not valid "
                                   "JSON (line 4, column 1: ",
                                   0) == 0);

    const InstalledMod& bare = f.mod(mods, "5000000005");
    CHECK(bare.state == InstallState::Broken);
    CHECK(bare.problems == std::vector<std::string>{"The Workshop folder has no "
                                                    ".metadata/metadata.json, so the game "
                                                    "cannot load it."});

    // Subscribed, not downloaded: not installed. Once content appears while
    // Steam has not finished, it is downloading.
    CHECK(findMod(mods, "5000000006") == nullptr);
    test::writeText(f.content("5000000006") / ".metadata" / "metadata.json",
                    "{\"name\": \"Example Pending\", \"game_custom_data\": {}}");
    const std::vector<InstalledMod> rescanned = f.scan(inst);
    CHECK(f.mod(rescanned, "5000000006").state == InstallState::Downloading);
}

TEST_CASE("Victoria 3 folder names that read the same become one Broken mod", "[games][vic3]") {
    Vic3Fixture f;
    const GameInstallation inst = f.discovered();
    // Unpaired surrogates have no UTF-8 form: both names read "Odd\uFFFD".
    for (const wchar_t* name : {L"Odd\xD800", L"Odd\xD801"}) {
        const fs::path folder = f.modDir() / fs::path(name);
        test::writeText(folder / ".metadata" / "metadata.json",
                        "{\"name\": \"Odd\", \"game_custom_data\": {}}");
    }
    const std::vector<InstalledMod> mods = f.scan(inst);
    const InstalledMod& odd = f.mod(mods, "Odd\xEF\xBF\xBD");
    CHECK(odd.state == InstallState::Broken);
    CHECK(odd.contentPath.empty());
    CHECK(odd.descriptorPath.empty());
    REQUIRE(odd.problems.size() == 1);
    CHECK(odd.problems[0].rfind("The mod folders ", 0) == 0);
    CHECK(odd.problems[0].find("all give this mod the id \"Odd\xEF\xBF\xBD\"") !=
          std::string::npos);
    CHECK(odd.problems[0].find("are shown as \xEF\xBF\xBD.") != std::string::npos);
    CHECK(std::count_if(mods.begin(), mods.end(), [](const InstalledMod& m) {
              return m.identity.id == "Odd\xEF\xBF\xBD";
          }) == 1);
}

TEST_CASE("Victoria 3 current configuration is read from content_load.json", "[games][vic3]") {
    Vic3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);

    auto entries = f.adapter.readCurrentConfiguration(inst, mods);
    REQUIRE(entries.ok());
    REQUIRE(entries->size() == 4);
    const auto& e = *entries;
    CHECK(e[0].identity == ModIdentity{"vic3", "local", "Local Almanac"});
    CHECK(e[0].nameHint == "Local Almanac");
    CHECK(e[0].versionHint == "0.1.0");
    CHECK(e[1].identity == ModIdentity{"vic3", "steam-workshop", "5000000002"});
    CHECK(e[1].nameHint == "Example Core");
    CHECK(e[2].identity == ModIdentity{"vic3", "steam-workshop", "5000000001"});
    CHECK(e[2].nameHint == "Example Ledger");
    CHECK(e[3].identity == ModIdentity{"vic3", "local", "Example Bundle.zip"});
    CHECK(e[3].nameHint == "Example Bundle");
    for (const PlaysetEntry& entry : e)
        CHECK(entry.enabled);

    // The launcher's current selection is reproduced: no writes.
    std::vector<InstalledMod> ordered;
    for (const PlaysetEntry& entry : e)
        ordered.push_back(f.mod(mods, entry.identity.id));
    auto plan = f.adapter.buildLaunchPlan(inst, ordered);
    REQUIRE(plan.ok());
    CHECK(plan->configWrites.empty());
    CHECK(plan->warnings.empty());
}

TEST_CASE("Victoria 3 content_load.json entries outside the inventory", "[games][vic3]") {
    Vic3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    // Other spellings of inventory paths; a Workshop item Steam has not
    // downloaded; local entries that are gone or lie elsewhere; a duplicate,
    // and entries the game cannot use.
    const std::string almanac = f.generic(std::string(kUserData) + "/mod/local almanac");
    test::writeText(f.contentLoad(),
                    "{\"enabledMods\": [{\"path\": \"" + almanac + "/\"},\n" + "{\"path\": \"" +
                        f.jsonPath(f.content("5000000006")) + "\"},\n" + "{\"path\": \"" +
                        f.jsonPath(f.content("5000000003")) + "\\\\\"},\n" + "{\"path\": \"" +
                        f.jsonPath(f.modDir() / "Removed Mod") + "\"},\n" +
                        "{\"path\": \"D:\\\\Elsewhere\\\\Portable Mod\"},\n" + "{\"path\": \"" +
                        f.jsonPath(f.modDir() / "Local Almanac") + "\"},\n" +
                        "{\"path\": 5}, \"x\", {}],\n" + "\"disabledDLC\": []}");
    auto entries = f.adapter.readCurrentConfiguration(inst, mods);
    REQUIRE(entries.ok());
    REQUIRE(entries->size() == 5);
    const auto& e = *entries;
    CHECK(e[0].identity == ModIdentity{"vic3", "local", "Local Almanac"});
    CHECK(e[0].nameHint == "Local Almanac");
    CHECK(e[1].identity == ModIdentity{"vic3", "steam-workshop", "5000000006"});
    CHECK(e[1].nameHint == "5000000006");
    CHECK(e[2].identity == ModIdentity{"vic3", "steam-workshop", "5000000003"});
    CHECK(e[2].nameHint == "Example Maps");
    CHECK(e[3].identity == ModIdentity{"vic3", "local", "Removed Mod"});
    CHECK(e[3].nameHint == "Removed Mod");
    CHECK(e[4].identity == ModIdentity{"vic3", "local", "Portable Mod"});
    CHECK(e[4].nameHint == "Portable Mod");

    // No file: nothing enabled. Not JSON, or not a list: a parse error.
    fs::remove(f.contentLoad());
    auto none = f.adapter.readCurrentConfiguration(inst, mods);
    REQUIRE(none.ok());
    CHECK(none->empty());
    test::writeText(f.contentLoad(), "{\"enabledMods\": [}");
    auto invalid = f.adapter.readCurrentConfiguration(inst, mods);
    REQUIRE_FALSE(invalid.ok());
    CHECK(invalid.error().code == ErrorCode::ParseError);
    CHECK(invalid.error().message.find("content_load.json") != std::string::npos);
    test::writeText(f.contentLoad(), "{\"enabledMods\": {}}");
    CHECK(f.adapter.readCurrentConfiguration(inst, mods).error().code == ErrorCode::ParseError);
    test::writeText(f.contentLoad(), "{\"disabledDLC\": []}");
    CHECK(f.adapter.readCurrentConfiguration(inst, mods)->empty());
}

TEST_CASE("Victoria 3 entries elsewhere never take the id of an installed mod", "[games][vic3]") {
    Vic3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const test::LogCapture capture;
    // As a tool other than the launcher could write them: another folder with
    // the installed local mod's name, one that is gone, a drive root, an empty
    // path, and the installed folder itself in the \\?\ form.
    const fs::path almanac = f.modDir() / "Local Almanac";
    const fs::path namesake = f.path("Elsewhere/Local Almanac");
    test::writeText(namesake / ".metadata" / "metadata.json", "{\"name\": \"Other Almanac\"}");
    const std::string alias = "\\\\?\\" + test::windowsForm(almanac.lexically_normal());
    // Not the same path to the adapter, which compares paths as written.
    REQUIRE_FALSE(platform::isWithin(platform::pathFromUtf8(alias), almanac));
    test::writeText(f.contentLoad(),
                    "{\"enabledMods\": [{\"path\": \"" + f.jsonPath(namesake) + "\"},\n" +
                        "{\"path\": \"D:\\\\Elsewhere\\\\Local Almanac\"},\n" +
                        "{\"path\": \"C:\\\\\"}, {\"path\": \"\"},\n" + "{\"path\": \"" +
                        test::replaceAll(alias, "\\", "\\\\") + "\"},\n" +
                        "{\"path\": \"D:\\\\Elsewhere\\\\Portable Mod\"}]}");
    auto entries = f.adapter.readCurrentConfiguration(inst, mods);
    REQUIRE(entries.ok());
    REQUIRE(entries->size() == 2);
    const auto& e = *entries;
    CHECK(e[0].identity == ModIdentity{"vic3", "local", "Local Almanac"});
    CHECK(e[0].nameHint == "Local Almanac");
    CHECK(e[0].versionHint == "0.1.0");
    CHECK(e[1].identity == ModIdentity{"vic3", "local", "Portable Mod"});
    const auto leftOut = std::count_if(
        capture.records().begin(), capture.records().end(), [](const LogRecord& record) {
            return record.level == LogLevel::Warning &&
                   record.message.find("it is left out") != std::string::npos;
        });
    CHECK(leftOut == 4);
}

TEST_CASE("Victoria 3 launch plan writes only content_load.json", "[games][vic3]") {
    Vic3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const std::string launcherWritten = test::readText(f.contentLoad());
    const std::vector<std::string> modFiles = filesIn(f.modDir());

    const LaunchPlan plan =
        f.plan(inst, mods, {"5000000001", "Local Almanac", "5000000003", "Example Bundle.zip"});
    CHECK(plan.game == "vic3");
    CHECK(samePath(plan.executable, f.binaries() / "victoria3.exe"));
    // The executable's folder, as the launcher does: the engine finds ../game
    // from there.
    CHECK(samePath(plan.workingDirectory, f.binaries()));
    CHECK(plan.arguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(plan.environment == std::vector<std::pair<std::string, std::string>>{
                                  {"SteamAppId", "529340"}, {"SteamGameId", "529340"}});
    CHECK(plan.warnings.empty());

    // No registration files: content_load.json is the only write, in the
    // launcher's layout, disabledDLC and enabledUGC kept.
    REQUIRE(plan.configWrites.size() == 1);
    const ConfigWrite& write = plan.configWrites[0];
    CHECK(samePath(write.target, f.contentLoad()));
    CHECK(write.purpose == "load order (content_load.json)");
    CHECK(write.contents == f.layout({f.content("5000000001"), f.modDir() / "Local Almanac",
                                      f.content("5000000003"), f.modDir() / "Example Bundle.zip"}));
    CHECK(test::readText(f.contentLoad()) == launcherWritten);

    // Once applied, the same plan needs no writes, also after a rescan; mod/
    // is untouched.
    apply(plan);
    CHECK(f.plan(inst, mods, {"5000000001", "Local Almanac", "5000000003", "Example Bundle.zip"})
              .configWrites.empty());
    const std::vector<InstalledMod> rescanned = f.scan(inst);
    CHECK(
        f.plan(inst, rescanned, {"5000000001", "Local Almanac", "5000000003", "Example Bundle.zip"})
            .configWrites.empty());
    CHECK(filesIn(f.modDir()) == modFiles);

    // Back to the launcher's selection: byte for byte the launcher's file.
    const LaunchPlan back =
        f.plan(inst, mods, {"Local Almanac", "5000000002", "5000000001", "Example Bundle.zip"});
    REQUIRE(back.configWrites.size() == 1);
    CHECK(back.configWrites[0].contents == launcherWritten);

    // Without mods, enabledMods is an empty list.
    const LaunchPlan vanilla = f.plan(inst, mods, {});
    REQUIRE(vanilla.configWrites.size() == 1);
    CHECK(vanilla.configWrites[0].contents == f.layout({}));
}

TEST_CASE("Victoria 3 keeps everything but enabledMods in content_load.json", "[games][vic3]") {
    Vic3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const fs::path almanac = f.modDir() / "Local Almanac";

    SECTION("unknown keys, formatVersion and enabledUGC entries, in their order") {
        test::writeText(f.contentLoad(),
                        "{\"formatVersion\":\"1.1\",\"disabledDLC\":[{\"paradoxAppId\":\"dlc_b\"},"
                        "{\"paradoxAppId\":\"dlc_a\"}],\"enabledUGC\":[{\"path\":\"C:\\\\UGC\"}],"
                        "\"enabledMods\":[],\"pmmFixtureUnknown\":{\"kept\":[1,2.5,null]}}");
        const LaunchPlan plan = f.plan(inst, mods, {"Local Almanac"});
        REQUIRE(plan.configWrites.size() == 1);
        CHECK(plan.configWrites[0].contents ==
              "{\n  \"formatVersion\": \"1.1\",\n  \"disabledDLC\": [\n    {\n"
              "      \"paradoxAppId\": \"dlc_b\"\n    },\n    {\n"
              "      \"paradoxAppId\": \"dlc_a\"\n    }\n  ],\n  \"enabledUGC\": [\n    {\n"
              "      \"path\": \"C:\\\\UGC\"\n    }\n  ],\n  \"enabledMods\": [\n    {\n"
              "      \"path\": \"" +
                  f.jsonPath(almanac) +
                  "\"\n    }\n  ],\n  \"pmmFixtureUnknown\": {\n    \"kept\": [\n      1,\n"
                  "      2.5,\n      null\n    ]\n  }\n}");
    }
    SECTION("no enabledMods: it comes first") {
        test::writeText(f.contentLoad(), "{\"disabledDLC\":[],\"enabledUGC\":[]}");
        const LaunchPlan plan = f.plan(inst, mods, {"Local Almanac"});
        REQUIRE(plan.configWrites.size() == 1);
        CHECK(plan.configWrites[0].contents ==
              f.layout({almanac}, "  \"disabledDLC\": [],\n  \"enabledUGC\": []\n"));
    }
    SECTION("no file: the launcher's three keys, nothing else") {
        fs::remove(f.contentLoad());
        const LaunchPlan plan = f.plan(inst, mods, {"Local Almanac"});
        REQUIRE(plan.configWrites.size() == 1);
        CHECK(plan.configWrites[0].contents ==
              f.layout({almanac}, "  \"disabledDLC\": [],\n  \"enabledUGC\": []\n"));
        CHECK(plan.warnings.empty());
    }
    SECTION("the same JSON in another layout: no write") {
        test::writeText(f.contentLoad(),
                        "\t{ \"enabledMods\" : [ { \"path\" : \"" + f.jsonPath(almanac) +
                            "\" } ], \"disabledDLC\" : [ ], \"enabledUGC\" : [ ] }\r\n");
        CHECK(f.plan(inst, mods, {"Local Almanac"}).configWrites.empty());
    }
    SECTION("not JSON: rebuilt with a warning") {
        test::writeText(f.contentLoad(), "{\"enabledMods\": [");
        const LaunchPlan plan = f.plan(inst, mods, {"Local Almanac"});
        REQUIRE(plan.configWrites.size() == 1);
        CHECK(plan.configWrites[0].contents ==
              f.layout({almanac}, "  \"disabledDLC\": [],\n  \"enabledUGC\": []\n"));
        CHECK(plan.warnings ==
              std::vector<std::string>{"content_load.json was not valid JSON and is rebuilt; DLC "
                                       "choices stored in it could not be preserved."});
    }
    SECTION("unreadable: never overwritten") {
        // Larger than the 16 MiB read limit: DLC choices that could not be
        // read are not replaced.
        test::writeText(f.contentLoad(), "{\"disabledDLC\": [], \"x\": \"" +
                                             std::string(std::size_t{16} * 1024 * 1024, 'x') +
                                             "\"}");
        auto plan = f.adapter.buildLaunchPlan(inst, std::vector<InstalledMod>{});
        REQUIRE_FALSE(plan.ok());
        CHECK(plan.error().code == ErrorCode::LimitExceeded);
    }
}

TEST_CASE("Victoria 3 reuses the path strings content_load.json holds", "[games][vic3]") {
    Vic3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    // Another spelling of the same folders (generic separators, other case,
    // a trailing separator): kept as written, so nothing changes.
    const std::string almanac = f.generic(std::string(kUserData) + "/mod/LOCAL ALMANAC/");
    const std::string ledger = f.generic(std::string(kContent) + "5000000001");
    test::writeText(f.contentLoad(), "{\"enabledMods\":[{\"path\":\"" + almanac +
                                         "\"},{\"path\":\"" + ledger +
                                         "\"}],\"disabledDLC\":[],\"enabledUGC\":[]}");
    CHECK(f.plan(inst, mods, {"Local Almanac", "5000000001"}).configWrites.empty());

    // Reordered: the existing strings move with their mods; a new mod gets
    // the launcher's form.
    const LaunchPlan plan = f.plan(inst, mods, {"5000000001", "5000000002", "Local Almanac"});
    REQUIRE(plan.configWrites.size() == 1);
    CHECK(plan.configWrites[0].contents ==
          "{\n  \"enabledMods\": [\n    {\n      \"path\": \"" + ledger +
              "\"\n    },\n    {\n      \"path\": \"" + f.jsonPath(f.content("5000000002")) +
              "\"\n    },\n    {\n      \"path\": \"" + almanac +
              "\"\n    }\n  ],\n  \"disabledDLC\": [],\n  \"enabledUGC\": []\n}");
}

TEST_CASE("Victoria 3 launch plans refuse what the game cannot load", "[games][vic3]") {
    Vic3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const std::string before = test::readText(f.contentLoad());
    const auto error = [&](std::vector<InstalledMod> ordered) {
        auto result = f.adapter.buildLaunchPlan(inst, ordered);
        REQUIRE_FALSE(result.ok());
        return result.error();
    };

    InstalledMod other = f.mod(mods, "Local Almanac");
    other.identity.game = "ck3";
    CHECK(error({other}).code == ErrorCode::WrongGame);

    const Error twice = error({f.mod(mods, "5000000001"), f.mod(mods, "5000000001")});
    CHECK(twice.code == ErrorCode::InvalidArgument);
    CHECK(twice.message == "The mod \"Example Ledger\" is listed twice.");

    InstalledMod gone = f.mod(mods, "Local Almanac");
    fs::remove_all(gone.contentPath);
    const Error missing = error({gone});
    CHECK(missing.code == ErrorCode::NotFound);
    CHECK(missing.message ==
          "The files of the mod \"Local Almanac\" are missing. Rescan or reinstall it.");

    InstalledMod odd = f.mod(mods, "5000000001");
    odd.identity.source = "paradox-mods";
    CHECK(error({odd}).code == ErrorCode::InvalidArgument);
    odd.identity = ModIdentity{"vic3", "steam-workshop", "0123"};
    CHECK(error({odd}).code == ErrorCode::InvalidArgument);
    CHECK(test::readText(f.contentLoad()) == before);
}

TEST_CASE("Victoria 3 compatibility warnings follow supported_game_version", "[games][vic3]") {
    Vic3Fixture f;
    const GameInstallation inst = f.discovered();
    const std::vector<InstalledMod> mods = f.scan(inst);
    const auto warnings = [&](std::string_view id) {
        return f.adapter.compatibilityWarnings(inst, f.mod(mods, id));
    };

    CHECK(warnings("5000000001").empty()); // "1.15.*"
    CHECK(warnings("5000000003").empty()); // ""
    CHECK(warnings("5000000002") ==
          std::vector<std::string>{"Made for Victoria 3 1.7.*; the installed version is 1.15.2."});

    // Which wildcards the game rejects is not known yet: none is reported.
    InstalledMod custom = f.mod(mods, "5000000001");
    for (const char* accepted : {"1.15.2", "1.15", "1.*", "*", "1.*.*"}) {
        custom.descriptor.supportedVersion = accepted;
        CHECK(f.adapter.compatibilityWarnings(inst, custom).empty());
    }
    custom.descriptor.supportedVersion = "Ivan-Chai";
    CHECK(f.adapter.compatibilityWarnings(inst, custom) ==
          std::vector<std::string>{
              "The supported_game_version \"Ivan-Chai\" is not a version pattern."});
}

TEST_CASE("Victoria 3 process names and load-order information", "[games][vic3]") {
    Vic3Adapter adapter;
    GameInstallation inst;
    inst.executablePath = u8path("C:/Games/Victoria 3/binaries/victoria3.exe");
    // The console wrapper starts victoria3.exe and counts as the game.
    ProcessNames names = adapter.processNames(inst);
    CHECK(names.game == std::vector<std::string>{"victoria3.exe", "victoria3_win_console.exe"});
    CHECK(names.conflictingWriters ==
          std::vector<std::string>{"Paradox Launcher.exe", "dowser.exe"});
    inst.executablePath.clear();
    CHECK(adapter.processNames(inst).game ==
          std::vector<std::string>{"victoria3.exe", "victoria3_win_console.exe"});

    DiscoveryEnvironment env;
    env.documentsDir = u8path("/docs");
    CHECK(adapter.defaultUserDataPath(env) == u8path("/docs/Paradox Interactive/Victoria 3"));

    // Not checked in the game yet: the note says so above a Victoria 3 playset.
    const LoadOrderInfo info = adapter.loadOrderInfo();
    CHECK_FALSE(info.verifiedInGame);
    CHECK(info.userNote == "It is not yet verified that Victoria 3 follows this order when two "
                           "mods change the same file.");
    CHECK(info.summary.find("content_load.json enabledMods, index 0 first") != std::string::npos);
    CHECK(info.summary.find("later in the list wins") != std::string::npos);
    CHECK(info.evidence.find("not yet verified") != std::string::npos);
}

TEST_CASE("Paradox Mods folders and the launcher's path form", "[games][vic3]") {
    for (const char* name : {"pdx_123", "pdx_1", "pdx_12_3"})
        CHECK(isParadoxModsFolder(name));
    for (const char* name : {"pdx_", "pdx_a", "pdx_12_", "pdx_1_2_3", "PDX_123", "pdx_123 copy",
                             "my pdx_123", "Local Almanac"})
        CHECK_FALSE(isParadoxModsFolder(name));

    CHECK(contentLoadPath(u8path("c:/Users/Player/Documents/Paradox Interactive/Victoria 3/mod/"
                                 "Local Almanac/")) ==
          "C:\\Users\\Player\\Documents\\Paradox Interactive\\Victoria 3\\mod\\Local Almanac");
    CHECK(contentLoadPath(u8path("D:\\Steam\\steamapps\\workshop\\content\\529340\\5000000001")) ==
          "D:\\Steam\\steamapps\\workshop\\content\\529340\\5000000001");
    CHECK(contentLoadPath(u8path("D:/a/./b/../Mod.zip")) == "D:\\a\\Mod.zip");
}
