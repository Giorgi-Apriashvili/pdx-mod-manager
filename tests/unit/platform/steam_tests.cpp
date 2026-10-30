#include "platform/fs.hpp"
#include "platform/steam.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::platform;
using namespace pmm::platform::steam;
using pmm::test::TempDir;

namespace {

KeyValue parseFixture(std::string_view name) {
    auto document =
        readKeyValuesFile(test::fixturePath(std::string("platform/steam/") + std::string(name)));
    REQUIRE(document.ok());
    return std::move(document).value();
}

// Escapes a UTF-8 string for a quoted KeyValues value.
std::string vdfEscape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '\\' || c == '"')
            out += '\\';
        out += c;
    }
    return out;
}

std::string replaceAll(std::string text, std::string_view from, std::string_view to) {
    for (std::size_t pos = text.find(from); pos != std::string::npos;
         pos = text.find(from, pos + to.size()))
        text.replace(pos, from.size(), to);
    return text;
}

// Creates <library>/steamapps/appmanifest_<id>.acf from the Stellaris fixture
// with the given state flags, plus the install folder when requested.
fs::path installFixtureApp(const fs::path& library, const std::string& stateFlags,
                           bool createFolder) {
    std::string text = test::readText(test::fixturePath("platform/steam/appmanifest_281990.acf"));
    text = replaceAll(text, "\"StateFlags\"\t\t\"4\"", "\"StateFlags\"\t\t\"" + stateFlags + "\"");
    const fs::path acf = library / "steamapps" / "appmanifest_281990.acf";
    test::writeText(acf, text);
    if (createFolder)
        fs::create_directories(library / "steamapps" / "common" / "Stellaris");
    return acf;
}

} // namespace

TEST_CASE("KeyValues parser handles the tolerant parts of the grammar", "[platform][steam]") {
    const KeyValue document = parseFixture("keyvalues_edge.vdf"); // BOM + CRLF
    const KeyValue* root = document.child("root");                // case-insensitive
    REQUIRE(root != nullptr);
    CHECK(root->isBlock);
    CHECK(root->key == "Root");
    CHECK(root->line == 2);

    CHECK(root->childValueOr("Quoted") == "a \"quoted\" word");
    CHECK(root->childValueOr("Path") == "C:\\Games\\Ünïcødé 日本語");
    CHECK(root->childValueOr("Escapes") == "line1\nline2\ttab");
    CHECK(root->childValueOr("Literal") == "keep \\q backslash");
    CHECK(root->childValueOr("unquotedkey") == "unquoted_value");
    CHECK(root->childValueOr("Platform") == "windows"); // [$WIN32] ignored
    CHECK(root->childValueOr("Empty", "fallback").empty());
    CHECK(root->childValueOr("Missing", "fallback") == "fallback");
    CHECK_FALSE(root->childValue("Nested").has_value()); // a block, not a value
    CHECK(root->childValueOr("Multi\nline") == "value");
    CHECK(root->childValueOr("After") == "multiline");

    const KeyValue* leaf = document.find({"ROOT", "nested", "Deep", "leaf"});
    REQUIRE(leaf != nullptr);
    CHECK(leaf->value == "value");
    CHECK(leaf->line == 14);
    CHECK(document.find({"Root", "Nested", "Nope"}) == nullptr);

    // Duplicates keep file order; lookups return the first.
    CHECK(root->childValueOr("DUPLICATE") == "first");
    const auto count =
        std::count_if(root->children.begin(), root->children.end(),
                      [](const KeyValue& kv) { return keyEquals(kv.key, "duplicate"); });
    CHECK(count == 2);
}

TEST_CASE("KeyValues parser reports malformed input with the line", "[platform][steam]") {
    struct Case {
        std::string text;
        std::string where;
    };
    const auto c =
        GENERATE(Case{"\"a\" \"b\"\n\"c\" \"unterminated\n", "line 2"},
                 Case{"\"a\"\n{\n\"b\" \"c\"\n", "line 4"}, Case{"\"a\" \"b\"\n}\n", "line 2"},
                 Case{"\"a\" { \"b\" }", "line 1"}, Case{"{ \"a\" \"b\" }", "line 1"},
                 Case{"\"lonely\"", "line 1"}, Case{"\"a\" [$WIN32\n\"b\" \"c\"", "line 1"});
    CAPTURE(c.text);
    const auto result = parseKeyValues(c.text);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::ParseError);
    CHECK(result.error().message.find(c.where) != std::string::npos);
}

TEST_CASE("KeyValues parser survives hostile input", "[platform][steam]") {
    CHECK(parseKeyValues("").ok());
    CHECK(parseKeyValues("\xEF\xBB\xBF // only a comment").ok());
    std::string deep;
    for (int i = 0; i < 5000; ++i)
        deep += "\"k\" { ";
    const auto result = parseKeyValues(deep);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::ParseError);
}

TEST_CASE("readKeyValuesFile names the file in errors", "[platform][steam]") {
    TempDir temp;
    const fs::path file = temp / "broken file.vdf";
    test::writeText(file, "\"a\"\n{\n");
    const auto result = readKeyValuesFile(file);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::ParseError);
    CHECK(result.error().message.find("broken file.vdf") != std::string::npos);
    CHECK(result.error().message.find("line 3") != std::string::npos);

    const auto missing = readKeyValuesFile(temp / "missing.vdf");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::NotFound);
}

TEST_CASE("Library folders: modern format", "[platform][steam]") {
    const KeyValue document = parseFixture("libraryfolders.vdf");
    const fs::path root = pathFromUtf8("C:\\Program Files (x86)\\Steam");
    const auto libraries = parseLibraryFolders(document, root);
    REQUIRE(libraries.ok());
    REQUIRE(libraries->size() == 2);
    CHECK((*libraries)[0].path == root);
    CHECK((*libraries)[0].appIds == std::vector<std::string>{"228980", "281990"});
    CHECK(pathToUtf8((*libraries)[1].path) == "D:\\SteamLibrary");
    CHECK((*libraries)[1].label == "Games drive");
    CHECK((*libraries)[1].appIds == std::vector<std::string>{"236850"});
}

TEST_CASE("Library folders: legacy format always includes the root", "[platform][steam]") {
    const KeyValue document = parseFixture("libraryfolders_legacy.vdf");
    TempDir temp;
    const auto libraries = parseLibraryFolders(document, temp.path());
    REQUIRE(libraries.ok());
    REQUIRE(libraries->size() == 3);
    CHECK((*libraries)[0].path == temp.path());
    CHECK(pathToUtf8((*libraries)[1].path) == "D:\\SteamLibrary");
    CHECK(pathToUtf8((*libraries)[2].path) == "E:\\Spiele\\Steam Bibliothek");
    CHECK((*libraries)[2].appIds.empty());

    const auto wrong = parseLibraryFolders(parseFixture("appmanifest_281990.acf"), temp.path());
    REQUIRE_FALSE(wrong.ok());
    CHECK(wrong.error().code == ErrorCode::ParseError);
}

TEST_CASE("readLibraryFolders reads steamapps/libraryfolders.vdf", "[platform][steam]") {
    TempDir temp;
    const fs::path root = temp.path() / test::unicodeSubdir() / "Steam";
    fs::create_directories(root / "steamapps");

    SECTION("missing file yields just the root") {
        const auto libraries = readLibraryFolders(root);
        REQUIRE(libraries.ok());
        REQUIRE(libraries->size() == 1);
        CHECK((*libraries)[0].path == root);
    }

    SECTION("the root entry is merged, not duplicated") {
        std::string text = test::readText(test::fixturePath("platform/steam/libraryfolders.vdf"));
        text = replaceAll(text, "C:\\\\Program Files (x86)\\\\Steam", vdfEscape(pathToUtf8(root)));
        test::writeText(root / "steamapps" / "libraryfolders.vdf", text);
        const auto libraries = readLibraryFolders(root);
        REQUIRE(libraries.ok());
        REQUIRE(libraries->size() == 2);
        CHECK((*libraries)[0].path == root);
        CHECK((*libraries)[0].appIds.size() == 2);
        CHECK(pathToUtf8((*libraries)[1].path) == "D:\\SteamLibrary");
    }
}

TEST_CASE("readAppManifest reads the fields used for discovery", "[platform][steam]") {
    TempDir temp;
    const fs::path library = temp.path() / test::unicodeSubdir() / "Steam Library";
    const fs::path acf = installFixtureApp(library, "4", false);

    const auto manifest = readAppManifest(acf);
    REQUIRE(manifest.ok());
    CHECK(manifest->appId == "281990");
    CHECK(manifest->name == "Stellaris");
    CHECK(manifest->installDir == "Stellaris");
    CHECK(manifest->buildId == "25664273");
    CHECK(manifest->stateFlags == 4);
    CHECK(manifest->fullyInstalled());
    CHECK_FALSE(manifest->updateRequired());
    CHECK(manifest->lastUpdated == 1791281883);
    CHECK(manifest->sizeOnDisk == 30972215062ull);
    CHECK(manifest->libraryRoot == library);
    CHECK(manifest->manifestPath == acf);
    CHECK(manifest->installPath() == library / "steamapps" / "common" / "Stellaris");
}

TEST_CASE("readAppManifest rejects malformed manifests", "[platform][steam]") {
    TempDir temp;
    const fs::path acf = temp / "appmanifest_1.acf";
    const auto installDir = GENERATE(std::string(""), std::string(".."), std::string("..\\\\evil"),
                                     std::string("a/b"), std::string("C:"));
    test::writeText(acf, "\"AppState\" { \"appid\" \"1\" \"installdir\" \"" + installDir + "\" }");
    const auto manifest = readAppManifest(acf);
    REQUIRE_FALSE(manifest.ok());
    CHECK(manifest.error().code == ErrorCode::ValidationFailed);

    test::writeText(acf, "\"Other\" { }");
    CHECK(readAppManifest(acf).error().code == ErrorCode::ParseError);
    test::writeText(acf, "\"AppState\" { \"appid\" \"12x\" \"installdir\" \"x\" }");
    CHECK(readAppManifest(acf).error().code == ErrorCode::ParseError);
}

TEST_CASE("findInstalledApp prefers a complete installation", "[platform][steam]") {
    TempDir temp;
    const fs::path stale = temp / "Stale Library";
    const fs::path good = temp.path() / test::unicodeSubdir() / "Good Library";
    const fs::path empty = temp / "Empty Library";
    installFixtureApp(stale, "2", false); // update required, folder missing
    installFixtureApp(good, "4", true);
    fs::create_directories(empty / "steamapps");

    const std::vector<LibraryFolder> libraries = {{empty, {}, {}}, {stale, {}, {}}, {good, {}, {}}};
    const auto found = findInstalledApp(libraries, "281990");
    REQUIRE(found.ok());
    CHECK(found->libraryRoot == good);
    CHECK(fs::is_directory(found->installPath()));

    const auto staleOnly = findInstalledApp({{stale, {}, {}}}, "281990");
    REQUIRE(staleOnly.ok());
    CHECK(staleOnly->updateRequired());
    CHECK_FALSE(staleOnly->fullyInstalled());

    const auto missing = findInstalledApp(libraries, "236850");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::NotFound);

    const auto invalid = findInstalledApp(libraries, "../281990");
    REQUIRE_FALSE(invalid.ok());
    CHECK(invalid.error().code == ErrorCode::InvalidArgument);
}

TEST_CASE("Workshop paths follow the Steam layout", "[platform][steam]") {
    const fs::path library = pathFromUtf8("/games/Steam Library");
    CHECK(toGenericUtf8(workshopContentDir(library, "281990")) ==
          "/games/Steam Library/steamapps/workshop/content/281990");
    CHECK(toGenericUtf8(appWorkshopManifestPath(library, "281990")) ==
          "/games/Steam Library/steamapps/workshop/appworkshop_281990.acf");
    CHECK(isValidAppId("281990"));
    CHECK_FALSE(isValidAppId(""));
    CHECK_FALSE(isValidAppId("28199a"));
    CHECK_FALSE(isValidAppId("123456789012345678901"));
}

TEST_CASE("readAppWorkshopManifest reads installed items and pending state", "[platform][steam]") {
    const auto manifest =
        readAppWorkshopManifest(test::fixturePath("platform/steam/appworkshop_281990.acf"));
    REQUIRE(manifest.ok());
    CHECK(manifest->appId == "281990");
    CHECK(manifest->sizeOnDisk == 233409630u);
    CHECK(manifest->needsUpdate);
    CHECK_FALSE(manifest->needsDownload);
    CHECK(manifest->timeLastUpdated == 1793271176);
    CHECK(manifest->lastBuildId == "25664273");
    REQUIRE(manifest->items.size() == 4);
    CHECK(manifest->items[0].id == "703156866");
    CHECK(manifest->items[1].id == "1623423360");
    CHECK(manifest->items[2].id == "1628912584");
    CHECK(manifest->items[3].id == "3090328185");

    const WorkshopItemState* current = manifest->findItem("1623423360");
    REQUIRE(current != nullptr);
    CHECK(current->installed);
    CHECK(current->size == 231762225u);
    CHECK(current->timeUpdated == 1791477347);
    CHECK(current->manifest == "1070831328046085548");
    CHECK(current->subscribedBy == "100000001");
    CHECK_FALSE(current->hasPendingUpdate());

    const WorkshopItemState* outdated = manifest->findItem("1628912584");
    REQUIRE(outdated != nullptr);
    CHECK(outdated->hasPendingUpdate());
    CHECK(outdated->latestTimeUpdated == 1792511498);
    CHECK(outdated->subscribedBy.empty()); // no longer subscribed

    const WorkshopItemState* requested = manifest->findItem("3090328185");
    REQUIRE(requested != nullptr);
    CHECK_FALSE(requested->installed);
    CHECK(requested->hasPendingUpdate());
    CHECK(manifest->findItem("1") == nullptr);

    TempDir temp;
    test::writeText(temp / "not workshop.acf", "\"AppState\" { }");
    const auto wrong = readAppWorkshopManifest(temp / "not workshop.acf");
    REQUIRE_FALSE(wrong.ok());
    CHECK(wrong.error().code == ErrorCode::ParseError);
}

TEST_CASE("findSteamRoot uses the first candidate with steamapps", "[platform][steam]") {
    TempDir temp;
    const fs::path notSteam = temp / "Not Steam";
    const fs::path steam = temp.path() / test::unicodeSubdir() / "Steam";
    fs::create_directories(notSteam);
    fs::create_directories(steam / "steamapps");

    const auto found = findSteamRoot({temp / "missing", notSteam, fs::path(), steam});
    REQUIRE(found.ok());
    CHECK(fs::equivalent(*found, steam));
    CHECK(looksLikeSteamRoot(steam));
    CHECK_FALSE(looksLikeSteamRoot(notSteam));

    const auto none = findSteamRoot({temp / "missing", notSteam});
    REQUIRE_FALSE(none.ok());
    CHECK(none.error().code == ErrorCode::NotFound);
    CHECK(none.error().detail.find("Not Steam") != std::string::npos);
}

TEST_CASE("defaultSteamRootCandidates covers the OS default", "[platform][steam]") {
    const auto candidates = defaultSteamRootCandidates();
    REQUIRE_FALSE(candidates.empty());
    CHECK(std::find(candidates.begin(), candidates.end(),
                    fs::path(L"C:\\Program Files (x86)\\Steam")) != candidates.end());
}

TEST_CASE("isSteamRunning recognises the client", "[platform][steam]") {
    const std::vector<RunningProcess> windows = {{10, "explorer.exe", {}}, {20, "Steam.exe", {}}};
    CHECK(isSteamRunning(windows));

    // A registry pid counts only when the process name is unknown.
    CHECK(isSteamRunning({{99, "", {}}}, 99));
    CHECK_FALSE(isSteamRunning({{99, "notepad.exe", {}}}, 99));
    CHECK_FALSE(isSteamRunning({}, 99));
}

TEST_CASE("findSteamApiLibrary searches the usual folders", "[platform][steam]") {
    CHECK(std::string(steamApiLibraryName()) == "steam_api64.dll");

    TempDir temp;
    const fs::path game = temp.path() / test::unicodeSubdir() / "Game Folder";
    fs::create_directories(game);
    CHECK_FALSE(findSteamApiLibrary(game).has_value());
    CHECK_FALSE(findSteamApiLibrary(fs::path()).has_value());

    test::writeText(game / "steam_api64.dll", "x");
    CHECK(findSteamApiLibrary(game) == game / "steam_api64.dll");
}

// Read-only smoke test against the Steam installation of the machine running
// the tests. Hidden from the default run (not hermetic); run it explicitly
// with `pmm_platform_tests "[.steam-real]"`.
TEST_CASE("Real Steam installation is discovered read-only", "[.steam-real]") {
    const auto root = findSteamRoot();
    if (!root.ok()) {
        SKIP("Steam is not installed: " + root.error().detail);
    }
    const auto libraries = readLibraryFolders(*root);
    REQUIRE(libraries.ok());
    REQUIRE_FALSE(libraries->empty());
    for (const auto& library : *libraries)
        UNSCOPED_INFO("library " << pathToUtf8(library.path) << " apps=" << library.appIds.size());
    const auto stellaris = findInstalledApp(*libraries, "281990");
    if (stellaris.ok()) {
        UNSCOPED_INFO("stellaris " << pathToUtf8(stellaris->installPath()) << " build "
                                   << stellaris->buildId << " flags " << stellaris->stateFlags);
        const auto workshop = readAppWorkshopManifest(
            appWorkshopManifestPath(stellaris->libraryRoot, stellaris->appId));
        if (workshop.ok())
            UNSCOPED_INFO("workshop items " << workshop->items.size());
        const auto api = findSteamApiLibrary(stellaris->installPath());
        UNSCOPED_INFO("steam api " << (api ? pathToUtf8(*api) : std::string("not found")));
    }
    UNSCOPED_INFO("steam running " << isSteamRunning());
    CHECK(true);
}
