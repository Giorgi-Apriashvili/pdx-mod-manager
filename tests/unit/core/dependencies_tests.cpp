#include "core/dependencies.hpp"
#include "playset_test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string_view>
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

namespace {

// A playset of `game` whose patch (#2) names one mod of every kind: one
// enabled before it, one enabled after it (#4), a disabled one (#3), one
// installed elsewhere, one not installed, and itself.
struct EveryKind {
    std::vector<PlaysetEntry> entries;
    InstalledModMap inventory;

    explicit EveryKind(const GameKey& game) {
        entries = {
            workshop("6000000001", "Base", true, game), workshop("6000000002", "Patch", true, game),
            workshop("6000000003", "Off", false, game), workshop("6000000004", "Late", true, game)};
        add(inventory, installed(entries[0].identity, "Example Base"));
        add(inventory, installed(entries[1].identity, "Example Patch",
                                 {"Example Base", "Example Late", "Example Off",
                                  "Example Elsewhere", "Example Missing", "Example Patch"}));
        add(inventory, installed(entries[2].identity, "Example Off"));
        add(inventory, installed(entries[3].identity, "Example Late"));
        add(inventory, installed({game, "steam-workshop", "6000000005"}, "Example Elsewhere"));
    }
};

} // namespace

TEST_CASE("CK3, EU4, Stellaris and Victoria 3 report each kind of dependency issue as before",
          "[core][dependencies]") {
    for (const char* game : {"ck3", "eu4", "stellaris", "vic3"}) {
        INFO(game);
        const EveryKind playset(game);
        const auto issues = checkDependencies(playset.entries, playset.inventory);
        REQUIRE(issues.size() == 4);
        CHECK(issues[0].kind == DependencyIssueKind::OrderedAfter);
        CHECK(issues[0].message == "Requires \"Example Late\" (#4), which must load before this "
                                   "mod (#2). Move it above this mod.");
        CHECK(issues[1].kind == DependencyIssueKind::Disabled);
        CHECK(issues[1].message == "Requires \"Example Off\" (#3), which is disabled in this "
                                   "playset.");
        CHECK(issues[2].kind == DependencyIssueKind::NotInPlayset);
        CHECK(issues[2].message == "Requires \"Example Elsewhere\", which is installed but not in "
                                   "this playset.");
        CHECK(issues[3].kind == DependencyIssueKind::NotInstalled);
        CHECK(issues[3].message == "Requires \"Example Missing\", which is not installed.");
        for (const DependencyIssue& issue : issues)
            CHECK(issue.entryIndex == 1);
    }
}

TEST_CASE("Hearts of Iron IV dependencies only order the load and raise no issues",
          "[core][dependencies]") {
    EveryKind playset("hoi4");
    CHECK(checkDependencies(playset.entries, playset.inventory).empty());
    // Enabling or disabling anything reports nothing either.
    playset.entries[1].enabled = false;
    CHECK(dependencyIssuesIntroducedByEnabling(playset.entries, playset.inventory,
                                               std::vector<std::size_t>{1, 2})
              .empty());
    CHECK_FALSE(playset.entries[2].enabled); // nothing is enabled automatically
}

TEST_CASE("Resolved dependencies say where each name leads, whatever the game",
          "[core][dependencies]") {
    for (const char* game : {"hoi4", "stellaris"}) {
        INFO(game);
        EveryKind playset(game);
        // Resolved for a disabled entry too.
        playset.entries[1].enabled = false;
        playset.entries.push_back(workshop("6000000009", "Not installed", true, game));
        const auto resolved = resolveDependencies(playset.entries, playset.inventory);
        REQUIRE(resolved.size() == 5);
        CHECK(resolved[0].empty());
        CHECK(resolved[4].empty()); // not installed: no descriptor
        using R = DependencyResolution;
        const std::vector<ResolvedDependency> expected{
            {"Example Base", R::EnabledEarlier, 0},   {"Example Late", R::EnabledLater, 3},
            {"Example Off", R::Disabled, 2},          {"Example Elsewhere", R::NotInPlayset, {}},
            {"Example Missing", R::NotInstalled, {}}, // and not itself
        };
        CHECK(resolved[1] == expected);
    }
}

TEST_CASE("Resolved names follow the matching rules of the checks", "[core][dependencies]") {
    // Trimmed, matched case-insensitively, listed once per mod (whatever the
    // spelling), and an earlier enabled entry wins over a later one of the
    // same name.
    const std::vector<PlaysetEntry> entries{workshop("6000000011", "Core", true, "hoi4"),
                                            workshop("6000000012", "Addon", true, "hoi4"),
                                            workshop("6000000013", "Core copy", true, "hoi4")};
    InstalledModMap inventory;
    add(inventory, installed(entries[0].identity, "Example Core"));
    add(inventory, installed(entries[1].identity, "Example Addon",
                             {" example CORE ", "example CORE", "Example Core", ""}));
    add(inventory, installed(entries[2].identity, "Example Core"));
    // Same name in another game: not a match for Hearts of Iron IV.
    add(inventory, installed({"stellaris", "steam-workshop", "1"}, "Example Other"));
    const auto resolved = resolveDependencies(entries, inventory);
    CHECK(resolved[1] == std::vector<ResolvedDependency>{
                             {"example CORE", DependencyResolution::EnabledEarlier, 0}});

    add(inventory, installed(entries[1].identity, "Example Addon", {"Example Other"}));
    CHECK(resolveDependencies(entries, inventory)[1] ==
          std::vector<ResolvedDependency>{
              {"Example Other", DependencyResolution::NotInstalled, std::nullopt}});
}

TEST_CASE("Spellings of one mod are resolved once, and an enabled copy wins over a disabled one",
          "[core][dependencies]") {
    for (const char* game : {"hoi4", "stellaris"}) {
        INFO(game);
        // #1 and #3 are copies of one name (#1 disabled); #4 is another,
        // disabled mod whose name differs from theirs only in case.
        const std::vector<PlaysetEntry> entries{workshop("6000000021", "Core", false, game),
                                                workshop("6000000022", "Addon", true, game),
                                                workshop("6000000023", "Core copy", true, game),
                                                workshop("6000000024", "Loud", false, game)};
        InstalledModMap inventory;
        add(inventory, installed(entries[0].identity, "Example Core"));
        add(inventory, installed(entries[1].identity, "Example Addon",
                                 {"example core", "Example Core", "EXAMPLE CORE", "Example Missing",
                                  "example MISSING"}));
        add(inventory, installed(entries[2].identity, "Example Core"));
        add(inventory, installed(entries[3].identity, "EXAMPLE CORE"));

        using R = DependencyResolution;
        CHECK(resolveDependencies(entries, inventory)[1] ==
              std::vector<ResolvedDependency>{{"example core", R::EnabledLater, 2},
                                              {"EXAMPLE CORE", R::Disabled, 3},
                                              {"Example Missing", R::NotInstalled, std::nullopt}});

        // The checks report every exact spelling, as before.
        const auto issues = checkDependencies(entries, inventory);
        if (std::string_view(game) == "hoi4") {
            CHECK(issues.empty());
            continue;
        }
        REQUIRE(issues.size() == 5);
        CHECK(issues[0].message == "Requires \"example core\" (#3), which must load before this "
                                   "mod (#2). Move it above this mod.");
        CHECK(issues[1].message == "Requires \"Example Core\" (#3), which must load before this "
                                   "mod (#2). Move it above this mod.");
        CHECK(issues[2].message == "Requires \"EXAMPLE CORE\" (#4), which is disabled in this "
                                   "playset.");
        CHECK(issues[3].message == "Requires \"Example Missing\", which is not installed.");
        CHECK(issues[4].message == "Requires \"example MISSING\", which is not installed.");
    }
}
