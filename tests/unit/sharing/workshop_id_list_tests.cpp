#include "sharing/share_code.hpp"
#include "sharing/workshop_id_list.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::sharing;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

namespace {

using Ids = std::vector<std::string>;

Ids parsed(std::string_view text) {
    auto ids = parseWorkshopIdList(text);
    if (!ids.ok())
        FAIL("unexpected error: " << ids.error().message << " | " << ids.error().detail);
    return std::move(ids).value();
}

Error parseError(std::string_view text) {
    auto ids = parseWorkshopIdList(text);
    REQUIRE_FALSE(ids.ok());
    return ids.error();
}

PlaysetEntry entry(const char* sourceKey, const char* id, bool enabled = true) {
    PlaysetEntry out;
    out.identity = ModIdentity{"stellaris", sourceKey, id};
    out.enabled = enabled;
    return out;
}

} // namespace

TEST_CASE("Workshop ID lists in the export form parse in order", "[sharing][idlist]") {
    CHECK(parsed("3818297502|3149445431|3366734814") ==
          Ids{"3818297502", "3149445431", "3366734814"});
    CHECK(parsed("42") == Ids{"42"});
    CHECK(parsed("18446744073709551615|1") == Ids{"18446744073709551615", "1"});
}

TEST_CASE("Workshop ID lists accept every separator, and runs of them", "[sharing][idlist]") {
    for (const char* separator :
         {"|", ",", " ", "\t", "\r", "\n", "\r\n", "\xC2\xA0", "\xE2\x80\xAF", "\xE2\x80\x8B",
          "\xEF\xBB\xBF", ", ", " | ", "||", "\r\n\r\n", ",\xC2\xA0\xE2\x80\x8B"}) {
        INFO(std::string(separator));
        const std::string text = std::string("101") + separator + "202" + separator + "303";
        CHECK(parsed(text) == Ids{"101", "202", "303"});
        // Leading and trailing separators are ignored as well.
        CHECK(parsed(std::string(separator) + text + separator) == Ids{"101", "202", "303"});
    }
    // A file saved with a byte order mark and Windows line ends.
    CHECK(parsed("\xEF\xBB\xBF"
                 "101\r\n202\r\n") == Ids{"101", "202"});
}

TEST_CASE("Workshop ID lists accept links to Steam Workshop item pages", "[sharing][idlist]") {
    for (const char* link :
         {"https://steamcommunity.com/sharedfiles/filedetails/?id=3149445431",
          "http://steamcommunity.com/sharedfiles/filedetails/?id=3149445431",
          "steamcommunity.com/sharedfiles/filedetails/?id=3149445431",
          "www.steamcommunity.com/sharedfiles/filedetails/?id=3149445431",
          "https://www.steamcommunity.com/sharedfiles/filedetails/?id=3149445431",
          "https://steamcommunity.com/workshop/filedetails/?id=3149445431",
          "https://steamcommunity.com/sharedfiles/filedetails?id=3149445431",
          "HTTPS://SteamCommunity.COM/SharedFiles/FileDetails/?id=3149445431",
          "https://steamcommunity.com/sharedfiles/filedetails/?searchtext=x&id=3149445431",
          "https://steamcommunity.com/sharedfiles/filedetails/?id=3149445431&l=german",
          "https://steamcommunity.com/sharedfiles/filedetails/?id=3149445431#comments",
          "https://steamcommunity.com/sharedfiles/filedetails/?l=en&id=3149445431&tscn=1#x"}) {
        INFO(link);
        CHECK(parsed(std::string("3818297502 ") + link + "|3366734814") ==
              Ids{"3818297502", "3149445431", "3366734814"});
    }
}

TEST_CASE("Workshop ID list entries that are not ids are refused by number", "[sharing][idlist]") {
    SECTION("the first entry: neither a share code nor a list") {
        const Error e = parseError("hello world");
        CHECK(e.code == ErrorCode::ParseError);
        CHECK(e.message == "This text is neither a share code (it starts with \"PMM1.\") nor a "
                           "list of Workshop IDs: \"hello\" is not a Workshop ID or a Steam "
                           "Workshop link.");
        CHECK(e.detail == "entry 1 at byte offset 0");
    }
    SECTION("the first entry: a share code behind an invisible separator") {
        for (const char* before : {"\xE2\x80\x8B", "\xEF\xBB\xBF", "\xC2\xA0", " \xE2\x80\x8B"}) {
            INFO(std::string(before));
            const Error e = parseError(std::string(before) + "PMM1.abc.def");
            CHECK(e.code == ErrorCode::ParseError);
            CHECK(e.message == "This looks like a share code, but an invisible character or a "
                               "special space comes before \"PMM\". Copy the code again, "
                               "starting at PMM1.");
        }
        // Only the first entry.
        CHECK_THAT(parseError("101 PMM1.abc.def").message,
                   StartsWith("Entry 2 of the Workshop ID list, \"PMM1.abc.def\""));
    }
    SECTION("later entries") {
        for (const char* bad : {"12a4", "0123", "0", "18446744073709551616",
                                "123456789012345678901", "-5", "+5", "1.0", "１２", "id=5"}) {
            INFO(bad);
            const Error e = parseError(std::string("101|202, ") + bad + "|303");
            CHECK(e.code == ErrorCode::ParseError);
            CHECK_THAT(e.message,
                       StartsWith("Entry 3 of the Workshop ID list, \"" + std::string(bad) +
                                  "\", is not a Workshop ID or a Steam Workshop link."));
            CHECK(e.detail == "entry 3 at byte offset 9");
        }
    }
    SECTION("control characters are quoted printably") {
        const Error e = parseError(std::string("101|1\x01"
                                               "2"));
        CHECK_THAT(e.message, ContainsSubstring("\"1?2\""));
    }
    SECTION("direction controls and line separators are quoted as '?'") {
        // U+202E, U+2066, U+200F, U+061C, U+2028 and U+2029.
        const Error e = parseError("101|\xE2\x80\xAE"
                                   "4321\xE2\x81\xA6"
                                   "x\xE2\x80\x8F\xD8\x9C\xE2\x80\xA8\xE2\x80\xA9"
                                   "y");
        CHECK_THAT(e.message, StartsWith("Entry 2 of the Workshop ID list, \"?4321?x????y\","));
    }
    SECTION("a position suffix is explained") {
        const Error e = parseError("3818297502|3149445431;2|3366734814");
        CHECK(e.code == ErrorCode::ParseError);
        CHECK(e.message ==
              "Entry 2, \"3149445431;2\", has a \";2\" suffix. Other mod managers use it to pin a "
              "load position; Paradox games load mods in list order, so remove the suffixes and "
              "put the IDs in the order the mods should load.");
        CHECK(e.detail == "entry 2 at byte offset 11");
        // Also on the first entry, and after a link.
        CHECK_THAT(parseError("3818297502;1").message,
                   StartsWith("Entry 1, \"3818297502;1\", has a"));
        // A link is quoted far enough to show its id.
        CHECK_THAT(
            parseError("steamcommunity.com/sharedfiles/filedetails/?id=3818297502;7").message,
            StartsWith("Entry 1, \"steamcommunity.com/sharedfiles/filedetails/?id=3818297502;7\", "
                       "has a \";7\" suffix"));
        // Not a suffix: the generic message.
        CHECK_THAT(parseError("101|202;x").message, StartsWith("Entry 2 of the Workshop ID list"));
        CHECK_THAT(parseError("101|202;").message, StartsWith("Entry 2 of the Workshop ID list"));
    }
    SECTION("links to other pages") {
        for (const char* link : {"https://example.com/sharedfiles/filedetails/?id=101",
                                 "https://steamcommunity.com/id/someone",
                                 "https://steamcommunity.com/sharedfiles/filedetailsx?id=101",
                                 "https://steamcommunity.com/app/281990/workshop/",
                                 "steam://url/CommunityFilePage/101", "www.example.com"}) {
            INFO(link);
            const Error e = parseError(std::string("101 ") + link);
            CHECK(e.code == ErrorCode::ParseError);
            CHECK(e.message == "Entry 2, \"" + std::string(link) +
                                   "\", is a link, but not to a Steam Workshop item page.");
        }
    }
    SECTION("item links without one valid id") {
        for (const char* link : {"https://steamcommunity.com/sharedfiles/filedetails/",
                                 "https://steamcommunity.com/sharedfiles/filedetails/?l=en",
                                 "https://steamcommunity.com/sharedfiles/filedetails/?id=",
                                 "https://steamcommunity.com/sharedfiles/filedetails/?id=0",
                                 "https://steamcommunity.com/sharedfiles/filedetails/?ID=101",
                                 "https://steamcommunity.com/sharedfiles/filedetails/#id=101"}) {
            INFO(link);
            CHECK_THAT(parseError(link).message,
                       ContainsSubstring("is a Steam Workshop link without a valid item ID"));
        }
        const Error twice =
            parseError("101 https://steamcommunity.com/sharedfiles/filedetails/?id=202&id=303");
        CHECK(twice.message == "Entry 2, \"https://steamcommunity.com/sharedfiles/filedetails/"
                               "?id=202&id=303\", is a Steam Workshop link with more than one "
                               "item ID.");
    }
}

TEST_CASE("Workshop ID lists refuse an item listed twice", "[sharing][idlist]") {
    Error e = parseError("101|202|303|404|505|606|202");
    CHECK(e.code == ErrorCode::ValidationFailed);
    CHECK(e.message == "Workshop ID 202 is listed twice, as entry 2 and entry 7. Remove one of "
                       "them: a playset holds each mod once.");
    // An id and a link to the same item.
    e = parseError("101 https://steamcommunity.com/sharedfiles/filedetails/?id=101");
    CHECK(e.code == ErrorCode::ValidationFailed);
    CHECK_THAT(e.message, StartsWith("Workshop ID 101 is listed twice, as entry 1 and entry 2."));
}

TEST_CASE("Workshop ID lists need at least one id", "[sharing][idlist]") {
    for (const char* empty : {"", " ", "|||", "\r\n", "\xEF\xBB\xBF", ", ,"}) {
        INFO(empty);
        const Error e = parseError(empty);
        CHECK(e.code == ErrorCode::ParseError);
        CHECK(e.message == "The text contains no Workshop IDs. Paste a share code (PMM1.…) or IDs "
                           "such as 3818297502|3149445431.");
    }
}

TEST_CASE("Workshop ID lists are limited to 10000 entries and 2 MiB", "[sharing][idlist][limits]") {
    std::vector<std::string> ids;
    for (std::size_t i = 0; i < kMaxEntries; ++i)
        ids.push_back(std::to_string(1'000'000'000ULL + i));
    const std::string full = formatWorkshopIdList(ids);
    CHECK(parsed(full).size() == kMaxEntries);

    Error e = parseError(full + "|42");
    CHECK(e.code == ErrorCode::LimitExceeded);
    CHECK(e.message == "The Workshop ID list has more than 10000 entries, more than a playset can "
                       "hold.");
    CHECK_THAT(e.detail, StartsWith("entry 10001 at byte offset"));
    // The 10001st entry is not read: the limit decides first.
    CHECK(parseError(full + "|not-an-id").code == ErrorCode::LimitExceeded);

    // The size is checked before anything else, even before a bad first entry.
    std::string large(kMaxIdListInputBytes, ' ');
    large.replace(0, 3, "101");
    CHECK(parsed(large) == Ids{"101"});
    large.push_back(' ');
    e = parseError(large);
    CHECK(e.code == ErrorCode::LimitExceeded);
    CHECK(e.message == "This text is too large to be a share code or a Workshop ID list (limit 2 "
                       "MiB).");
    large.replace(0, 3, "bad");
    CHECK(parseError(large).code == ErrorCode::LimitExceeded);
}

TEST_CASE("Workshop ID lists format with '|' and round-trip", "[sharing][idlist]") {
    const Ids ids{"3818297502", "3149445431", "3366734814"};
    CHECK(formatWorkshopIdList(ids) == "3818297502|3149445431|3366734814");
    CHECK(formatWorkshopIdList(Ids{}).empty());
    CHECK(formatWorkshopIdList(Ids{"7"}) == "7");
    CHECK(parsed(formatWorkshopIdList(ids)) == ids);
    // Any accepted form formats to the export form.
    CHECK(formatWorkshopIdList(parsed(
              " 3818297502,\r\nhttps://steamcommunity.com/sharedfiles/filedetails/?id=3149445431 "
              "3366734814\n")) == "3818297502|3149445431|3366734814");
}

TEST_CASE("A Workshop ID list maps to a manifest for the selected game", "[sharing][idlist]") {
    const Ids ids{"3818297502", "3149445431"};
    for (const char* game : {"ck3", "eu4", "stellaris"}) {
        INFO(game);
        auto manifest = manifestFromWorkshopIds(game, ids);
        REQUIRE(manifest.ok());
        CHECK(manifest->schemaVersion == kManifestSchemaVersion);
        CHECK(manifest->game == game);
        CHECK(manifest->playsetName == "Imported mod list");
        CHECK(manifest->gameVersionHint.empty());
        REQUIRE(manifest->mods.size() == 2);
        for (std::size_t i = 0; i < ids.size(); ++i) {
            CHECK(manifest->mods[i] == ManifestEntry{"steam-workshop", ids[i], "", true, "", ""});
        }
        CHECK(validateManifest(*manifest).ok());
    }
    auto named = manifestFromWorkshopIds("eu4", ids, "My list");
    REQUIRE(named.ok());
    CHECK(named->playsetName == "My list");

    CHECK(manifestFromWorkshopIds("hoi4", ids).error().code == ErrorCode::UnsupportedGame);
    CHECK(manifestFromWorkshopIds("unknown", ids).error().code == ErrorCode::ValidationFailed);
    CHECK(manifestFromWorkshopIds("", ids).error().code == ErrorCode::ValidationFailed);
    CHECK(manifestFromWorkshopIds("stellaris", Ids{"0"}).error().code ==
          ErrorCode::ValidationFailed);
    CHECK(manifestFromWorkshopIds("stellaris", Ids{"1", "1"}).error().code ==
          ErrorCode::ValidationFailed);
}

TEST_CASE("A playset's Workshop ID list holds its enabled Workshop mods in order",
          "[sharing][idlist]") {
    Playset playset;
    playset.game = "stellaris";
    playset.name = "Mixed";
    playset.entries = {
        entry("steam-workshop", "1003"),        entry("local", "my mod"),
        entry("steam-workshop", "1001", false), entry("steam-workshop", "1002"),
        entry("local", "other", false),         entry("steam-workshop", "1004", false)};
    auto list = workshopIdListFromPlayset(playset);
    REQUIRE(list.ok());
    CHECK(list->text == "1003|1002");
    CHECK(list->ids == Ids{"1003", "1002"});
    CHECK(list->leftOutLocal == 2);
    CHECK(list->leftOutDisabled == 2);
    // The list imports into the same enabled Workshop mods, in the same order.
    CHECK(parsed(list->text) == list->ids);

    SECTION("nothing to list") {
        playset.entries = {entry("local", "a"), entry("steam-workshop", "5", false)};
        list = workshopIdListFromPlayset(playset);
        REQUIRE(list.ok());
        CHECK(list->text.empty());
        CHECK(list->ids.empty());
        CHECK(list->leftOutLocal == 1);
        CHECK(list->leftOutDisabled == 1);
        playset.entries.clear();
        list = workshopIdListFromPlayset(playset);
        REQUIRE(list.ok());
        CHECK(list->text.empty());
        CHECK(list->leftOutLocal + list->leftOutDisabled == 0);
    }
    SECTION("entries of another game, malformed ids and repeats are refused") {
        playset.entries[3].identity.game = "eu4";
        CHECK(workshopIdListFromPlayset(playset).error().code == ErrorCode::WrongGame);
        playset.entries[3].identity.game = "stellaris";
        playset.entries[3].identity.id = "01002";
        CHECK(workshopIdListFromPlayset(playset).error().code == ErrorCode::ValidationFailed);
        playset.entries[3].identity.id = "1003";
        const Error e = workshopIdListFromPlayset(playset).error();
        CHECK(e.code == ErrorCode::ValidationFailed);
        CHECK(e.message == "Entries #1 and #4 are the same Workshop mod (1003), so the playset "
                           "cannot be shared.");
        // A disabled repeat is left out, not refused.
        playset.entries[3].enabled = false;
        CHECK(workshopIdListFromPlayset(playset).ok());
    }
}

TEST_CASE("Text starting with PMM is a share code, anything else a list", "[sharing][idlist]") {
    using enum ImportTextFormat;
    CHECK(detectImportTextFormat("PMM1.abc.def") == ShareCode);
    CHECK(detectImportTextFormat(" \t\r\nPMM9.x") == ShareCode);
    CHECK(detectImportTextFormat("PMM") == ShareCode);
    CHECK(detectImportTextFormat("PMMx") == ShareCode);
    CHECK(detectImportTextFormat("pmm1.abc") == WorkshopIdList);
    CHECK(detectImportTextFormat("PM") == WorkshopIdList);
    CHECK(detectImportTextFormat("3818297502|PMM1.x") == WorkshopIdList);
    CHECK(detectImportTextFormat("\xEF\xBB\xBFPMM1.x") == WorkshopIdList);
    CHECK(detectImportTextFormat("") == WorkshopIdList);
    CHECK(detectImportTextFormat("   ") == WorkshopIdList);
    // A share code is still decoded as one.
    CHECK(decodeShareCode("PMM9.abc.def").error().code == ErrorCode::UnsupportedVersion);
}
