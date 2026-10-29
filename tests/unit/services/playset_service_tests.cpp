#include "persistence/playset_repository.hpp"
#include "persistence/settings_repository.hpp"
#include "services/playset_service.hpp"
#include "services/playset_view.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <vector>

using namespace pmm;
using namespace pmm::services;
using namespace pmm::services::test;
using Catch::Matchers::ContainsSubstring;

namespace {

std::vector<std::string> ids(const Playset& playset) {
    std::vector<std::string> out;
    for (const PlaysetEntry& e : playset.entries)
        out.push_back(e.identity.id);
    return out;
}

std::vector<bool> enabledFlags(const Playset& playset) {
    std::vector<bool> out;
    for (const PlaysetEntry& e : playset.entries)
        out.push_back(e.enabled);
    return out;
}

// Reads the playset straight from the database (what a restart would see).
Playset stored(World& w, std::int64_t id) {
    persistence::PlaysetRepository repo(*w.db);
    return require(repo.get(id));
}

std::int64_t makePlayset(World& w, std::vector<PlaysetEntry> entries,
                         const std::string& name = "Main") {
    persistence::PlaysetRepository repo(*w.db);
    Playset p;
    p.game = kGame;
    p.name = name;
    p.entries = std::move(entries);
    return require(repo.create(p));
}

} // namespace

TEST_CASE("Playsets are created, duplicated, renamed, deleted and restored",
          "[services][playsets]") {
    World w;
    PlaysetService service(w.ctx);
    CHECK_FALSE(require(service.activePlaysetId(kGame)));

    const std::int64_t first = require(service.create(kGame, "  Main  "));
    CHECK(require(service.playset(first)).name == "Main");
    const std::int64_t second = require(service.create(kGame, "main"));
    CHECK(require(service.playset(second)).name == "main (2)");
    const std::int64_t unnamed = require(service.create(kGame, ""));
    CHECK(require(service.playset(unnamed)).name == PlaysetService::kDefaultName);
    CHECK_FALSE(service.create(kGame, std::string(300, 'x')));
    CHECK_FALSE(service.create("no-such-game", "x"));

    // Without a stored choice the first playset (by name) is offered.
    CHECK(require(service.activePlaysetId(kGame)) == first);
    requireOk(service.setActivePlayset(kGame, second));
    CHECK(require(service.activePlaysetId(kGame)) == second);
    CHECK(require(service.lastGameKey()) == std::optional<GameKey>(kGame));
    persistence::SettingsRepository settings(*w.db);
    CHECK(require(settings.getInt("game.stellaris.activePlaysetId")) == second);
    CHECK_FALSE(service.setActivePlayset("eu4", second));

    const std::int64_t copy = require(service.duplicate(first, ""));
    CHECK(require(service.playset(copy)).name == "Main (copy)");

    const Status clash = service.rename(copy, "MAIN");
    REQUIRE_FALSE(clash);
    CHECK(clash.error().code == ErrorCode::AlreadyExists);
    CHECK_FALSE(service.rename(copy, "   "));
    requireOk(service.rename(copy, "Copy of main"));
    CHECK(require(service.playset(copy)).name == "Copy of main");

    // Deleting the active playset activates another one and is undoable.
    const std::int64_t historyId = require(service.remove(second));
    CHECK(require(service.activePlaysetId(kGame)) != std::optional<std::int64_t>(second));
    CHECK(require(service.listPlaysets(kGame)).size() == 3);
    const auto last = require(service.lastDeleted(kGame));
    REQUIRE(last);
    CHECK(last->id == historyId);
    CHECK(last->snapshot.name == "main (2)");

    CHECK(require(service.restoreLastDeleted(kGame)) == second);
    CHECK(require(service.activePlaysetId(kGame)) == second);
    CHECK(require(service.playset(second)).name == "main (2)");
    CHECK_FALSE(require(service.lastDeleted(kGame)));
    CHECK_FALSE(service.restoreLastDeleted(kGame));
    CHECK_FALSE(require(service.history(kGame)).empty());
}

TEST_CASE("Edits persist immediately and can be undone and redone", "[services][playsets]") {
    World w;
    w.installFixtureMods();
    w.refresh();
    PlaysetService service(w.ctx);
    const std::int64_t id = makePlayset(w, {workshopEntry("2000000001", "UI Overhaul Example"),
                                            workshopEntry("2000000002", "Patch", false),
                                            workshopEntry("2000000003", "Old Era", true)});
    const ViewFilter unfiltered;

    CHECK_FALSE(service.undoInfo(id).canUndo);
    const std::size_t disable[] = {0, 2};
    REQUIRE(service.setEnabled(id, disable, false).ok());
    CHECK(enabledFlags(stored(w, id)) == std::vector<bool>{false, false, false});
    CHECK(service.undoInfo(id).undoText == "Disable 2 mods");

    requireOk(service.move(id, 2, 0, unfiltered));
    CHECK(ids(stored(w, id)) == std::vector<std::string>{"2000000003", "2000000001", "2000000002"});
    CHECK(service.undoInfo(id).undoText == "Move \"Old Era\"");

    const std::size_t down[] = {0};
    CHECK(require(service.moveDown(id, down, unfiltered)) == std::vector<std::size_t>{1});
    CHECK(ids(stored(w, id)) == std::vector<std::string>{"2000000001", "2000000003", "2000000002"});

    const std::size_t block[] = {1, 2};
    CHECK(require(service.moveBlock(id, block, 0, unfiltered)) == 0);
    CHECK(ids(stored(w, id)) == std::vector<std::string>{"2000000003", "2000000002", "2000000001"});

    const ModIdentity local{kGame, source::Local, "example_local_tweaks"};
    const ModIdentity addList[] = {local};
    requireOk(service.addMods(id, addList));
    const Playset withLocal = stored(w, id);
    REQUIRE(withLocal.entries.size() == 4);
    CHECK(withLocal.entries[3].identity == local);
    CHECK(withLocal.entries[3].enabled);
    CHECK(withLocal.entries[3].nameHint == "Example Local Tweaks");
    CHECK(service.addMods(id, addList).error().code == ErrorCode::AlreadyExists);
    const ModIdentity notInstalled[] = {ModIdentity{kGame, source::SteamWorkshop, "999"}};
    CHECK(service.addMods(id, notInstalled).error().code == ErrorCode::NotFound);

    // Undo everything step by step, newest first.
    const std::vector<std::vector<std::string>> expected = {
        {"2000000003", "2000000002", "2000000001"}, // before add
        {"2000000001", "2000000003", "2000000002"}, // before block move
        {"2000000003", "2000000001", "2000000002"}, // before move down
        {"2000000001", "2000000002", "2000000003"}, // before move
    };
    for (const auto& order : expected) {
        requireOk(service.undo(id));
        CHECK(ids(stored(w, id)) == order);
    }
    requireOk(service.undo(id));
    CHECK(enabledFlags(stored(w, id)) == std::vector<bool>{true, false, true});
    CHECK_FALSE(service.undoInfo(id).canUndo);
    CHECK(service.undo(id).error().code == ErrorCode::InvalidArgument);

    // Redo replays them in order; a new edit clears the redo stack.
    REQUIRE(service.undoInfo(id).canRedo);
    CHECK(service.undoInfo(id).redoText == "Disable 2 mods");
    requireOk(service.redo(id));
    requireOk(service.redo(id));
    CHECK(ids(stored(w, id)) == std::vector<std::string>{"2000000003", "2000000001", "2000000002"});
    const std::size_t enableFirst[] = {0};
    REQUIRE(service.setEnabled(id, enableFirst, true).ok());
    CHECK_FALSE(service.undoInfo(id).canRedo);

    // A fresh service (restart) sees the persisted state; undo history is per session.
    PlaysetService restarted(w.ctx);
    CHECK(ids(require(restarted.playset(id))) == ids(stored(w, id)));
    CHECK_FALSE(restarted.undoInfo(id).canUndo);
}

TEST_CASE("Reordering a filtered or sorted view is refused with an explanation",
          "[services][playsets]") {
    World w;
    PlaysetService service(w.ctx);
    const std::int64_t id =
        makePlayset(w, {workshopEntry("1", "A"), workshopEntry("2", "B"), workshopEntry("3", "C")});
    ViewFilter searched;
    searched.search = "a";
    ViewFilter sorted;
    sorted.browseSorted = true;
    ViewFilter disabledOnly;
    disabledOnly.status = StatusFilter::Disabled;

    for (const ViewFilter& filter : {searched, sorted, disabledOnly}) {
        const Status refused = service.move(id, 0, 2, filter);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().code == ErrorCode::ValidationFailed);
        CHECK(refused.error().message == reorderPolicy(filter).explanation);
        const std::size_t one[] = {1};
        CHECK_FALSE(service.moveUp(id, one, filter));
        CHECK_FALSE(service.moveDown(id, one, filter));
        CHECK_FALSE(service.moveBlock(id, one, 0, filter));
    }
    CHECK(ids(stored(w, id)) == std::vector<std::string>{"1", "2", "3"});
    CHECK_FALSE(service.undoInfo(id).canUndo);
    // Enabling/disabling stays possible while filtered.
    const std::size_t first[] = {0};
    CHECK(service.setEnabled(id, first, false).ok());
}

TEST_CASE("Bulk enabling and disabling surfaces the dependency issues it introduces",
          "[services][playsets]") {
    World w;
    w.installFixtureMods();
    w.refresh();
    PlaysetService service(w.ctx);
    // Index 0: the patch depends on "UI Overhaul Example" (index 1).
    const std::int64_t id = makePlayset(w, {workshopEntry("2000000002", "Patch", false),
                                            workshopEntry("2000000001", "UI Overhaul", false)});

    const std::size_t patch[] = {0};
    const auto enabling = require(service.setEnabled(id, patch, true));
    REQUIRE(enabling.size() == 1);
    CHECK(enabling[0].kind == DependencyIssueKind::Disabled);
    CHECK(enabling[0].dependencyName == "UI Overhaul Example");

    const std::size_t overhaul[] = {1};
    const auto enablingDependency = require(service.setEnabled(id, overhaul, true));
    // Enabled now, but it loads after its dependent.
    REQUIRE(enablingDependency.size() == 1);
    CHECK(enablingDependency[0].kind == DependencyIssueKind::OrderedAfter);

    requireOk(service.move(id, 1, 0, ViewFilter{}));
    const auto disabling =
        require(service.setEnabled(id, patch, false)); // index 0 is the overhaul now
    REQUIRE(disabling.size() == 1);
    CHECK(disabling[0].kind == DependencyIssueKind::Disabled);
    CHECK(disabling[0].entryIndex == 1);
    // Nothing is ever enabled or reordered automatically.
    CHECK(enabledFlags(stored(w, id)) == std::vector<bool>{false, true});
}

TEST_CASE("Removing entries snapshots the playset first; foreign changes invalidate undo",
          "[services][playsets]") {
    World w;
    PlaysetService service(w.ctx);
    const std::int64_t id = makePlayset(
        w, {workshopEntry("1", "A"), workshopEntry("2", "B", false), workshopEntry("3", "C")});
    const std::size_t remove[] = {2, 0};
    requireOk(service.removeEntries(id, remove));
    CHECK(ids(stored(w, id)) == std::vector<std::string>{"2"});
    CHECK(service.undoInfo(id).undoText == "Remove 2 mods");

    persistence::PlaysetRepository repo(*w.db);
    const auto history = require(repo.historyForPlayset(id));
    REQUIRE(history.size() == 1);
    CHECK(history[0].reason == kHistoryReasonEntriesRemoved);
    CHECK(ids(history[0].snapshot) == std::vector<std::string>{"1", "2", "3"});

    requireOk(service.undo(id));
    CHECK(ids(stored(w, id)) == std::vector<std::string>{"1", "2", "3"});
    CHECK(enabledFlags(stored(w, id)) == std::vector<bool>{true, false, true});

    // Something else (an import replace, another instance) changes the playset:
    // redo must not overwrite it.
    requireOk(repo.saveEntries(id, {workshopEntry("9", "Imported")}));
    const Status redo = service.redo(id);
    REQUIRE_FALSE(redo);
    CHECK_THAT(redo.error().message, ContainsSubstring("changed elsewhere"));
    CHECK(ids(stored(w, id)) == std::vector<std::string>{"9"});
    CHECK_FALSE(service.undoInfo(id).canRedo);
}

TEST_CASE("The undo stack keeps the last 100 steps", "[services][playsets]") {
    World w;
    PlaysetService service(w.ctx);
    const std::int64_t id = makePlayset(w, {workshopEntry("1", "A")});
    const std::size_t first[] = {0};
    for (int i = 0; i < 105; ++i)
        REQUIRE(service.setEnabled(id, first, i % 2 != 0).ok());
    int undone = 0;
    while (service.undoInfo(id).canUndo) {
        requireOk(service.undo(id));
        ++undone;
    }
    CHECK(undone == static_cast<int>(PlaysetService::kUndoLimit));
    // 105 toggles starting from enabled; undoing 100 lands after toggle 5.
    CHECK(enabledFlags(stored(w, id)) == std::vector<bool>{false});
}

TEST_CASE("Playset views resolve names, readiness, warnings and counts", "[services][view]") {
    World w;
    w.installFixtureMods();
    w.refresh();
    const std::int64_t id =
        makePlayset(w, {workshopEntry("2000000003", "hint ignored when installed"),
                        workshopEntry("2000000002", "Patch", true),
                        workshopEntry("2000000777", "Missing One", false),
                        workshopEntry("2000000888", "", true),
                        localEntry("not_here", "Their local mod", false)});

    const PlaysetView view = require(loadPlaysetView(w.ctx, id));
    REQUIRE(view.rows.size() == 5);
    CHECK(view.rows[0].displayName == "Old Era Example");
    CHECK(view.rows[2].displayName == "Missing One");
    CHECK(view.rows[3].displayName == "2000000888");
    CHECK(view.rows[0].readiness.status == EntryStatus::Ready);
    // Adapter compatibility warning (made for v3.14).
    REQUIRE_FALSE(view.rows[0].readiness.warnings.empty());
    CHECK_THAT(view.rows[0].readiness.warnings.back(), ContainsSubstring("v3.14"));
    // The patch's dependency is not in the playset.
    CHECK_FALSE(view.rows[1].readiness.warnings.empty());
    CHECK(view.rows[2].readiness.status == EntryStatus::Missing);
    CHECK_FALSE(view.rows[2].blocking);
    CHECK(view.rows[3].blocking);
    CHECK(view.rows[4].readiness.status == EntryStatus::NeedsUserAction);

    CHECK(view.counts.total == 5);
    CHECK(view.counts.enabled == 3);
    CHECK(view.counts.ready == 2);
    CHECK(view.counts.missing == 3);
    CHECK(view.counts.blocking == 1);
    CHECK(view.counts.warning == 2);
    CHECK_FALSE(view.launch.canLaunch);
    REQUIRE(view.launch.blockers.size() == 1);
    CHECK(view.launch.blockers[0].index == 3);
    CHECK(view.launch.unresolvedDisabledCount == 2);
}
