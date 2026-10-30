#include "persistence/acquisition_job_repository.hpp"
#include "persistence/playset_repository.hpp"
#include "persistence/settings_repository.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace pmm;
using namespace pmm::persistence;
using namespace pmm::persistence::test;

TEST_CASE("One database shared by two writer threads stays consistent",
          "[persistence][concurrency]") {
    TempDir temp;
    auto db = require(Database::open(temp.dbFile()));
    std::atomic<int> failures{0};

    std::thread settingsWriter([&] {
        SettingsRepository settings(*db);
        for (int i = 0; i < 200; ++i) {
            if (!settings.setInt("counter." + std::to_string(i), i).ok())
                ++failures;
        }
    });
    std::thread playsetWriter([&] {
        PlaysetRepository playsets(*db);
        AcquisitionJobRepository jobs(*db);
        for (int i = 0; i < 40; ++i) {
            Playset p;
            p.game = "stellaris";
            p.name = "Threaded " + std::to_string(i);
            p.entries = workshopEntries(10, "stellaris", 1000 * i);
            auto id = playsets.create(p);
            if (!id.ok() || !playsets.saveEntries(*id, workshopEntries(5, "stellaris", i)).ok() ||
                !jobs.createOrReuse({"stellaris", source::SteamWorkshop, std::to_string(i)}, *id)
                     .ok())
                ++failures;
        }
    });
    settingsWriter.join();
    playsetWriter.join();

    CHECK(failures.load() == 0);
    CHECK(queryInt(*db, "SELECT count(*) FROM settings") == 200);
    CHECK(queryInt(*db, "SELECT count(*) FROM playsets") == 40);
    CHECK(queryInt(*db, "SELECT count(*) FROM playset_entries") == std::int64_t{40} * 5);
    CHECK(queryInt(*db, "SELECT count(*) FROM acquisition_jobs") == 40);
    CHECK(queryText(*db, "PRAGMA integrity_check") == "ok");
}

TEST_CASE("Two connections to one file serialise their writes", "[persistence][concurrency]") {
    // Models two application instances: separate Database objects, same file.
    TempDir temp;
    auto first = require(Database::open(temp.dbFile()));
    auto second = require(Database::open(temp.dbFile()));
    std::atomic<int> failures{0};

    auto writer = [&failures](Database& db, const std::string& prefix) {
        SettingsRepository settings(db);
        for (int i = 0; i < 100; ++i) {
            auto status = db.transaction([&]() -> Status {
                PMM_TRY(settings.set(prefix + std::to_string(i), "a"));
                return settings.set(prefix + std::to_string(i), "b");
            });
            if (!status.ok())
                ++failures;
        }
    };
    std::thread a(writer, std::ref(*first), "first.");
    std::thread b(writer, std::ref(*second), "second.");
    a.join();
    b.join();

    CHECK(failures.load() == 0);
    CHECK(queryInt(*first, "SELECT count(*) FROM settings WHERE value = 'b'") == 200);
}
