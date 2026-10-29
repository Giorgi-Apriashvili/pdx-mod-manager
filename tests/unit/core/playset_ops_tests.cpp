#include "core/playset_ops.hpp"
#include "playset_test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

using namespace pmm;
using Catch::Matchers::ContainsSubstring;
using test::local;
using test::workshop;

namespace {

std::vector<PlaysetEntry> fiveEntries() {
    return {workshop("1", "Alpha"), workshop("2", "Bravo", false), workshop("3", "Charlie"),
            local("delta", "Delta"), workshop("5", "Echo", false)};
}

std::string order(const std::vector<PlaysetEntry>& entries) {
    std::string out;
    for (const PlaysetEntry& e : entries)
        out += e.nameHint.front();
    return out;
}

} // namespace

TEST_CASE("moveEntry places the entry at the target index", "[core][playset_ops]") {
    auto entries = fiveEntries();
    REQUIRE(moveEntry(entries, 0, 3).ok());
    CHECK(order(entries) == "BCDAE");
    REQUIRE(moveEntry(entries, 4, 0).ok());
    CHECK(order(entries) == "EBCDA");
    REQUIRE(moveEntry(entries, 2, 2).ok());
    CHECK(order(entries) == "EBCDA");

    const auto before = entries;
    CHECK(moveEntry(entries, 5, 0).error().code == ErrorCode::InvalidArgument);
    CHECK(moveEntry(entries, 0, 5).error().code == ErrorCode::InvalidArgument);
    CHECK(entries == before);
}

TEST_CASE("moveUp and moveDown swap with neighbours and stop at the edges",
          "[core][playset_ops]") {
    auto entries = fiveEntries();
    REQUIRE(moveUp(entries, 2).ok());
    CHECK(order(entries) == "ACBDE");
    REQUIRE(moveUp(entries, 0).ok());
    CHECK(order(entries) == "ACBDE");
    REQUIRE(moveDown(entries, 4).ok());
    CHECK(order(entries) == "ACBDE");
    REQUIRE(moveDown(entries, 0).ok());
    CHECK(order(entries) == "CABDE");
    CHECK_FALSE(moveUp(entries, 9).ok());
    CHECK_FALSE(moveDown(entries, 9).ok());
}

TEST_CASE("moveBlock gathers a multi-selection at the drop gap", "[core][playset_ops]") {
    auto entries = fiveEntries();
    const std::vector<std::size_t> selection{4, 1}; // any order

    SECTION("to the top") {
        auto start = moveBlock(entries, selection, 0);
        REQUIRE(start.ok());
        CHECK(*start == 0);
        CHECK(order(entries) == "BEACD");
    }
    SECTION("to the bottom") {
        auto start = moveBlock(entries, selection, 5);
        REQUIRE(start.ok());
        CHECK(*start == 3);
        CHECK(order(entries) == "ACDBE");
    }
    SECTION("into a gap between unselected entries") {
        auto start = moveBlock(entries, selection, 3); // before "D"
        REQUIRE(start.ok());
        CHECK(*start == 2);
        CHECK(order(entries) == "ACBED");
    }
    SECTION("onto a gap inside the selection") {
        const std::vector<std::size_t> spread{0, 2, 4};
        auto start = moveBlock(entries, spread, 2);
        REQUIRE(start.ok());
        CHECK(*start == 1);
        CHECK(order(entries) == "BACED");
    }
    SECTION("invalid arguments change nothing") {
        const auto before = entries;
        const std::vector<std::size_t> bad{1, 7};
        CHECK(moveBlock(entries, bad, 0).error().code == ErrorCode::InvalidArgument);
        CHECK(moveBlock(entries, selection, 6).error().code == ErrorCode::InvalidArgument);
        CHECK(moveBlock(entries, std::vector<std::size_t>{}, 0).error().code ==
              ErrorCode::InvalidArgument);
        CHECK(entries == before);
    }
}

TEST_CASE("Selection Move Up/Down keeps blocks together and respects edges",
          "[core][playset_ops]") {
    auto entries = fiveEntries();
    auto moved = moveSelectionUp(entries, std::vector<std::size_t>{0, 2, 3});
    REQUIRE(moved.ok());
    CHECK(order(entries) == "ACDBE"); // A is blocked at the top; C and D move up together
    CHECK(*moved == std::vector<std::size_t>{0, 1, 2});

    moved = moveSelectionDown(entries, std::vector<std::size_t>{3, 4, 1});
    REQUIRE(moved.ok());
    CHECK(order(entries) == "ADCBE"); // B and E are blocked at the bottom
    CHECK(*moved == std::vector<std::size_t>{2, 3, 4});

    CHECK_FALSE(moveSelectionUp(entries, std::vector<std::size_t>{5}).ok());
}

TEST_CASE("setEnabled changes only the named entries and reports the count",
          "[core][playset_ops]") {
    auto entries = fiveEntries();
    auto changed = setEnabled(entries, std::vector<std::size_t>{1, 2, 4, 1}, true);
    REQUIRE(changed.ok());
    CHECK(*changed == 2); // entry 2 was already enabled
    for (const auto& e : entries)
        CHECK(e.enabled);

    const auto before = entries;
    CHECK_FALSE(setEnabled(entries, std::vector<std::size_t>{0, 9}, false).ok());
    CHECK(entries == before); // validated before any change
}

TEST_CASE("Removing entries can be undone exactly", "[core][playset_ops]") {
    auto entries = fiveEntries();
    const auto original = entries;
    auto removed = removeEntries(entries, std::vector<std::size_t>{3, 0});
    REQUIRE(removed.ok());
    CHECK(order(entries) == "BCE");
    REQUIRE(removed->size() == 2);
    CHECK((*removed)[0].index == 0);
    CHECK((*removed)[1].index == 3);

    REQUIRE(restoreEntries(entries, *removed).ok());
    CHECK(entries == original);

    // Restoring after the mod was added back is refused without changes.
    auto again = removeEntries(entries, std::vector<std::size_t>{1});
    REQUIRE(again.ok());
    REQUIRE(addEntries(entries, std::vector<PlaysetEntry>{(*again)[0].entry}).ok());
    const auto before = entries;
    CHECK(restoreEntries(entries, *again).error().code == ErrorCode::AlreadyExists);
    CHECK(entries == before);
}

TEST_CASE("addEntries appends and refuses duplicate identities", "[core][playset_ops]") {
    auto entries = fiveEntries();
    REQUIRE(addEntries(entries, std::vector<PlaysetEntry>{workshop("6", "Foxtrot"),
                                                          local("7", "Golf", false)})
                .ok());
    CHECK(order(entries) == "ABCDEFG");
    CHECK_FALSE(entries.back().enabled);

    const auto before = entries;
    // Same id and source as an existing entry, even with a different name.
    CHECK(addEntries(entries, std::vector<PlaysetEntry>{workshop("8", "Hotel"), workshop("1", "Other")})
              .error()
              .code == ErrorCode::AlreadyExists);
    CHECK(addEntries(entries, std::vector<PlaysetEntry>{workshop("9", "India"), workshop("9", "India")})
              .error()
              .code == ErrorCode::AlreadyExists);
    CHECK(entries == before);
    // A local mod may share its id text with a Workshop id.
    CHECK(addEntries(entries, std::vector<PlaysetEntry>{local("1", "Local one")}).ok());
    CHECK(findEntry(entries, ModIdentity{"stellaris", source::Local, "1"}) == entries.size() - 1);
}

TEST_CASE("Reordering is only allowed in the unfiltered load-order view",
          "[core][playset_ops]") {
    ViewFilter filter;
    CHECK(reorderPolicy(filter).allowed);
    CHECK(reorderPolicy(filter).explanation.empty());

    filter.search = "  \t";
    CHECK(reorderPolicy(filter).allowed); // whitespace-only search is no search

    filter.search = "ui";
    ReorderPolicy policy = reorderPolicy(filter);
    CHECK_FALSE(policy.allowed);
    CHECK(policy.explanation == "Clear the search and filters to change load order.");

    filter.search.clear();
    for (const StatusFilter status : {StatusFilter::Enabled, StatusFilter::Disabled,
                                      StatusFilter::Missing, StatusFilter::Warning}) {
        filter.status = status;
        policy = reorderPolicy(filter);
        CHECK_FALSE(policy.allowed);
        CHECK_THAT(policy.explanation, ContainsSubstring("Clear the search and filters"));
    }

    filter.status = StatusFilter::All;
    filter.browseSorted = true;
    policy = reorderPolicy(filter);
    CHECK_FALSE(policy.allowed);
    CHECK_THAT(policy.explanation, ContainsSubstring("load-order sorting"));
    CHECK_FALSE(isUnfiltered(filter));
}

TEST_CASE("visibleIndices filters and sorts the view without touching load order",
          "[core][playset_ops]") {
    std::vector<PlaysetEntry> entries = {
        workshop("100", "Zeta UI"), workshop("200", "alpha ships", false),
        workshop("300", "Beta Events"), local("my-ui-fix", "日本語 Ui Patch"),
        workshop("500", "Gamma", false)};
    std::vector<EntryReadiness> readiness(entries.size());
    readiness[0].status = EntryStatus::Ready;
    readiness[1].status = EntryStatus::Missing;
    readiness[2].status = EntryStatus::Ready;
    readiness[2].warnings = {"version differs"};
    readiness[3].status = EntryStatus::NeedsUserAction;
    readiness[4].status = EntryStatus::Broken;
    const auto snapshot = entries;

    ViewFilter filter;
    CHECK(visibleIndices(entries, readiness, filter) == std::vector<std::size_t>{0, 1, 2, 3, 4});

    filter.search = " UI ";
    CHECK(visibleIndices(entries, readiness, filter) == std::vector<std::size_t>{0, 3});
    filter.search = "300"; // ids are searchable too
    CHECK(visibleIndices(entries, readiness, filter) == std::vector<std::size_t>{2});
    filter.search = "日本";
    CHECK(visibleIndices(entries, readiness, filter) == std::vector<std::size_t>{3});

    filter.search.clear();
    filter.status = StatusFilter::Enabled;
    CHECK(visibleIndices(entries, readiness, filter) == std::vector<std::size_t>{0, 2, 3});
    filter.status = StatusFilter::Disabled;
    CHECK(visibleIndices(entries, readiness, filter) == std::vector<std::size_t>{1, 4});
    filter.status = StatusFilter::Missing;
    CHECK(visibleIndices(entries, readiness, filter) == std::vector<std::size_t>{1, 3});
    filter.status = StatusFilter::Warning;
    CHECK(visibleIndices(entries, readiness, filter) == std::vector<std::size_t>{2, 4});

    filter.status = StatusFilter::All;
    filter.browseSorted = true;
    CHECK(visibleIndices(entries, readiness, filter) == std::vector<std::size_t>{1, 2, 4, 0, 3});

    // Without readiness information only the flag-based filters match.
    filter = ViewFilter{};
    filter.status = StatusFilter::Missing;
    CHECK(visibleIndices(entries, std::vector<EntryReadiness>{}, filter).empty());
    filter.status = StatusFilter::Enabled;
    CHECK(visibleIndices(entries, std::vector<EntryReadiness>{}, filter).size() == 3);

    CHECK(entries == snapshot);
}
