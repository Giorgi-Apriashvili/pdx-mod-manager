#include "core/import_plan.hpp"
#include "core/readiness.hpp"
#include "core/utf8.hpp"
#include "playset_test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

using namespace pmm;
using Catch::Matchers::ContainsSubstring;
using test::add;
using test::installed;
using test::local;
using test::workshop;

namespace {

// Requested setup mixing every reconciliation case, plus an inventory with
// extra installed mods that are not part of it.
struct Scenario {
    std::vector<PlaysetEntry> requested{
        workshop("100", "Installed enabled"),
        workshop("200", "Missing enabled"),
        workshop("300", "Missing disabled", false),
        local("their-local", "Local only", true),
        local("shared-local", "Local installed", false),
        workshop("600", "Broken disabled", false),
    };
    InstalledModMap inventory;

    Scenario() {
        add(inventory, installed(requested[0].identity, "Installed enabled"));
        add(inventory, installed(requested[4].identity, "Local installed"));
        add(inventory,
            installed(requested[5].identity, "Broken disabled", {}, InstallState::Broken));
        add(inventory, installed({"stellaris", "steam-workshop", "900"}, "Extra installed A"));
        add(inventory, installed({"stellaris", "local", "extra"}, "Extra installed B"));
    }

    ImportPlan plan(const std::vector<std::string>& existing = {}) const {
        return buildImportPlan("stellaris", "Shared setup", requested, inventory, existing,
                               "v4.1.5", "v4.1.5");
    }
};

} // namespace

TEST_CASE("Import reconciles installed, missing, disabled, broken and local-only entries",
          "[core][import]") {
    const Scenario s;
    const ImportPlan plan = s.plan();

    REQUIRE(plan.entries.size() == 6);
    CHECK(plan.entries[0].status == ImportEntryStatus::Installed);
    CHECK(plan.entries[1].status == ImportEntryStatus::Missing);
    CHECK(plan.entries[2].status == ImportEntryStatus::Missing);
    CHECK(plan.entries[3].status == ImportEntryStatus::LocalUnmapped);
    CHECK(plan.entries[4].status == ImportEntryStatus::Installed);
    CHECK(plan.entries[5].status == ImportEntryStatus::Broken);
    CHECK(plan.entries[0].installedName == "Installed enabled");

    const ImportCounts& c = plan.counts;
    CHECK(c.total == 6);
    CHECK(c.enabled == 3);
    CHECK(c.installed == 2);
    CHECK(c.missing == 2);
    CHECK(c.missingEnabled == 1);
    CHECK(c.missingDisabled == 1);
    CHECK(c.localUnmapped == 1);
    CHECK(c.broken == 1);

    // Disabled missing Workshop mods are acquired too; installed, broken and
    // local entries never are.
    CHECK(plan.toAcquire ==
          std::vector<ModIdentity>{s.requested[1].identity, s.requested[2].identity});
}

TEST_CASE("The imported playset is exactly the requested setup", "[core][import]") {
    const Scenario s;
    const Playset playset = s.plan().toPlayset();
    CHECK(playset.id == 0);
    CHECK(playset.game == "stellaris");
    CHECK(playset.name == "Shared setup");
    // Same order, ids and enabled flags; no extra installed mod activated, no
    // failed or missing entry dropped.
    CHECK(playset.entries == s.requested);
}

TEST_CASE("Proposed names never collide with existing playsets", "[core][import]") {
    CHECK(uniquePlaysetName("Setup", {}) == "Setup");
    CHECK(uniquePlaysetName("Setup", {"Other"}) == "Setup");
    CHECK(uniquePlaysetName("Setup", {"Setup"}) == "Setup (2)");
    CHECK(uniquePlaysetName("Setup", {"setup", "SETUP (2)"}) == "Setup (3)");
    CHECK(uniquePlaysetName("", {}) == "Imported playset");
    CHECK(uniquePlaysetName("日本語", {"日本語"}) == "日本語 (2)");

    const Scenario s;
    CHECK(s.plan({"Shared setup", "Shared setup (2)"}).proposedName == "Shared setup (3)");
    CHECK(s.plan({"Shared setup"}).requestedName == "Shared setup");
}

TEST_CASE("Proposed names stay within the name limit and cut on character boundaries",
          "[core][import]") {
    // 255 bytes: "a" + 127 two-byte characters. " (2)" only fits after cutting
    // whole characters (and never half of one).
    std::string longName = "a";
    for (int i = 0; i < 127; ++i)
        longName += "\xC3\xA9";
    REQUIRE(longName.size() == 255);
    CHECK(uniquePlaysetName(longName, {}) == longName);

    const std::string second = uniquePlaysetName(longName, {longName});
    CHECK(second.size() <= kMaxPlaysetNameBytes);
    CHECK(second.ends_with(" (2)"));
    CHECK(isValidUtf8(second));
    CHECK(longName.starts_with(second.substr(0, second.size() - 4)));
    CHECK(second.size() == 1 + 2 * 125 + 4); // two characters cut, not three bytes

    const std::string third = uniquePlaysetName(longName, {longName, second});
    CHECK(third.ends_with(" (3)"));
    CHECK(third.size() <= kMaxPlaysetNameBytes);

    // Whitespace left at the end of a shortened name is dropped.
    const std::string spaced = std::string(250, 'x') + "   yy";
    CHECK(uniquePlaysetName(spaced, {spaced}) == std::string(250, 'x') + " (2)");
    // A name that is too long by itself is shortened even without a clash.
    CHECK(uniquePlaysetName(std::string(300, 'z'), {}) == std::string(256, 'z'));

    // The suffix and counter stay whole; a custom limit is honoured.
    CHECK(uniquePlaysetName("Main", {}, " (copy)") == "Main (copy)");
    CHECK(uniquePlaysetName("Main", {"main (copy)"}, " (copy)") == "Main (copy) (2)");
    CHECK(uniquePlaysetName("Long playset", {}, " (copy)", 12) == "Long (copy)");
    CHECK(uniquePlaysetName("", {}, {}, 8) == "Imported");

    // The import plan proposes a name that fits as well.
    const Scenario s;
    ImportPlan plan =
        buildImportPlan("stellaris", longName, s.requested, s.inventory, {longName}, "", "");
    CHECK(plan.proposedName == second);
    CHECK(plan.toPlayset().name.size() <= kMaxPlaysetNameBytes);
}

TEST_CASE("Import warnings cover versions and always state the content caveat", "[core][import]") {
    Scenario s;
    ImportPlan plan = s.plan();
    REQUIRE_FALSE(plan.warnings.empty());
    CHECK(plan.warnings.back() == kIdenticalContentNotGuaranteed);
    for (const std::string& w : plan.warnings)
        CHECK_THAT(w, !ContainsSubstring("game version"));

    plan = buildImportPlan("stellaris", "P", s.requested, s.inventory, {}, "4.1.5", "v4.1.5");
    for (const std::string& w : plan.warnings)
        CHECK_THAT(w, !ContainsSubstring("game version")); // "v" prefix is not a difference

    plan = buildImportPlan("stellaris", "P", s.requested, s.inventory, {}, "v4.2.0", "v4.1.5");
    CHECK_THAT(plan.warnings.front(), ContainsSubstring("game version v4.1.5"));
    CHECK_THAT(plan.warnings.front(), ContainsSubstring("v4.2.0 is installed"));

    plan = buildImportPlan("stellaris", "P", s.requested, s.inventory, {}, "", "v4.1.5");
    CHECK_THAT(plan.warnings.front(), ContainsSubstring("could not be determined"));

    plan = buildImportPlan("stellaris", "P", s.requested, s.inventory, {}, "v4.2.0", "");
    CHECK(plan.warnings.back() == kIdenticalContentNotGuaranteed);
}

TEST_CASE("Mod version and fingerprint differences are visible", "[core][import]") {
    Scenario s;
    s.requested[0].versionHint = "1.0";
    s.inventory[s.requested[0].identity].descriptor.version = "1.2";
    s.requested[4].fingerprint = "sha256:" + std::string(64, 'a');
    s.inventory[s.requested[4].identity].fingerprint = "sha256:" + std::string(64, 'b');

    const ImportPlan plan = s.plan();
    CHECK(plan.entries[0].installedVersion == "1.2");
    REQUIRE_FALSE(plan.entries[0].warnings.empty());
    CHECK_THAT(plan.entries[0].warnings[0], ContainsSubstring("version 1.0"));
    REQUIRE_FALSE(plan.entries[4].warnings.empty());
    CHECK_THAT(plan.entries[4].warnings[0], ContainsSubstring("fingerprint"));

    bool versionNote = false;
    bool fingerprintNote = false;
    for (const std::string& w : plan.warnings) {
        versionNote = versionNote || w.find("different installed version") != std::string::npos;
        fingerprintNote = fingerprintNote || w.find("installed files differ") != std::string::npos;
    }
    CHECK(versionNote);
    CHECK(fingerprintNote);
    // Informational only: the entries keep the requested hints.
    CHECK(plan.toPlayset().entries == s.requested);
}

TEST_CASE("Unmapped local mods get suggestions but are never mapped automatically",
          "[core][import]") {
    Scenario s;
    s.requested[3].fingerprint = "sha256:" + std::string(64, 'c');
    InstalledMod sameContent = installed({"stellaris", "local", "renamed-copy"}, "Renamed copy");
    sameContent.fingerprint = s.requested[3].fingerprint;
    add(s.inventory, sameContent);
    add(s.inventory, installed({"stellaris", "local", "same-name"}, "Local only"));
    add(s.inventory, installed({"eu4", "local", "wrong-game"}, "Local only"));

    const ImportPlan plan = s.plan();
    const ImportPlanEntry& entry = plan.entries[3];
    CHECK(entry.status == ImportEntryStatus::LocalUnmapped);
    CHECK(entry.localCandidates == std::vector<ModIdentity>{{"stellaris", "local", "renamed-copy"},
                                                            {"stellaris", "local", "same-name"}});
    CHECK(plan.toPlayset().entries[3].identity.id == "their-local");
}

TEST_CASE("Duplicate requested identities are reported, kept, and acquired once",
          "[core][import]") {
    Scenario s;
    s.requested.push_back(s.requested[1]);
    const ImportPlan plan = s.plan();
    CHECK(plan.entries.size() == 7);
    CHECK(plan.toAcquire.size() == 2);
    bool reported = false;
    for (const std::string& w : plan.warnings)
        reported = reported || w.find("repeats") != std::string::npos;
    CHECK(reported);
}

TEST_CASE("Imported setups gate launching only on enabled entries", "[core][import]") {
    // End to end over core: plan -> playset -> readiness with acquisition jobs.
    const Scenario s;
    const Playset playset = s.plan().toPlayset();

    AcquisitionJob failedEnabled;
    failedEnabled.identity = s.requested[1].identity;
    failedEnabled.state = AcquisitionState::Failed;
    AcquisitionJob unavailableDisabled;
    unavailableDisabled.identity = s.requested[2].identity;
    unavailableDisabled.state = AcquisitionState::Unavailable;
    const std::vector<AcquisitionJob> jobs{failedEnabled, unavailableDisabled};

    auto readiness = computePlaysetReadiness(playset.entries, s.inventory, jobs);
    CHECK(readiness[1].status == EntryStatus::Failed);
    CHECK(readiness[2].status == EntryStatus::Unavailable);
    CHECK(readiness[3].status == EntryStatus::NeedsUserAction);
    CHECK(readiness[5].status == EntryStatus::Broken);

    LaunchReadiness launch = evaluateLaunch(playset.entries, readiness);
    CHECK_FALSE(launch.canLaunch);
    REQUIRE(launch.blockers.size() == 2); // enabled: failed Workshop mod, unmapped local mod
    CHECK(launch.blockers[0].index == 1);
    CHECK(launch.blockers[1].index == 3);
    CHECK(launch.unresolvedDisabledCount == 2); // unavailable + broken, both disabled

    // Once the enabled entries are resolved, the disabled unresolved ones do not block.
    InstalledModMap resolved = s.inventory;
    add(resolved, installed(s.requested[1].identity, "Missing enabled"));
    add(resolved, installed(s.requested[3].identity, "Local only"));
    readiness = computePlaysetReadiness(playset.entries, resolved, jobs);
    launch = evaluateLaunch(playset.entries, readiness);
    CHECK(launch.canLaunch);
    CHECK(launch.unresolvedDisabledCount == 2);
}
