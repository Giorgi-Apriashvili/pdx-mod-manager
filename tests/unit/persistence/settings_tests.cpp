#include "persistence/settings_repository.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

using namespace pmm;
using namespace pmm::persistence;
using namespace pmm::persistence::test;

TEST_CASE("Settings store, overwrite and remove UTF-8 values", "[persistence][settings]") {
    auto db = openMemory();
    SettingsRepository settings(*db);
    CHECK_FALSE(require(settings.get("ui.theme")).has_value());

    requireOk(settings.set("ui.theme", "light"));
    requireOk(settings.set("ui.theme", "dark"));
    requireOk(settings.set("library.path", "C:/Users/Example/Ünïcødé dir/日本語"));
    requireOk(settings.set("empty", ""));
    CHECK(require(settings.get("ui.theme")) == std::optional<std::string>("dark"));
    CHECK(require(settings.get("library.path")) ==
          std::optional<std::string>("C:/Users/Example/Ünïcødé dir/日本語"));
    CHECK(require(settings.get("empty")) == std::optional<std::string>(""));

    requireOk(settings.remove("ui.theme"));
    requireOk(settings.remove("ui.theme")); // absent keys are fine
    CHECK_FALSE(require(settings.get("ui.theme")).has_value());
    CHECK(require(settings.get("library.path")).has_value());
}

TEST_CASE("Typed setting helpers parse and validate", "[persistence][settings]") {
    auto db = openMemory();
    SettingsRepository settings(*db);
    CHECK_FALSE(require(settings.getInt("window.width")).has_value());
    CHECK_FALSE(require(settings.getBool("window.maximized")).has_value());

    requireOk(settings.setInt("window.width", 1440));
    requireOk(settings.setInt("big", std::numeric_limits<std::int64_t>::min()));
    requireOk(settings.setBool("window.maximized", true));
    CHECK(require(settings.getInt("window.width")) == std::optional<std::int64_t>(1440));
    CHECK(require(settings.getInt("big")) ==
          std::optional<std::int64_t>(std::numeric_limits<std::int64_t>::min()));
    CHECK(require(settings.get("window.maximized")) == std::optional<std::string>("true"));
    CHECK(require(settings.getBool("window.maximized")) == std::optional<bool>(true));

    requireOk(settings.set("legacy.flag", "0"));
    CHECK(require(settings.getBool("legacy.flag")) == std::optional<bool>(false));

    requireOk(settings.set("window.width", "12px"));
    auto badInt = settings.getInt("window.width");
    REQUIRE_FALSE(badInt.ok());
    CHECK(badInt.error().code == ErrorCode::ParseError);
    CHECK(badInt.valueOr(std::optional<std::int64_t>(800)) == std::optional<std::int64_t>(800));

    requireOk(settings.set("window.maximized", "yes"));
    auto badBool = settings.getBool("window.maximized");
    REQUIRE_FALSE(badBool.ok());
    CHECK(badBool.error().code == ErrorCode::ParseError);
}

TEST_CASE("Settings reject empty keys", "[persistence][settings]") {
    auto db = openMemory();
    SettingsRepository settings(*db);
    auto status = settings.set("", "x");
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().code == ErrorCode::InvalidArgument);
}
