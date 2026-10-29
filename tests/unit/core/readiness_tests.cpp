#include "core/readiness.hpp"
#include "playset_test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <vector>

using namespace pmm;
using Catch::Matchers::ContainsSubstring;
using test::add;
using test::installed;
using test::local;
using test::workshop;

namespace {

AcquisitionJob job(const PlaysetEntry& entry, AcquisitionState state) {
    AcquisitionJob j;
    j.identity = entry.identity;
    j.state = state;
    return j;
}

} // namespace

TEST_CASE("Installed usable content is ready, with mismatch warnings", "[core][readiness]") {
    PlaysetEntry entry = workshop("1", "Mod");
    InstalledMod mod = installed(entry.identity, "Mod");

    EntryReadiness r = computeReadiness(entry, &mod, nullptr);
    CHECK(r.status == EntryStatus::Ready);
    CHECK(r.warnings.empty());

    entry.versionHint = "1.0";
    mod.descriptor.version = "1.1";
    entry.fingerprint = "sha256:" + std::string(64, 'a');
    mod.fingerprint = "sha256:" + std::string(64, 'b');
    mod.problems = {"Descriptor has no supported_version."};
    r = computeReadiness(entry, &mod, nullptr);
    CHECK(r.status == EntryStatus::Ready);
    REQUIRE(r.warnings.size() == 3);
    CHECK_THAT(r.warnings[0], ContainsSubstring("version 1.0"));
    CHECK_THAT(r.warnings[1], ContainsSubstring("fingerprint"));

    mod.state = InstallState::NeedsUpdate;
    r = computeReadiness(entry, &mod, nullptr);
    CHECK(r.status == EntryStatus::Ready);
    CHECK_THAT(r.warnings[0], ContainsSubstring("update is pending"));

    // Installed content wins over any job state.
    const AcquisitionJob failed = job(entry, AcquisitionState::Failed);
    CHECK(computeReadiness(entry, &mod, &failed).status == EntryStatus::Ready);
}

TEST_CASE("Broken or updating installed content is not ready", "[core][readiness]") {
    const PlaysetEntry entry = workshop("1", "Mod");
    InstalledMod mod = installed(entry.identity, "Mod", {}, InstallState::Broken);
    mod.problems = {"Content folder is missing."};
    EntryReadiness r = computeReadiness(entry, &mod, nullptr);
    CHECK(r.status == EntryStatus::Broken);
    CHECK(r.detail == "Content folder is missing.");

    mod.state = InstallState::Downloading;
    AcquisitionJob j = job(entry, AcquisitionState::Downloading);
    j.bytesDownloaded = 50;
    j.bytesTotal = 200;
    r = computeReadiness(entry, &mod, &j);
    CHECK(r.status == EntryStatus::Downloading);
    CHECK(r.progress == 0.25);
}

TEST_CASE("Acquisition job states map to entry states", "[core][readiness]") {
    const PlaysetEntry entry = workshop("1", "Mod");
    struct Case {
        AcquisitionState state;
        EntryStatus expected;
    };
    for (const Case c : {Case{AcquisitionState::Pending, EntryStatus::Pending},
                         Case{AcquisitionState::Subscribing, EntryStatus::Pending},
                         Case{AcquisitionState::Subscribed, EntryStatus::Pending},
                         Case{AcquisitionState::DownloadRequested, EntryStatus::Pending},
                         Case{AcquisitionState::Downloading, EntryStatus::Downloading},
                         Case{AcquisitionState::Installed, EntryStatus::Pending},
                         Case{AcquisitionState::Failed, EntryStatus::Failed},
                         Case{AcquisitionState::Unavailable, EntryStatus::Unavailable},
                         Case{AcquisitionState::NeedsUserAction, EntryStatus::NeedsUserAction},
                         Case{AcquisitionState::Cancelled, EntryStatus::Missing}}) {
        INFO(toString(c.state));
        const AcquisitionJob j = job(entry, c.state);
        const EntryReadiness r = computeReadiness(entry, nullptr, &j);
        CHECK(r.status == c.expected);
        CHECK_FALSE(r.detail.empty());
    }
}

TEST_CASE("A finished job is not trusted until the inventory has the content",
          "[core][readiness]") {
    const PlaysetEntry entry = workshop("1", "Mod");
    const AcquisitionJob j = job(entry, AcquisitionState::Installed);
    const EntryReadiness r = computeReadiness(entry, nullptr, &j);
    CHECK(r.status == EntryStatus::Pending);
    CHECK_THAT(r.detail, ContainsSubstring("verifying"));
}

TEST_CASE("Download progress is a fraction or indeterminate", "[core][readiness]") {
    const PlaysetEntry entry = workshop("1", "Mod");
    AcquisitionJob j = job(entry, AcquisitionState::Downloading);
    CHECK(computeReadiness(entry, nullptr, &j).progress < 0.0);
    j.bytesTotal = 1000;
    j.bytesDownloaded = 750;
    CHECK(computeReadiness(entry, nullptr, &j).progress == 0.75);
    j.bytesDownloaded = 5000;
    CHECK(computeReadiness(entry, nullptr, &j).progress == 1.0);
}

TEST_CASE("Failure details come from the job when available", "[core][readiness]") {
    const PlaysetEntry entry = workshop("1", "Mod");
    AcquisitionJob j = job(entry, AcquisitionState::Failed);
    j.errorMessage = "Steam is offline.";
    CHECK(computeReadiness(entry, nullptr, &j).detail == "Steam is offline.");
}

TEST_CASE("Without content or job: Workshop is missing, local needs the user",
          "[core][readiness]") {
    const PlaysetEntry w = workshop("1", "Workshop mod");
    const PlaysetEntry l = local("my-mod", "Local mod");
    CHECK(computeReadiness(w, nullptr, nullptr).status == EntryStatus::Missing);
    const EntryReadiness r = computeReadiness(l, nullptr, nullptr);
    CHECK(r.status == EntryStatus::NeedsUserAction);
    CHECK_THAT(r.detail, ContainsSubstring("cannot be downloaded"));

    // A job for a different mod is ignored.
    const AcquisitionJob other = job(workshop("2", "Other"), AcquisitionState::Downloading);
    CHECK(computeReadiness(w, nullptr, &other).status == EntryStatus::Missing);
}

TEST_CASE("Playset readiness uses the latest job and adds dependency warnings",
          "[core][readiness]") {
    const PlaysetEntry addon = workshop("20", "Addon");
    const PlaysetEntry base = workshop("10", "Base");
    const PlaysetEntry pending = workshop("30", "Pending mod", false);
    InstalledModMap inventory;
    add(inventory, installed(base.identity, "Base"));
    add(inventory, installed(addon.identity, "Addon", {"Base"}));
    add(inventory, installed({"stellaris", "steam-workshop", "99"}, "Extra installed mod"));

    AcquisitionJob older = job(pending, AcquisitionState::Failed);
    older.id = 1;
    older.updatedAt = 100;
    AcquisitionJob newer = job(pending, AcquisitionState::Downloading);
    newer.id = 2;
    newer.updatedAt = 200;
    const std::vector<AcquisitionJob> jobs{newer, older};

    const std::vector<PlaysetEntry> entries{addon, base, pending};
    const auto readiness = computePlaysetReadiness(entries, inventory, jobs);
    REQUIRE(readiness.size() == entries.size()); // extra installed mods are not added
    CHECK(readiness[0].status == EntryStatus::Ready);
    REQUIRE(readiness[0].warnings.size() == 1);
    CHECK_THAT(readiness[0].warnings[0], ContainsSubstring("must load before"));
    CHECK(readiness[1].status == EntryStatus::Ready);
    CHECK(readiness[2].status == EntryStatus::Downloading);
}

TEST_CASE("Launch requires every enabled entry to be ready", "[core][readiness]") {
    std::vector<PlaysetEntry> entries{workshop("1", "Ready mod"), workshop("2", "Missing disabled", false),
                                      local("x", "Unavailable disabled", false)};
    std::vector<EntryReadiness> readiness(3);
    readiness[0].status = EntryStatus::Ready;
    readiness[1].status = EntryStatus::Missing;
    readiness[2].status = EntryStatus::Unavailable;

    SECTION("unresolved disabled entries never block") {
        const LaunchReadiness launch = evaluateLaunch(entries, readiness);
        CHECK(launch.canLaunch);
        CHECK(launch.blockers.empty());
        CHECK(launch.enabledCount == 1);
        CHECK(launch.unresolvedDisabledCount == 2);
        CHECK_THAT(launch.summary, ContainsSubstring("do not block"));
    }
    SECTION("a missing enabled entry blocks with its position and name") {
        entries[1].enabled = true;
        readiness[1].detail = "Not installed.";
        const LaunchReadiness launch = evaluateLaunch(entries, readiness);
        CHECK_FALSE(launch.canLaunch);
        REQUIRE(launch.blockers.size() == 1);
        CHECK(launch.blockers[0].index == 1);
        CHECK(launch.blockers[0].name == "Missing disabled");
        CHECK(launch.blockers[0].status == EntryStatus::Missing);
        CHECK(launch.blockers[0].reason == "#2 Missing disabled: missing - Not installed.");
        CHECK_THAT(launch.summary, ContainsSubstring("1 enabled mod is not ready"));
    }
    SECTION("every non-ready state blocks when enabled") {
        for (const EntryStatus status :
             {EntryStatus::Missing, EntryStatus::Pending, EntryStatus::Downloading,
              EntryStatus::Failed, EntryStatus::Unavailable, EntryStatus::NeedsUserAction,
              EntryStatus::Broken}) {
            readiness[0].status = status;
            CHECK_FALSE(evaluateLaunch(entries, readiness).canLaunch);
        }
    }
    SECTION("missing readiness information blocks enabled entries") {
        readiness.resize(1);
        CHECK(evaluateLaunch(entries, readiness).canLaunch);
        entries[2].enabled = true;
        const LaunchReadiness launch = evaluateLaunch(entries, readiness);
        CHECK_FALSE(launch.canLaunch);
        CHECK(launch.blockers[0].name == "Unavailable disabled");
    }
    SECTION("an empty playset launches the unmodded game") {
        const LaunchReadiness launch = evaluateLaunch({}, {});
        CHECK(launch.canLaunch);
        CHECK(launch.summary == "Ready to launch without mods.");
    }
}
