// The check that ends a Workshop download (GameAdapter::verifyWorkshopContent)
// for each adapter family: it accepts what the family's scan can describe.

#include "core/utf8.hpp"
#include "games/ck3.hpp"
#include "games/stellaris.hpp"
#include "platform/steam.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::games;
using test::TempDir;
using test::u8path;
using test::writeText;

TEST_CASE("The dlc_load.json family's download check requires a usable descriptor",
          "[games][content-check]") {
    const StellarisAdapter adapter;
    TempDir dir("pmm verify");
    const fs::path base = dir.path() / u8path("Ünïcødé dir") / u8path("日本語");

    const fs::path good = base / "703156866";
    writeText(good / "descriptor.mod", "name=\"Cinematic Camera\"\n");
    CHECK(adapter.verifyWorkshopContent(good).ok());

    const fs::path noDescriptor = base / "2";
    writeText(noDescriptor / "common" / "x.txt", "x");
    auto missing = adapter.verifyWorkshopContent(noDescriptor);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::ValidationFailed);
    CHECK(missing.error().message.find("descriptor.mod") != std::string::npos);

    const fs::path emptyDescriptor = base / "3";
    writeText(emptyDescriptor / "descriptor.mod", "");
    CHECK_FALSE(adapter.verifyWorkshopContent(emptyDescriptor).ok());

    const fs::path descriptorDir = base / "4";
    fs::create_directories(descriptorDir / "descriptor.mod");
    CHECK_FALSE(adapter.verifyWorkshopContent(descriptorDir).ok());

    auto gone = adapter.verifyWorkshopContent(base / "5");
    REQUIRE_FALSE(gone.ok());
    CHECK(gone.error().code == ErrorCode::NotFound);
    CHECK(gone.error().message == "the content folder reported by Steam does not exist");

    writeText(base / "legacy_item.zip", "PK\x03\x04");
    CHECK(adapter.verifyWorkshopContent(base / "legacy_item.zip").ok());
    writeText(base / "empty_legacy.zip", "");
    auto emptyFile = adapter.verifyWorkshopContent(base / "empty_legacy.zip");
    REQUIRE_FALSE(emptyFile.ok());
    CHECK(emptyFile.error().message == "the downloaded file is empty");
}

TEST_CASE("Workshop content without descriptor.mod is described by its only .mod file",
          "[games][content-check]") {
    const Ck3Adapter adapter;
    TempDir dir("pmm verify sole");
    const fs::path base = dir.path() / u8path("Ünïcødé dir");
    const auto folder = [&](std::string_view id) {
        const fs::path path = base / u8path(std::string(id));
        writeText(path / "common" / "x.txt", "x");
        return path;
    };

    // "<Name>.mod" instead of descriptor.mod (seen for Crusader Kings III); the
    // extension in any case.
    const fs::path sole = folder("1");
    writeText(sole / u8path("Example_Stëel.MOD"), "name=\"Example Steel\"");
    CHECK(adapter.verifyWorkshopContent(sole).ok());

    const fs::path none = folder("2");
    writeText(none / "readme.txt", "no descriptor");
    auto missing = adapter.verifyWorkshopContent(none);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().message == "descriptor.mod is missing from the downloaded content");

    const fs::path two = folder("3");
    writeText(two / "A.mod", "name=\"A\"");
    writeText(two / "b.mod", "name=\"B\"");
    auto ambiguous = adapter.verifyWorkshopContent(two);
    REQUIRE_FALSE(ambiguous.ok());
    CHECK(ambiguous.error().code == ErrorCode::ValidationFailed);
    CHECK(ambiguous.error().message ==
          "descriptor.mod is missing from the downloaded content, and its 2 .mod files leave it "
          "open which one describes the mod");

    const fs::path emptySole = folder("4");
    writeText(emptySole / "Example.mod", "");
    auto empty = adapter.verifyWorkshopContent(emptySole);
    REQUIRE_FALSE(empty.ok());
    CHECK(empty.error().message == "Example.mod is empty");

    // descriptor.mod wins, also when it is the empty one.
    const fs::path both = folder("5");
    writeText(both / "descriptor.mod", "name=\"From descriptor.mod\"");
    writeText(both / "Other.mod", "name=\"Other\"");
    CHECK(adapter.verifyWorkshopContent(both).ok());
    const fs::path emptyDescriptor = folder("6");
    writeText(emptyDescriptor / "descriptor.mod", "");
    writeText(emptyDescriptor / "Other.mod", "name=\"Other\"");
    auto emptyWins = adapter.verifyWorkshopContent(emptyDescriptor);
    REQUIRE_FALSE(emptyWins.ok());
    CHECK(emptyWins.error().message == "descriptor.mod is empty");

    // A .zip marks a legacy zipped item, described inside its archive.
    const fs::path legacy = folder("7");
    writeText(legacy / "legacy.zip", "PK\x03\x04");
    writeText(legacy / "legacy.mod", "name=\"Legacy\"");
    auto zipped = adapter.verifyWorkshopContent(legacy);
    REQUIRE_FALSE(zipped.ok());
    CHECK(zipped.error().message == "descriptor.mod is missing from the downloaded content");

    // A folder named like a .mod file is not one.
    const fs::path directory = folder("8");
    fs::create_directories(directory / "Folder.mod");
    CHECK_FALSE(adapter.verifyWorkshopContent(directory).ok());
    writeText(directory / "Real.mod", "name=\"Real\"");
    CHECK(adapter.verifyWorkshopContent(directory).ok());
}

TEST_CASE("The dlc_load.json family's download check accepts what its scan describes",
          "[games][content-check]") {
    struct Layout {
        const char* id;
        std::vector<const char*> files;
        bool described;
    };
    const std::vector<Layout> layouts{
        {"4100000001", {"descriptor.mod"}, true},               // descriptor.mod
        {"4100000002", {"descriptor.mod", "Other.mod"}, true},  // and another .mod
        {"4100000003", {"descriptor.mod", "legacy.zip"}, true}, // and a .zip
        {"4100000004", {"Example_Steel.mod"}, true},            // one .mod
        {"4100000005", {"Example.MOD"}, true},                  // one upper-case .MOD
        {"4100000006", {"A.mod", "b.mod"}, false},              // two .mod
        {"4100000007", {"readme.txt"}, false},                  // no .mod
        {"4100000008", {"legacy.mod", "legacy.ZIP"}, false},    // one .mod and a .zip
    };
    const Ck3Adapter adapter;
    TempDir dir("pmm verify scan");
    GameInstallation inst;
    inst.game = "ck3";
    inst.userDataPath = dir.path() / "user data";
    inst.steamLibraryPath = dir.path() / "Steam";
    fs::create_directories(inst.userDataPath / "mod");
    const fs::path content = platform::steam::workshopContentDir(inst.steamLibraryPath, "1158310");
    for (const Layout& layout : layouts) {
        const fs::path folder = content / layout.id;
        writeText(folder / "common" / "x.txt", "x");
        for (const char* file : layout.files)
            writeText(folder / u8path(file), "name=\"Example\"");
    }
    auto mods = adapter.scanMods(inst);
    REQUIRE(mods.ok());
    REQUIRE(mods->size() == layouts.size());
    for (const Layout& layout : layouts) {
        INFO(layout.id);
        const InstalledMod* mod = test::findMod(*mods, layout.id);
        REQUIRE(mod != nullptr);
        CHECK((mod->state == InstallState::Ready) == layout.described);
        CHECK(adapter.verifyWorkshopContent(content / layout.id).ok() == layout.described);
    }
}

TEST_CASE("The download check reports paths with unpaired surrogates", "[games][content-check]") {
    TempDir dir("pmm verify surrogate");
    const fs::path folder = dir.path() / fs::path(L"caf\xD800");

    const StellarisAdapter adapter;
    Status gone = adapter.verifyWorkshopContent(folder / "703156866");
    REQUIRE_FALSE(gone.ok());
    CHECK(gone.error().code == ErrorCode::NotFound);
    CHECK(isValidUtf8(gone.error().detail));
    CHECK(gone.error().detail.find("caf\xEF\xBF\xBD") != std::string::npos);

    writeText(folder / "703156866" / "descriptor.mod", "name=\"x\"\n");
    CHECK(adapter.verifyWorkshopContent(folder / "703156866").ok());
    writeText(folder / "1" / "common" / "x.txt", "x");
    Status missing = adapter.verifyWorkshopContent(folder / "1");
    REQUIRE_FALSE(missing.ok());
    CHECK(isValidUtf8(missing.error().detail));
}
