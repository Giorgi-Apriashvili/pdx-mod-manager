#include "persistence/acquisition_job_repository.hpp"
#include "persistence/installation_repository.hpp"
#include "persistence/inventory_repository.hpp"
#include "persistence/launch_history_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/settings_repository.hpp"
#include "persistence/testing.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::persistence;
using namespace pmm::persistence::test;

namespace {
std::vector<Migration> migrationsUpTo(int version) {
    std::vector<Migration> out;
    for (const auto& m : builtinMigrations()) {
        if (m.version <= version)
            out.push_back(m);
    }
    return out;
}
} // namespace

TEST_CASE("A v1 database from the sample fixture opens with the current schema",
          "[persistence][fixtures]") {
    TempDir temp;
    const fs::path file = temp.dbFile();
    {
        auto db = require(testing::openWithMigrations(file, migrationsUpTo(1)));
        REQUIRE(db->schemaVersion() == 1);
        execRaw(*db, readFixture("v1_sample.sql"));
    }

    auto db = require(Database::open(file));
    CHECK(db->schemaVersion() == Database::latestSchemaVersion());
    if (Database::latestSchemaVersion() > 1) {
        fs::path backup = file;
        backup += ".bak-v1";
        CHECK(db->upgradeBackupPath() == backup);
        CHECK(fs::exists(backup));
    } else {
        CHECK(db->upgradeBackupPath().empty());
    }

    SettingsRepository settings(*db);
    CHECK(require(settings.get("ui.theme")) == std::optional<std::string>("dark"));
    CHECK(require(settings.getInt("ui.window.width")) == std::optional<std::int64_t>(1440));
    CHECK(require(settings.getBool("ui.window.maximized")) == std::optional<bool>(false));

    auto installations = require(InstallationRepository(*db).list());
    REQUIRE(installations.size() == 2);
    CHECK(installations[0].game == "eu4");
    CHECK(installations[0].distribution == DistributionSource::Manual);
    CHECK(installations[0].manuallyConfigured);
    CHECK(installations[1].game == "stellaris");
    CHECK(installations[1].steamAppId == "281990");
    CHECK(installations[1].defaultArguments == std::vector<std::string>{"-gdpr-compliant"});
    CHECK(installations[1].userDataPath ==
          fs::path(u8"C:/Users/Example/Documents/Paradox Interactive/Stellaris"));
    CHECK(installations[1].versionLabel == "Cygnus v4.5.2 (9776)");

    auto mods = require(InventoryRepository(*db).list("stellaris"));
    REQUIRE(mods.size() == 2);
    CHECK(mods[0].identity == ModIdentity{"stellaris", source::Local, "example_local_mod"});
    CHECK(mods[0].descriptor.name == "Ünïcødé local mod 日本語");
    CHECK(mods[0].state == InstallState::Broken);
    CHECK(mods[0].descriptor.extra ==
          std::vector<std::pair<std::string, std::string>>{{"replace_path", "common/ethics"}});
    CHECK(mods[1].descriptor.tags == std::vector<std::string>{"Fixes", "Graphics", "Overhaul"});
    CHECK(mods[1].contentUpdatedAt == 1761000000);

    PlaysetRepository playsets(*db);
    auto summaries = require(playsets.listForGame("stellaris"));
    REQUIRE(summaries.size() == 1);
    CHECK(summaries[0].name == "Example playset");
    CHECK(summaries[0].entryCount == 4);
    CHECK(summaries[0].enabledCount == 3);

    auto playset = require(playsets.get(1));
    REQUIRE(playset.entries.size() == 4);
    const std::vector<std::string> expectedIds{"1234567891", "18446744073709551615",
                                               "example_local_mod", "1234567890"};
    for (std::size_t i = 0; i < expectedIds.size(); ++i)
        CHECK(playset.entries[i].identity.id == expectedIds[i]);
    CHECK_FALSE(playset.entries[1].enabled);
    CHECK(playset.entries[1].versionHint == "2.0");
    CHECK(playset.entries[2].identity.source == source::Local);

    auto history = require(playsets.historyForGame("stellaris"));
    REQUIRE(history.size() == 1);
    CHECK(history[0].reason == history_reason::Deleted);
    CHECK(require(playsets.restore(history[0].id)) == 2);
    auto restored = require(playsets.get(2));
    CHECK(restored.name == "Deleted playset");
    REQUIRE(restored.entries.size() == 1);
    CHECK(restored.entries[0].identity.id == "1234567890");

    AcquisitionJobRepository jobs(*db);
    auto unfinished = require(jobs.listUnfinished());
    REQUIRE(unfinished.size() == 1);
    CHECK(unfinished[0].state == AcquisitionState::DownloadRequested);
    CHECK(unfinished[0].playsetId == std::optional<std::int64_t>(1));
    auto failed = require(jobs.get(2));
    CHECK(failed.state == AcquisitionState::Failed);
    CHECK(failed.identity.id == "18446744073709551615");
    CHECK(failed.bytesDownloaded == std::uint64_t{1048576});
    CHECK(failed.bytesTotal == std::uint64_t{4194304});

    auto launches = require(LaunchHistoryRepository(*db).latest("stellaris", 5));
    REQUIRE(launches.size() == 1);
    CHECK(launches[0].pid == std::optional<std::int64_t>(12345));

    // Ids keep increasing after the imported rows.
    Playset fresh;
    fresh.game = "stellaris";
    fresh.name = "After upgrade";
    CHECK(require(playsets.create(fresh)) == 3);
}

TEST_CASE("Upgrading to v2 drops the installation platform and keeps the rows",
          "[persistence][fixtures]") {
    TempDir temp;
    const fs::path file = temp.dbFile();
    const std::string platformColumns = "SELECT count(*) FROM pragma_table_info("
                                        "'game_installations') WHERE name = 'platform'";
    {
        auto db = require(testing::openWithMigrations(file, migrationsUpTo(1)));
        execRaw(*db, readFixture("v1_sample.sql"));
        // The migration does not depend on the stored value.
        execRaw(*db, "UPDATE game_installations SET platform = 'linux' WHERE game_key = 'eu4'");
        REQUIRE(queryInt(*db, platformColumns) == 1);
    }

    {
        auto db = require(testing::openWithMigrations(file, migrationsUpTo(2)));
        CHECK(db->schemaVersion() == 2);
        CHECK(queryInt(*db, "SELECT count(*) FROM schema_migrations") == 2);
        CHECK(queryInt(*db, "PRAGMA user_version") == 2);
        CHECK(queryInt(*db, platformColumns) == 0);
        CHECK(queryInt(*db, "SELECT count(*) FROM pragma_table_info('game_installations')") == 12);
        CHECK(queryText(*db, "SELECT updated_at FROM game_installations WHERE game_key = 'eu4'") ==
              "1761700000000");

        InstallationRepository repository(*db);
        auto installations = require(repository.list());
        REQUIRE(installations.size() == 2);
        CHECK(installations[0].game == "eu4");
        CHECK(installations[0].distribution == DistributionSource::Manual);
        CHECK(installations[0].manuallyConfigured);
        CHECK(installations[0].executablePath ==
              fs::path(u8"D:/Games/Europa Universalis IV/eu4.exe"));
        CHECK(installations[1].game == "stellaris");
        CHECK(installations[1].distribution == DistributionSource::Steam);
        CHECK(installations[1].steamAppId == "281990");
        CHECK(installations[1].steamLibraryPath == fs::path(u8"C:/Program Files (x86)/Steam"));
        CHECK(installations[1].defaultArguments == std::vector<std::string>{"-gdpr-compliant"});
        CHECK(installations[1].version == "v4.5.2");
        CHECK(installations[1].versionLabel == "Cygnus v4.5.2 (9776)");
        CHECK_FALSE(installations[1].manuallyConfigured);

        installations[1].version = "v4.6.0";
        requireOk(repository.upsert(installations[1]));
        CHECK(require(repository.get("stellaris"))->version == "v4.6.0");
    }

    // The backup is the old schema, with the column, and opens at that version.
    fs::path backup = file;
    backup += ".bak-v1";
    REQUIRE(fs::exists(backup));
    auto old = require(testing::openWithMigrations(backup, migrationsUpTo(1)));
    CHECK(old->schemaVersion() == 1);
    CHECK(queryInt(*old, platformColumns) == 1);
    CHECK(queryText(*old, "SELECT platform FROM game_installations WHERE game_key = 'stellaris'") ==
          "windows");
}

TEST_CASE("A v2 database from the sample fixture opens with the current schema",
          "[persistence][fixtures]") {
    TempDir temp;
    const fs::path file = temp.dbFile();
    {
        auto db = require(testing::openWithMigrations(file, migrationsUpTo(2)));
        REQUIRE(db->schemaVersion() == 2);
        execRaw(*db, readFixture("v2_sample.sql"));
    }

    auto db = require(Database::open(file));
    CHECK(db->schemaVersion() == Database::latestSchemaVersion());

    CHECK(require(SettingsRepository(*db).get("ui.theme")) == std::optional<std::string>("dark"));
    auto installations = require(InstallationRepository(*db).list());
    REQUIRE(installations.size() == 1);
    CHECK(installations[0].game == "stellaris");
    CHECK(installations[0].distribution == DistributionSource::Steam);
    CHECK(installations[0].steamAppId == "281990");
    CHECK(installations[0].versionLabel == "Cygnus v4.5.2 (9776)");

    PlaysetRepository playsets(*db);
    auto playset = require(playsets.get(1));
    CHECK(playset.name == "Example playset");
    REQUIRE(playset.entries.size() == 1);
    CHECK(playset.entries[0].identity.id == "1234567890");
}
