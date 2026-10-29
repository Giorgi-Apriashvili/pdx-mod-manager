#include "core/dependencies.hpp"
#include "playset_test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <vector>

using namespace pmm;
using Catch::Matchers::ContainsSubstring;
using test::add;
using test::installed;
using test::workshop;

namespace {

// Descriptor names deliberately differ from the playset name hints: matching
// uses the installed descriptor name.
struct Fixture {
    PlaysetEntry core = workshop("10", "core (hint)");
    PlaysetEntry addon = workshop("20", "addon (hint)");
    PlaysetEntry patch = workshop("30", "patch (hint)");
    InstalledModMap inventory;

    Fixture() {
        add(inventory, installed(core.identity, "UI Overhaul Dynamic"));
        add(inventory, installed(addon.identity, "UI Overhaul Dynamic - Extended Topbar",
                                 {"UI Overhaul Dynamic"}));
        add(inventory,
            installed(patch.identity, "Compatibility Patch",
                      {"UI Overhaul Dynamic", " Not Installed Mod ", "Installed Elsewhere"}));
        // Installed but not part of the test playsets.
        add(inventory, installed({"stellaris", "steam-workshop", "40"}, "Installed Elsewhere"));
        // Same name, other game: must not satisfy anything for Stellaris.
        add(inventory, installed({"eu4", "steam-workshop", "50"}, "Not Installed Mod"));
    }
};

} // namespace

TEST_CASE("Dependencies loading first raise no issues", "[core][dependencies]") {
    Fixture f;
    const std::vector<PlaysetEntry> entries{f.core, f.addon};
    CHECK(checkDependencies(entries, f.inventory).empty());
}

TEST_CASE("A dependency ordered after its dependent is reported", "[core][dependencies]") {
    Fixture f;
    const std::vector<PlaysetEntry> entries{f.addon, f.core};
    const auto issues = checkDependencies(entries, f.inventory);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].entryIndex == 0);
    CHECK(issues[0].kind == DependencyIssueKind::OrderedAfter);
    CHECK(issues[0].dependencyIndex == 1);
    CHECK(issues[0].dependencyName == "UI Overhaul Dynamic");
    CHECK_THAT(issues[0].message, ContainsSubstring("must load before this mod"));
    CHECK_THAT(issues[0].message, ContainsSubstring("#2"));
}

TEST_CASE("Disabled, uninstalled and out-of-playset dependencies are reported",
          "[core][dependencies]") {
    Fixture f;
    f.core.enabled = false;
    const std::vector<PlaysetEntry> entries{f.core, f.patch};
    const auto issues = checkDependencies(entries, f.inventory);
    REQUIRE(issues.size() == 3);
    CHECK(issues[0].kind == DependencyIssueKind::Disabled);
    CHECK(issues[0].dependencyIndex == 0);
    CHECK(issues[1].kind == DependencyIssueKind::NotInstalled);
    CHECK(issues[1].dependencyName == "Not Installed Mod"); // trimmed
    CHECK_FALSE(issues[1].dependencyIndex.has_value());
    CHECK(issues[2].kind == DependencyIssueKind::NotInPlayset);
    CHECK_THAT(issues[2].message, ContainsSubstring("installed but not in this playset"));
    for (const auto& issue : issues)
        CHECK(issue.entryIndex == 1);
}

TEST_CASE("Disabled or uninstalled dependents are not checked", "[core][dependencies]") {
    Fixture f;
    f.patch.enabled = false;
    PlaysetEntry unknown = workshop("99", "Not installed, unknown dependencies");
    const std::vector<PlaysetEntry> entries{f.patch, unknown};
    CHECK(checkDependencies(entries, f.inventory).empty());
}

TEST_CASE("Name matching prefers exact names and falls back to ASCII case folding",
          "[core][dependencies]") {
    Fixture f;
    f.inventory[f.addon.identity].descriptor.dependencies = {"ui overhaul DYNAMIC"};
    const std::vector<PlaysetEntry> ok{f.core, f.addon};
    CHECK(checkDependencies(ok, f.inventory).empty());

    // An uninstalled playset entry is matched by its name hint.
    PlaysetEntry hinted = workshop("77", "UI Overhaul Dynamic");
    InstalledModMap inventory;
    add(inventory, installed(f.addon.identity, "Addon", {"UI Overhaul Dynamic"}));
    const std::vector<PlaysetEntry> entries{hinted, f.addon};
    CHECK(checkDependencies(entries, inventory).empty());
}

TEST_CASE("A mod listing its own name is not a missing dependency", "[core][dependencies]") {
    Fixture f;
    f.inventory[f.core.identity].descriptor.dependencies = {"UI Overhaul Dynamic"};
    const std::vector<PlaysetEntry> entries{f.core};
    CHECK(checkDependencies(entries, f.inventory).empty());
}

TEST_CASE("Bulk enable reports only the issues it introduces", "[core][dependencies]") {
    Fixture f;
    f.core.enabled = false;
    f.addon.enabled = false;

    SECTION("enabling a dependent whose dependency is disabled") {
        const std::vector<PlaysetEntry> entries{f.core, f.addon};
        const auto introduced =
            dependencyIssuesIntroducedByEnabling(entries, f.inventory, std::vector<std::size_t>{1});
        REQUIRE(introduced.size() == 1);
        CHECK(introduced[0].kind == DependencyIssueKind::Disabled);
        CHECK(introduced[0].entryIndex == 1);
        CHECK_FALSE(entries[0].enabled); // nothing is activated automatically
    }
    SECTION("enabling both in a valid order introduces nothing") {
        const std::vector<PlaysetEntry> entries{f.core, f.addon};
        CHECK(dependencyIssuesIntroducedByEnabling(entries, f.inventory,
                                                   std::vector<std::size_t>{0, 1, 9})
                  .empty());
    }
    SECTION("enabling a dependency that loads too late turns one issue into another") {
        f.addon.enabled = true;
        const std::vector<PlaysetEntry> entries{f.addon, f.core};
        REQUIRE(checkDependencies(entries, f.inventory).front().kind ==
                DependencyIssueKind::Disabled);
        const auto introduced =
            dependencyIssuesIntroducedByEnabling(entries, f.inventory, std::vector<std::size_t>{1});
        REQUIRE(introduced.size() == 1);
        CHECK(introduced[0].kind == DependencyIssueKind::OrderedAfter);
        CHECK(introduced[0].entryIndex == 0);
    }
    SECTION("existing issues are not repeated") {
        f.patch.enabled = true;
        const std::vector<PlaysetEntry> entries{f.core, f.patch, f.addon};
        const auto introduced =
            dependencyIssuesIntroducedByEnabling(entries, f.inventory, std::vector<std::size_t>{2});
        REQUIRE(introduced.size() == 1);
        CHECK(introduced[0].entryIndex == 2);
    }
}
