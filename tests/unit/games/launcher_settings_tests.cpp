#include "games/launcher_settings.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::games;
using pmm::test::TempDir;

namespace {

std::string stellarisSettingsText() {
    return test::readText(
        test::fixturePath("games/stellaris/Steam/steamapps/common/Stellaris/launcher-settings.json"));
}

} // namespace

TEST_CASE("launcher-settings.json is parsed", "[games][launcher_settings]") {
    auto settings = parseLauncherSettings(stellarisSettingsText());
    REQUIRE(settings.ok());
    CHECK(settings->gameId == "stellaris");
    CHECK(settings->version == "Cygnus v4.5.2 (9776)");
    CHECK(settings->rawVersion == "v4.5.2");
    CHECK(settings->distPlatform == "steam");
    CHECK(settings->gameDataPath == "%USER_DOCUMENTS%/Paradox Interactive/Stellaris");
    CHECK(settings->exePath == "./stellaris.exe");
    CHECK(settings->exeArgs == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(settings->file.empty());

    // Optional fields may be missing or null; unknown fields are ignored.
    auto minimal = parseLauncherSettings("\xEF\xBB\xBF{\"exePath\": \"eu4.exe\", \"exeArgs\": null, "
                                         "\"version\": null, \"unknown\": [1, {}]}");
    REQUIRE(minimal.ok());
    CHECK(minimal->exePath == "eu4.exe");
    CHECK(minimal->exeArgs.empty());
    CHECK(minimal->version.empty());
}

TEST_CASE("launcher-settings.json is validated", "[games][launcher_settings]") {
    struct Case {
        std::string json;
        ErrorCode code;
    };
    const auto c = GENERATE(
        Case{"{\"exePath\": ", ErrorCode::ParseError}, Case{"[1, 2]", ErrorCode::ValidationFailed},
        Case{"{\"exePath\": 5}", ErrorCode::ValidationFailed},
        Case{"{\"gameId\": {\"x\": 1}}", ErrorCode::ValidationFailed},
        Case{"{\"exeArgs\": \"-gdpr-compliant\"}", ErrorCode::ValidationFailed},
        Case{"{\"exeArgs\": [\"-a\", 3]}", ErrorCode::ValidationFailed},
        Case{"{\"exeArgs\": [[\"-a\"]]}", ErrorCode::ValidationFailed},
        Case{"{\"exeArgs\": [\"a\\u0000b\"]}", ErrorCode::ValidationFailed},
        Case{"{\"exePath\": \"x\\u0000.exe\"}", ErrorCode::ValidationFailed});
    CAPTURE(c.json);
    const auto result = parseLauncherSettings(c.json);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == c.code);

    std::string many = "{\"exeArgs\": [";
    for (std::size_t i = 0; i <= kMaxLauncherArguments; ++i)
        many += (i == 0 ? "\"-x\"" : ",\"-x\"");
    many += "]}";
    CHECK(parseLauncherSettings(many).error().code == ErrorCode::ValidationFailed);
    CHECK(parseLauncherSettings("{\"exeArgs\": [\"" + std::string(kMaxLauncherArgumentBytes + 1, 'a') +
                                "\"]}")
              .error()
              .code == ErrorCode::ValidationFailed);
}

TEST_CASE("launcher-settings.json is found in the modern and the older layout",
          "[games][launcher_settings]") {
    TempDir temp;
    const fs::path install = temp.path() / test::u8path("Ünïcødé dir/日本語 game");
    test::writeText(install / "stellaris.exe", "placeholder");
    CHECK_FALSE(findLauncherSettings(install).has_value());
    CHECK(readLauncherSettings(install).error().code == ErrorCode::NotFound);

    // Older layout: launcher/launcher-settings.json with exePath relative to it.
    test::writeText(install / "launcher" / "launcher-settings.json",
                    "{\"gameId\": \"stellaris\", \"exePath\": \"../stellaris.exe\"}");
    auto older = readLauncherSettings(install);
    REQUIRE(older.ok());
    CHECK(older->file == install / "launcher" / "launcher-settings.json");
    auto exe = resolveLauncherExecutable(*older, install, OsPlatform::Windows);
    REQUIRE(exe.ok());
    CHECK(*exe == (install / "stellaris.exe").lexically_normal());

    // The modern location wins when both exist.
    test::writeText(install / "launcher-settings.json", stellarisSettingsText());
    auto modern = readLauncherSettings(install);
    REQUIRE(modern.ok());
    CHECK(modern->file == install / "launcher-settings.json");
    exe = resolveLauncherExecutable(*modern, install, OsPlatform::Windows);
    REQUIRE(exe.ok());
    CHECK(*exe == (install / "stellaris.exe").lexically_normal());

    // Malformed files report the file in the technical detail.
    test::writeText(install / "launcher-settings.json", "{ nope");
    auto broken = readLauncherSettings(install);
    REQUIRE_FALSE(broken.ok());
    CHECK(broken.error().code == ErrorCode::ParseError);
    CHECK(broken.error().detail.find("launcher-settings.json") != std::string::npos);
}

TEST_CASE("The launcher executable must be a regular file inside the installation",
          "[games][launcher_settings]") {
    TempDir temp;
    const fs::path install = temp.path() / test::u8path("game dir");
    test::writeText(install / "game.exe", "placeholder");
    test::writeText(install / "run.bat", "placeholder");
    test::writeText(install / "linuxgame", "placeholder");
    test::writeText(temp.path() / "outside.exe", "placeholder");
    fs::create_directories(install / "folder.exe");
    test::writeText(install / "game.app" / "Contents" / "MacOS" / "game", "placeholder");

    LauncherSettings settings;
    settings.file = install / "launcher-settings.json";
    const auto resolve = [&](std::string exePath, OsPlatform os) {
        settings.exePath = std::move(exePath);
        return resolveLauncherExecutable(settings, install, os);
    };

    CHECK(resolve("./game.exe", OsPlatform::Windows).ok());
    CHECK(resolve("sub/../game.exe", OsPlatform::Windows).ok());
    CHECK(resolve("../outside.exe", OsPlatform::Windows).error().code ==
          ErrorCode::ValidationFailed);
    CHECK(resolve(platform::pathToUtf8(temp.path() / "outside.exe"), OsPlatform::Windows)
              .error()
              .code == ErrorCode::ValidationFailed);
    CHECK(resolve("./missing.exe", OsPlatform::Windows).error().code == ErrorCode::NotFound);
    CHECK(resolve("./folder.exe", OsPlatform::Windows).error().code == ErrorCode::NotFound);
    CHECK(resolve("./run.bat", OsPlatform::Windows).error().code == ErrorCode::ValidationFailed);
    CHECK(resolve("", OsPlatform::Windows).error().code == ErrorCode::ValidationFailed);

    auto native = resolve("./linuxgame", OsPlatform::Linux);
    REQUIRE(native.ok());
    CHECK(*native == (install / "linuxgame").lexically_normal());

    auto bundle = resolve("./game.app", OsPlatform::MacOS);
    REQUIRE(bundle.ok());
    CHECK(*bundle == (install / "game.app" / "Contents" / "MacOS" / "game").lexically_normal());
}

TEST_CASE("gameDataPath maps %USER_DOCUMENTS% onto the documents folder",
          "[games][launcher_settings]") {
    const fs::path docs = test::u8path("/home/Ünïcødé user/Documents");
    auto mapped = resolveGameDataPath("%USER_DOCUMENTS%/Paradox Interactive/Stellaris", docs);
    REQUIRE(mapped.has_value());
    CHECK(*mapped == docs / "Paradox Interactive" / "Stellaris");
    CHECK(resolveGameDataPath("%USER_DOCUMENTS%\\Paradox Interactive\\Stellaris\\", docs) ==
          docs / "Paradox Interactive" / "Stellaris");
    CHECK(resolveGameDataPath("%USER_DOCUMENTS%", docs) == docs);

    CHECK_FALSE(resolveGameDataPath("%USER_DOCUMENTS%/../secret", docs).has_value());
    CHECK_FALSE(resolveGameDataPath("%USER_DOCUMENTS%/C:/x", docs).has_value());
    CHECK_FALSE(resolveGameDataPath("%USER_DOCUMENTS%x/y", docs).has_value());
    CHECK_FALSE(resolveGameDataPath("%APPDATA%/Paradox Interactive/Stellaris", docs).has_value());
    CHECK_FALSE(resolveGameDataPath("C:/Users/x/Documents/Paradox Interactive", docs).has_value());
    CHECK_FALSE(resolveGameDataPath("%USER_DOCUMENTS%/Paradox Interactive", {}).has_value());
}
