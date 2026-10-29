// Responsiveness with 2,000 playset entries (spec section 3): loading,
// filtering, sorting, reordering and bulk edits must stay well within an
// interactive budget on the GUI thread, with persistence in the background.

#include "app/core/qt_util.hpp"
#include "app/core/service_runner.hpp"
#include "app/gui/backend.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>

#include <algorithm>
#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::app;
using namespace pmm::app::test;

namespace {

constexpr int kEntries = 2000;
// Per GUI-thread operation; generous for unoptimised debug builds on CI.
constexpr qint64 kGuiBudgetMs = 400;

std::vector<ModRowData> syntheticRows() {
    std::vector<ModRowData> rows;
    rows.reserve(kEntries);
    for (int i = 0; i < kEntries; ++i) {
        ModRowData row;
        const std::string id = std::to_string(3000000000LL + i);
        row.entry.identity = ModIdentity{"stellaris", source::SteamWorkshop, id};
        row.entry.enabled = i % 4 != 0;
        row.key = identityKey(source::SteamWorkshop, id);
        row.name =
            QStringLiteral("Synthetic mod %1 %2").arg(i).arg(i % 13 == 0 ? "Ships" : "Events");
        row.source = qs(source::SteamWorkshop);
        row.sourceLabel = QStringLiteral("Workshop");
        row.providerId = qs(id);
        row.status = i % 50 == 0 ? QStringLiteral("missing") : QStringLiteral("ready");
        row.statusText = row.status;
        if (i % 17 == 0)
            row.warnings = QStringList{QStringLiteral("Version mismatch")};
        rows.push_back(std::move(row));
    }
    return rows;
}

template <class F> qint64 timed(F&& work) {
    QElapsedTimer timer;
    timer.start();
    work();
    return timer.elapsed();
}

} // namespace

TEST_CASE("2,000 entries load, filter, sort and reorder within budget", "[app][performance]") {
    ModListModel model(nullptr);
    ModFilterModel view(&model);
    PlaysetStateData state;
    state.playsetId = 1;
    state.game = QStringLiteral("stellaris");
    state.rows = syntheticRows();

    CHECK(timed([&] { model.resetTo(state); }) < kGuiBudgetMs);
    REQUIRE(view.rowCount() == kEntries);

    CHECK(timed([&] { view.setSearch(QStringLiteral("ships")); }) < kGuiBudgetMs);
    CHECK(view.rowCount() == 154);
    CHECK(timed([&] { view.setSearch({}); }) < kGuiBudgetMs);
    CHECK(timed([&] { view.setStatusFilter(QStringLiteral("missing")); }) < kGuiBudgetMs);
    CHECK(view.rowCount() == 40);
    CHECK(timed([&] { view.setStatusFilter(QStringLiteral("all")); }) < kGuiBudgetMs);
    CHECK(timed([&] { view.setSortMode(QStringLiteral("name")); }) < kGuiBudgetMs);
    CHECK(timed([&] { view.setSortMode(QStringLiteral("order")); }) < kGuiBudgetMs);

    // An authoritative state with a block moved from the end to the top and
    // a few data changes is applied incrementally.
    PlaysetStateData moved = state;
    std::rotate(moved.rows.begin(), moved.rows.begin() + 1500, moved.rows.begin() + 1510);
    moved.rows[700].entry.enabled = !moved.rows[700].entry.enabled;
    moved.rows[701].statusText = QStringLiteral("Downloading 45 %");
    CHECK(timed([&] { model.applyState(moved); }) < kGuiBudgetMs);
    CHECK(model.rows()[0].key == state.rows[1500].key);
    CHECK(model.rowCount() == kEntries);
}

TEST_CASE("2,000-entry playset through the full stack stays responsive", "[app][performance]") {
    TempDir temp;
    FakeWorld world(temp.path() / "world");
    QStringList keys;
    for (int i = 0; i < kEntries; ++i) {
        const std::string id = std::to_string(3100000000LL + i);
        fs::create_directories(world.workshop / id);
        keys.push_back(identityKey(source::SteamWorkshop, id));
    }
    FakeAdapter adapter(world);
    RunnerOptions options;
    options.runtime = hermeticOptions(temp.path() / "profile", adapter);
    options.steam = false;
    ServiceRunner runner(options);
    const RunnerStart start = runner.start();
    REQUIRE(start.ok);
    Backend backend(runner, {});
    backend.initialize(start);
    REQUIRE(waitUntil([&] { return backend.initialRefreshDone(); }, std::chrono::seconds(60)));
    REQUIRE(backend.inventory()->totalCount() == kEntries);

    backend.playsets()->create(QStringLiteral("Huge"));
    REQUIRE(waitUntil([&] { return backend.activePlaysetId() >= 0; }));
    backend.mods()->addMods(keys);
    REQUIRE(
        waitUntil([&] { return backend.mods()->count() == kEntries && !backend.mods()->busy(); },
                  std::chrono::seconds(60)));

    // Edits apply on the GUI thread at once; persistence happens on the worker.
    CHECK(timed([&] { backend.mods()->moveRows({1990, 1991, 1992}, -1); }) < kGuiBudgetMs);
    CHECK(timed([&] { backend.mods()->moveRows({1800, 1801}, -1700); }) < kGuiBudgetMs);
    CHECK(backend.mods()->rows()[100].key == keys[1800]);
    QList<int> all;
    for (int i = 0; i < kEntries; ++i)
        all.push_back(i);
    CHECK(timed([&] { backend.mods()->setEnabledRows(all, false); }) < kGuiBudgetMs);
    CHECK(backend.mods()->enabledCount() == 0);
    QElapsedTimer persisted;
    persisted.start();
    REQUIRE(waitUntil([&] { return !backend.mods()->busy(); }, std::chrono::seconds(60)));
    INFO("background persistence of three edits took " << persisted.elapsed() << " ms");
    CHECK(persisted.elapsed() < 30000);
    CHECK(backend.mods()->rows()[100].key == keys[1800]);
    CHECK(backend.mods()->enabledCount() == 0);

    CHECK(timed([&] { backend.modView()->setSearch(QStringLiteral("3100001999")); }) <
          kGuiBudgetMs);
    CHECK(backend.modView()->rowCount() == 1);

    backend.shutdown();
    runner.shutdown();
}
