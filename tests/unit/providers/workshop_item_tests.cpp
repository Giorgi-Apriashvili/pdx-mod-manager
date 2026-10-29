#include "providers/fallback.hpp"
#include "providers/workshop_item.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>

using namespace pmm;
using namespace pmm::providers;

TEST_CASE("EItemState flags decode individually and combined", "[providers][workshop]") {
    const ItemFlags none = decodeItemFlags(0);
    CHECK_FALSE(none.subscribed);
    CHECK_FALSE(none.installed);
    CHECK_FALSE(none.installedAndCurrent());

    const ItemFlags installed = decodeItemFlags(item_flag::Subscribed | item_flag::Installed);
    CHECK(installed.subscribed);
    CHECK(installed.installed);
    CHECK_FALSE(installed.legacy);
    CHECK(installed.installedAndCurrent());

    for (std::uint32_t extra :
         {item_flag::NeedsUpdate, item_flag::Downloading, item_flag::DownloadPending}) {
        const ItemFlags f = decodeItemFlags(item_flag::Subscribed | item_flag::Installed | extra);
        CHECK(f.installed);
        CHECK_FALSE(f.installedAndCurrent());
    }

    const ItemFlags all = decodeItemFlags(63);
    CHECK(all.subscribed);
    CHECK(all.legacy);
    CHECK(all.installed);
    CHECK(all.needsUpdate);
    CHECK(all.downloading);
    CHECK(all.downloadPending);
    CHECK(all.raw == 63);
}

TEST_CASE("Item flags have readable descriptions", "[providers][workshop]") {
    CHECK(describeItemFlags(0) == "none");
    CHECK(describeItemFlags(5) == "subscribed|installed");
    CHECK(describeItemFlags(1 | 16) == "subscribed|downloading");
    CHECK(describeItemFlags(2 | 8 | 32) == "legacy|needs_update|download_pending");
    CHECK(describeItemFlags(4 | 64) == "installed|0x40");
}

TEST_CASE("Published file ids must be canonical decimal uint64", "[providers][workshop]") {
    CHECK(isValidPublishedFileId("1"));
    CHECK(isValidPublishedFileId("703156866"));
    CHECK(isValidPublishedFileId("18446744073709551615")); // 2^64-1
    CHECK(parsePublishedFileId("3090328185") == std::uint64_t{3090328185});

    const char* bad = GENERATE("", "0", "0123", "-1", "+1", " 1", "1 ", "12a", "1.0", "0x10",
                               "18446744073709551616", "999999999999999999999", "\xD9\xA1");
    INFO("id: " << bad);
    CHECK_FALSE(isValidPublishedFileId(bad));
}

TEST_CASE("Steam AppIds must be canonical decimal uint32", "[providers][workshop]") {
    CHECK(parseSteamAppId("281990") == std::uint32_t{281990});
    CHECK(parseSteamAppId("4294967295") == std::uint32_t{4294967295u});
    CHECK_FALSE(isValidSteamAppId("4294967296"));
    CHECK_FALSE(isValidSteamAppId("0"));
    CHECK_FALSE(isValidSteamAppId("281990;rm"));
    CHECK_FALSE(isValidSteamAppId(""));
}

TEST_CASE("EResult values map to error codes and dispositions", "[providers][workshop]") {
    struct Row {
        int value;
        ErrorCode code;
        FailureDisposition disposition;
    };
    const Row rows[] = {
        {2, ErrorCode::ProviderUnavailable, FailureDisposition::Retryable},
        {3, ErrorCode::Offline, FailureDisposition::Retryable},
        {8, ErrorCode::ItemUnavailable, FailureDisposition::Unavailable},
        {9, ErrorCode::ItemUnavailable, FailureDisposition::Unavailable},
        {10, ErrorCode::Busy, FailureDisposition::Retryable},
        {15, ErrorCode::AccessDenied, FailureDisposition::Unavailable},
        {16, ErrorCode::Timeout, FailureDisposition::Retryable},
        {17, ErrorCode::AccessDenied, FailureDisposition::Unavailable},
        {20, ErrorCode::ProviderUnavailable, FailureDisposition::Retryable},
        {21, ErrorCode::ProviderUnavailable, FailureDisposition::UserAction},
        {24, ErrorCode::AccessDenied, FailureDisposition::UserAction},
        {25, ErrorCode::LimitExceeded, FailureDisposition::UserAction},
        {26, ErrorCode::ItemUnavailable, FailureDisposition::Unavailable},
        {35, ErrorCode::Offline, FailureDisposition::Retryable},
        {37, ErrorCode::IoError, FailureDisposition::Retryable},
        {54, ErrorCode::IoError, FailureDisposition::UserAction},
        {84, ErrorCode::Busy, FailureDisposition::Retryable},
        {86, ErrorCode::ItemUnavailable, FailureDisposition::Unavailable},
    };
    for (const Row& row : rows) {
        INFO("EResult " << row.value);
        const SteamResultInfo info = classifySteamResult(row.value);
        CHECK(info.value == row.value);
        CHECK(info.code == row.code);
        CHECK(info.disposition == row.disposition);
        CHECK_FALSE(info.ok());
        CHECK(info.retryable() == (row.disposition == FailureDisposition::Retryable));
        CHECK_FALSE(info.message.empty());
        CHECK(info.name != "Unknown");
    }

    const SteamResultInfo ok = classifySteamResult(1);
    CHECK(ok.ok());
    CHECK(ok.name == "OK");

    const SteamResultInfo unknown = classifySteamResult(9999);
    CHECK(unknown.name == "Unknown");
    CHECK(unknown.retryable());
    CHECK(unknown.code == ErrorCode::ProviderUnavailable);
}

TEST_CASE("Steam result errors carry context and the numeric EResult", "[providers][workshop]") {
    const Error e = steamResultError(9, "Subscribing to Workshop item 42");
    CHECK(e.code == ErrorCode::ItemUnavailable);
    CHECK(e.message.starts_with("Subscribing to Workshop item 42 failed: "));
    CHECK(e.message.ends_with("."));
    CHECK(e.detail == "EResult 9 (FileNotFound)");
}

TEST_CASE("Manual fallback links are built only from valid ids", "[providers][fallback]") {
    auto client = steamClientItemUrl("703156866");
    REQUIRE(client.ok());
    CHECK(*client == "steam://url/CommunityFilePage/703156866");

    auto web = workshopWebItemUrl("703156866");
    REQUIRE(web.ok());
    CHECK(*web == "https://steamcommunity.com/sharedfiles/filedetails/?id=703156866");

    auto links = manualFallbackLinks("1121692237");
    REQUIRE(links.ok());
    CHECK(links->steamClientUrl == "steam://url/CommunityFilePage/1121692237");
    CHECK(links->webUrl == "https://steamcommunity.com/sharedfiles/filedetails/?id=1121692237");

    for (const char* bad :
         {"", "0", "12&evil=1", "../../x", "javascript:alert(1)", "123456789012345678901", "1 2"}) {
        INFO("id: " << bad);
        auto r = manualFallbackLinks(bad);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().code == ErrorCode::InvalidArgument);
        CHECK_FALSE(steamClientItemUrl(bad).ok());
        CHECK_FALSE(workshopWebItemUrl(bad).ok());
    }
    CHECK(std::string(kManualFallbackLabel).find("manual") != std::string::npos);
}
