// Model invariants (QAbstractItemModelTester) and incremental updates of the
// list models: inserts, removals, reorders and data changes must not reset
// the models, and the proxy must filter and sort without touching load order.

#include "app/core/qt_util.hpp"
#include "app/gui/games_model.hpp"
#include "app/gui/inventory_model.hpp"
#include "app/gui/jobs_model.hpp"
#include "app/gui/mod_filter_model.hpp"
#include "app/gui/mod_list_model.hpp"
#include "app/gui/playsets_model.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModelTester>
#include <QSignalSpy>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::app;

namespace {

// Collects QAbstractItemModelTester complaints (category qt.modeltest).
class ModelTestLog {
public:
    ModelTestLog() {
        failures().clear();
        m_previous = qInstallMessageHandler(&ModelTestLog::handler);
    }
    ~ModelTestLog() { qInstallMessageHandler(m_previous); }
    static QStringList& failures() {
        static QStringList list;
        return list;
    }

private:
    static void handler(QtMsgType type, const QMessageLogContext& context, const QString& text) {
        if (type != QtDebugMsg && context.category &&
            QByteArray(context.category).startsWith("qt.modeltest"))
            failures().push_back(text);
    }
    QtMessageHandler m_previous = nullptr;
};

ModRowData modRow(const std::string& id, bool enabled,
                  const QString& status = QStringLiteral("ready"), const QString& name = {}) {
    ModRowData row;
    row.entry.identity = ModIdentity{"stellaris", source::SteamWorkshop, id};
    row.entry.enabled = enabled;
    row.key = identityKey(source::SteamWorkshop, id);
    row.name = name.isEmpty() ? QStringLiteral("Mod %1").arg(qs(id)) : name;
    row.source = qs(source::SteamWorkshop);
    row.sourceLabel = QStringLiteral("Workshop");
    row.providerId = qs(id);
    row.status = status;
    row.statusText = status == QLatin1String("ready") ? QStringLiteral("Ready") : status;
    row.blocking = enabled && status != QLatin1String("ready");
    return row;
}

PlaysetStateData playset(std::int64_t id, std::vector<ModRowData> rows) {
    PlaysetStateData state;
    state.playsetId = id;
    state.game = QStringLiteral("stellaris");
    state.name = QStringLiteral("Test");
    state.rows = std::move(rows);
    return state;
}

std::vector<ModRowData> rows(int count) {
    std::vector<ModRowData> out;
    for (int i = 0; i < count; ++i)
        out.push_back(modRow(std::to_string(100000 + i), i % 3 != 0,
                             i % 7 == 0 ? QStringLiteral("missing") : QStringLiteral("ready")));
    return out;
}

QStringList keys(const ModListModel& model) {
    QStringList out;
    for (const ModRowData& row : model.rows())
        out.push_back(row.key);
    return out;
}

QStringList keys(const std::vector<ModRowData>& list) {
    QStringList out;
    for (const ModRowData& row : list)
        out.push_back(row.key);
    return out;
}

} // namespace

TEST_CASE("ModListModel applies states incrementally", "[app][models]") {
    ModelTestLog log;
    ModListModel model(nullptr);
    ModFilterModel view(&model);
    QAbstractItemModelTester sourceTester(&model,
                                          QAbstractItemModelTester::FailureReportingMode::Warning);
    QAbstractItemModelTester viewTester(&view,
                                        QAbstractItemModelTester::FailureReportingMode::Warning);
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    model.resetTo(playset(1, rows(40)));
    REQUIRE(model.rowCount() == 40);
    CHECK(resets.count() == 1);
    CHECK(model.enabledCount() == 26);
    CHECK(model.missingCount() == 6);
    CHECK(model.data(model.index(0), ModListModel::PositionRole).toInt() == 1);

    SECTION("data change") {
        auto target = rows(40);
        target[5].entry.enabled = !target[5].entry.enabled;
        target[5].statusText = QStringLiteral("Changed");
        model.applyState(playset(1, target));
        CHECK(changed.count() == 1);
        CHECK(model.data(model.index(5), ModListModel::StatusTextRole).toString() == "Changed");
    }
    SECTION("removal and insertion") {
        auto target = rows(40);
        target.erase(target.begin() + 10, target.begin() + 13);
        target.insert(target.begin() + 2, modRow("999", true));
        target.push_back(modRow("998", false));
        model.applyState(playset(1, target));
        CHECK(removed.count() == 1);
        CHECK(inserted.count() == 2);
        CHECK(keys(model) == keys(target));
        REQUIRE(model.rowCount() == 39);
        CHECK(model.data(model.index(38), ModListModel::PositionRole).toInt() == 39);
    }
    SECTION("reorder") {
        auto target = rows(40);
        std::rotate(target.begin() + 3, target.begin() + 30,
                    target.begin() + 33); // block 30..32 -> 3
        model.applyState(playset(1, target));
        CHECK(keys(model) == keys(target));
        CHECK(inserted.count() == 0);
        CHECK(removed.count() == 0);
        CHECK(changed.count() >= 1);
    }
    SECTION("another playset resets") {
        model.applyState(playset(2, rows(5)));
        CHECK(resets.count() == 2);
        CHECK(model.rowCount() == 5);
    }
    SECTION("large structural changes reset") {
        model.applyState(playset(1, rows(400)));
        CHECK(resets.count() == 2);
        CHECK(model.rowCount() == 400);
    }
    CHECK(resets.count() <= 2);
    CHECK(ModelTestLog::failures().isEmpty());
    if (!ModelTestLog::failures().isEmpty())
        FAIL(ModelTestLog::failures().join('\n').toStdString());
}

TEST_CASE("ModFilterModel filters and sorts without changing load order", "[app][models]") {
    ModelTestLog log;
    ModListModel model(nullptr);
    ModFilterModel view(&model);
    QAbstractItemModelTester tester(&view, QAbstractItemModelTester::FailureReportingMode::Warning);
    std::vector<ModRowData> list{
        modRow("1", true, "ready", "Zeta"), modRow("2", false, "ready", "alpha"),
        modRow("3", true, "missing", "Beta"), modRow("4", true, "ready", "gamma")};
    list[3].warnings = QStringList{QStringLiteral("Requires Beta")};
    model.resetTo(playset(1, list));

    CHECK(view.reorderAllowed());
    CHECK(view.reorderBlockedReason().isEmpty());

    view.setSearch(QStringLiteral("ET"));
    CHECK(view.rowCount() == 2); // Zeta, Beta (case-insensitive)
    CHECK_FALSE(view.reorderAllowed());
    CHECK_FALSE(view.reorderBlockedReason().isEmpty());
    view.setSearch(QStringLiteral("3"));
    REQUIRE(view.rowCount() == 1); // provider id
    CHECK(view.sourceRow(0) == 2);
    view.setSearch({});

    view.setStatusFilter(QStringLiteral("disabled"));
    CHECK(view.rowCount() == 1);
    view.setStatusFilter(QStringLiteral("missing"));
    CHECK(view.rowCount() == 1);
    view.setStatusFilter(QStringLiteral("warning"));
    CHECK(view.rowCount() == 1);
    view.setStatusFilter(QStringLiteral("enabled"));
    CHECK(view.rowCount() == 3);
    view.setStatusFilter(QStringLiteral("all"));
    CHECK(view.reorderAllowed());

    view.setSortMode(QStringLiteral("name"));
    CHECK_FALSE(view.reorderAllowed());
    QStringList names;
    for (int r = 0; r < view.rowCount(); ++r)
        names.push_back(view.index(r, 0).data(ModListModel::NameRole).toString());
    CHECK(names == QStringList{"alpha", "Beta", "gamma", "Zeta"});
    CHECK(view.proxyRow(0) == 3); // Zeta is last by name
    view.setSortMode(QStringLiteral("status"));
    CHECK(view.index(0, 0).data(ModListModel::StatusRole).toString() == "missing");
    view.setSortMode(QStringLiteral("order"));
    CHECK(view.index(0, 0).data(ModListModel::NameRole).toString() == "Zeta");
    CHECK(view.reorderAllowed());
    // The source keeps the load order throughout.
    CHECK(keys(model) == QStringList{"steam-workshop:1", "steam-workshop:2", "steam-workshop:3",
                                     "steam-workshop:4"});

    // Updates while filtered keep the proxy consistent.
    view.setStatusFilter(QStringLiteral("enabled"));
    auto updated = list;
    updated[1].entry.enabled = true;
    model.applyState(playset(1, updated));
    CHECK(view.rowCount() == 4);
    if (!ModelTestLog::failures().isEmpty())
        FAIL(ModelTestLog::failures().join('\n').toStdString());
}

TEST_CASE("GamesModel, PlaysetsModel and JobsModel update in place", "[app][models]") {
    ModelTestLog log;

    GamesModel games(nullptr);
    QAbstractItemModelTester gamesTester(&games,
                                         QAbstractItemModelTester::FailureReportingMode::Warning);
    std::vector<GameRowData> gameRows(2);
    gameRows[0].key = "stellaris";
    gameRows[0].name = "Stellaris";
    gameRows[0].supported = true;
    gameRows[1].key = "hoi4";
    gameRows[1].name = "Hearts of Iron IV";
    games.setRows(gameRows);
    games.setCurrent("stellaris");
    CHECK(games.currentName() == "Stellaris");
    CHECK_FALSE(games.currentInstalled());
    QSignalSpy gameResets(&games, &QAbstractItemModel::modelReset);
    gameRows[0].installed = true;
    gameRows[0].manualLocation = true;
    games.setRows(gameRows);
    CHECK(gameResets.count() == 0);
    CHECK(games.currentInstalled());
    CHECK(games.data(games.index(0), GamesModel::ManualLocationRole).toBool());
    CHECK(games.currentLoadOrderNote().isEmpty());
    QSignalSpy currentChanges(&games, &GamesModel::currentChanged);
    gameRows[0].loadOrderNote = "The order is not applied as written.";
    games.setRows(gameRows);
    CHECK(gameResets.count() == 0);
    CHECK(currentChanges.count() >= 1);
    CHECK(games.currentLoadOrderNote() == "The order is not applied as written.");
    CHECK(games.data(games.index(0), GamesModel::LoadOrderNoteRole).toString() ==
          gameRows[0].loadOrderNote);

    PlaysetsModel playsets(nullptr);
    QAbstractItemModelTester playsetsTester(
        &playsets, QAbstractItemModelTester::FailureReportingMode::Warning);
    PlaysetListData list;
    list.playsets = {{1, "A", 3, 2, "Today"}, {2, "B", 0, 0, "Today"}};
    list.activeId = 2;
    list.lastDeletedHistoryId = 7;
    playsets.setList(list);
    CHECK(playsets.activeName() == "B");
    CHECK_FALSE(playsets.canRestore()); // not deleted in this session
    playsets.rememberDeleted(7);
    playsets.setList(list);
    CHECK(playsets.canRestore());
    playsets.updateCounts(1, 4, 4, {});
    CHECK(playsets.data(playsets.index(0), PlaysetsModel::EntryCountRole).toInt() == 4);
    list.playsets.pop_back();
    list.activeId = 1;
    playsets.setList(list);
    CHECK(playsets.count() == 1);
    CHECK(playsets.activeId() == 1);

    JobsModel jobs;
    QAbstractItemModelTester jobsTester(&jobs,
                                        QAbstractItemModelTester::FailureReportingMode::Warning);
    std::vector<JobRowData> jobRows(3);
    for (int i = 0; i < 3; ++i) {
        jobRows[static_cast<std::size_t>(i)].jobId = i + 1;
        jobRows[static_cast<std::size_t>(i)].state = "pending";
    }
    jobs.setRows(jobRows);
    QSignalSpy jobResets(&jobs, &QAbstractItemModel::modelReset);
    std::vector<JobRowData> next{jobRows[2], jobRows[0]};
    next[0].state = "downloading";
    next[0].progress = 0.5;
    JobRowData added;
    added.jobId = 9;
    next.push_back(added);
    jobs.setRows(next);
    CHECK(jobResets.count() == 0);
    REQUIRE(jobs.count() == 3);
    CHECK(jobs.data(jobs.index(0), JobsModel::JobIdRole).toInt() == 3);
    CHECK(jobs.data(jobs.index(0), JobsModel::ProgressRole).toDouble() == 0.5);
    CHECK(jobs.data(jobs.index(2), JobsModel::JobIdRole).toInt() == 9);

    InventoryModel inventory(nullptr);
    QAbstractItemModelTester inventoryTester(
        &inventory, QAbstractItemModelTester::FailureReportingMode::Warning);
    std::vector<InventoryRowData> installed(3);
    installed[0] = {"steam-workshop:1", "Alpha", "steam-workshop", "Workshop", "1", "1.0",
                    "Installed",        ""};
    installed[1] = {"steam-workshop:2", "Beta", "steam-workshop", "Workshop", "2", "1.0",
                    "Installed",        ""};
    installed[2] = {"local:x", "Gamma", "local", "Local", "x", "", "Broken", "no descriptor"};
    inventory.setRows(installed);
    inventory.setPlaysetKeys({"steam-workshop:2"});
    CHECK(inventory.data(inventory.index(1), InventoryModel::InPlaysetRole).toBool());
    inventory.setFilter("gam");
    CHECK(inventory.count() == 1);

    if (!ModelTestLog::failures().isEmpty())
        FAIL(ModelTestLog::failures().join('\n').toStdString());
}
