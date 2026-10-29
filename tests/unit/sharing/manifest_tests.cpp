#include "sharing/manifest.hpp"
#include "sharing_test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

using namespace pmm;
using namespace pmm::sharing;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using test::manifestJson;

namespace {

Error parseError(const std::string& json) {
    auto r = parseManifestJson(json);
    REQUIRE_FALSE(r.ok());
    return r.error();
}

std::string entryJson(std::string_view members) {
    return "[{" + std::string(members) + "}]";
}

std::string repeatedArray(std::string_view element, std::size_t count) {
    std::string json = "[";
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0)
            json += ',';
        json += element;
    }
    return json + "]";
}

// `count` members "k0":0, "k1":0, ... without the surrounding braces.
std::string numberedMembers(std::size_t count) {
    std::string json;
    for (std::size_t i = 0; i < count; ++i)
        json += (i > 0 ? ",\"k" : "\"k") + std::to_string(i) + "\":0";
    return json;
}

} // namespace

TEST_CASE("Canonical serialization uses fixed key order and omits empty optional fields",
          "[sharing][manifest]") {
    Manifest m;
    m.game = "stellaris";
    m.playsetName = "P \"q\" \\ Ü";
    m.mods = {{"steam-workshop", "42", "", false, "", ""},
              {"local", "loc", "Name", true, "1.0", "sha256:" + std::string(64, '0')}};
    auto json = serializeManifestJson(m);
    REQUIRE(json.ok());
    CHECK(*json == R"({"schemaVersion":1,"game":"stellaris","playsetName":"P \"q\" \\ Ü","mods":[)"
                   R"({"source":"steam-workshop","id":"42","enabled":false},)"
                   R"({"source":"local","id":"loc","nameHint":"Name","enabled":true,)"
                   R"("versionHint":"1.0","fingerprint":"sha256:)" +
                       std::string(64, '0') + R"("}]})");

    m.gameVersionHint = "v4.1.5";
    json = serializeManifestJson(m);
    REQUIRE(json.ok());
    CHECK_THAT(*json, StartsWith(R"({"schemaVersion":1,"game":"stellaris","playsetName":)"));
    CHECK_THAT(*json, ContainsSubstring(R"(,"gameVersionHint":"v4.1.5","mods":[)"));
}

TEST_CASE("Serialization round-trips through the strict parser", "[sharing][manifest]") {
    const Manifest m = test::sampleManifest();
    auto json = serializeManifestJson(m);
    REQUIRE(json.ok());
    auto parsed = parseManifestJson(*json);
    REQUIRE(parsed.ok());
    CHECK(*parsed == m);
}

TEST_CASE("Parser accepts any formatting, key order and escapes", "[sharing][manifest]") {
    const std::string json =
        " {\n \"mods\" : [ { \"enabled\" : true , \"id\" : \"7\" ,"
        " \"source\" : \"steam-workshop\", \"nameHint\": \"\\u00dc\\ud83d\\ude80\" } ],"
        "\t\"playsetName\":\"X\", \"game\":\"eu4\", \"schemaVersion\":1 }\r\n";
    auto parsed = parseManifestJson(json);
    REQUIRE(parsed.ok());
    CHECK(parsed->game == "eu4");
    REQUIRE(parsed->mods.size() == 1);
    CHECK(parsed->mods[0].nameHint == "Ü🚀");
}

TEST_CASE("Empty optional strings are equivalent to omitted fields", "[sharing][manifest]") {
    auto parsed = parseManifestJson(manifestJson(
        entryJson(
            R"("source":"local","id":"a","nameHint":"","enabled":true,"versionHint":"","fingerprint":"")"),
        R"("gameVersionHint":"",)"));
    REQUIRE(parsed.ok());
    CHECK(parsed->gameVersionHint.empty());
    CHECK(parsed->mods[0].nameHint.empty());
    auto json = serializeManifestJson(*parsed);
    REQUIRE(json.ok());
    CHECK_THAT(*json, !ContainsSubstring("nameHint"));
    CHECK_THAT(*json, !ContainsSubstring("gameVersionHint"));
}

TEST_CASE("Malformed JSON and UTF-8 are parse errors", "[sharing][manifest]") {
    for (const std::string& json : {
             std::string(""),
             std::string("{"),
             std::string(R"({"schemaVersion":1,})"),
             manifestJson() + "{}",                       // two root values
             manifestJson().substr(0, 40),                // truncated
             std::string(R"({"schemaVersion":1 /*c*/})"), // comments
             std::string(R"({'schemaVersion':1})"),
             std::string("\xEF\xBB\xBF") + manifestJson(), // byte order mark
             // Raw bytes (not JSON escapes): invalid, overlong, surrogate.
             std::string(R"({"schemaVersion":1,"game":"stellaris","playsetName":")"
                         "\xFF"
                         R"(","mods":[]})"),
             std::string(R"({"schemaVersion":1,"game":"stellaris","playsetName":")"
                         "\xC0\xAF"
                         R"(","mods":[]})"),
             std::string(R"({"schemaVersion":1,"game":"stellaris","playsetName":")"
                         "\xED\xA0\x80"
                         R"(","mods":[]})"),
             std::string(
                 R"({"schemaVersion":1,"game":"stellaris","playsetName":"\ud800","mods":[]})"),
         }) {
        INFO(json);
        CHECK(parseError(json).code == ErrorCode::ParseError);
    }
}

TEST_CASE("Root must be an object", "[sharing][manifest]") {
    for (const char* json : {"[]", "1", "\"x\"", "null", "true"}) {
        const Error e = parseError(json);
        CHECK(e.code == ErrorCode::ValidationFailed);
        CHECK_THAT(e.message, StartsWith("manifest:"));
    }
}

TEST_CASE("Duplicate keys are rejected at every level", "[sharing][manifest]") {
    Error e = parseError(
        R"({"schemaVersion":1,"game":"stellaris","game":"eu4","playsetName":"P","mods":[]})");
    CHECK(e.code == ErrorCode::ValidationFailed);
    CHECK_THAT(e.message, ContainsSubstring("duplicate key \"game\""));

    e = parseError(manifestJson(
        R"([{"source":"local","id":"a","enabled":true},{"source":"local","id":"b","id":"c","enabled":true}])"));
    CHECK(e.code == ErrorCode::ValidationFailed);
    CHECK_THAT(e.message, StartsWith("mods[1]: duplicate key \"id\""));
}

TEST_CASE("Unknown keys are rejected at every level", "[sharing][manifest]") {
    Error e = parseError(manifestJson("[]", R"("author":"someone",)"));
    CHECK(e.code == ErrorCode::ValidationFailed);
    CHECK_THAT(e.message, ContainsSubstring("unknown field \"author\""));

    e = parseError(
        manifestJson(entryJson(R"("source":"local","id":"a","enabled":true,"url":"https://x")")));
    CHECK(e.code == ErrorCode::ValidationFailed);
    CHECK_THAT(e.message, StartsWith("mods[0]: unknown field \"url\""));
}

TEST_CASE("schemaVersion is checked first and must be exactly 1", "[sharing][manifest]") {
    // A newer schema may add fields: report the version, not the unknown field.
    Error e = parseError(R"({"schemaVersion":2,"game":"stellaris","newField":true})");
    CHECK(e.code == ErrorCode::UnsupportedVersion);
    CHECK_THAT(e.message, ContainsSubstring("newer version"));
    CHECK_THAT(e.message, ContainsSubstring("Update the app"));

    CHECK(parseError(R"({"schemaVersion":0,"game":"stellaris"})").code ==
          ErrorCode::UnsupportedVersion);
    CHECK(parseError(R"({"schemaVersion":-1,"game":"stellaris"})").code ==
          ErrorCode::UnsupportedVersion);
    CHECK(parseError(R"({"schemaVersion":18446744073709551615,"game":"stellaris"})").code ==
          ErrorCode::UnsupportedVersion);
    for (const char* bad :
         {R"({"schemaVersion":"1"})", R"({"schemaVersion":1.0})", R"({"schemaVersion":1e0})",
          R"({"schemaVersion":null})", R"({"game":"stellaris"})"}) {
        e = parseError(bad);
        CHECK(e.code == ErrorCode::ValidationFailed);
        CHECK_THAT(e.message, StartsWith("schemaVersion:"));
    }
}

TEST_CASE("Wrong field types and missing fields name the JSON path", "[sharing][manifest]") {
    struct Case {
        std::string json;
        const char* path;
    };
    const Case cases[] = {
        {R"({"schemaVersion":1,"game":5,"playsetName":"P","mods":[]})", "game:"},
        {R"({"schemaVersion":1,"playsetName":"P","mods":[]})", "game:"},
        {R"({"schemaVersion":1,"game":"stellaris","playsetName":null,"mods":[]})", "playsetName:"},
        {R"({"schemaVersion":1,"game":"stellaris","mods":[]})", "playsetName:"},
        {R"({"schemaVersion":1,"game":"stellaris","playsetName":"P","gameVersionHint":4,"mods":[]})",
         "gameVersionHint:"},
        {R"({"schemaVersion":1,"game":"stellaris","playsetName":"P","mods":{}})", "mods:"},
        {R"({"schemaVersion":1,"game":"stellaris","playsetName":"P"})", "mods:"},
        {manifestJson(R"(["x"])"), "mods[0]:"},
        {manifestJson(entryJson(R"("source":"local","id":"a","enabled":"true")")),
         "mods[0].enabled:"},
        {manifestJson(entryJson(R"("source":"local","id":"a","enabled":1)")), "mods[0].enabled:"},
        {manifestJson(entryJson(R"("source":"local","id":"a")")), "mods[0].enabled:"},
        {manifestJson(entryJson(R"("source":"steam-workshop","id":123,"enabled":true)")),
         "mods[0].id:"},
        {manifestJson(entryJson(R"("id":"1","enabled":true)")), "mods[0].source:"},
        {manifestJson(entryJson(R"("source":"local","enabled":true)")), "mods[0].id:"},
        {manifestJson(entryJson(R"("source":"local","id":"a","enabled":true,"nameHint":[])")),
         "mods[0].nameHint:"},
        {manifestJson(entryJson(R"("source":"local","id":"a","enabled":true,"versionHint":1)")),
         "mods[0].versionHint:"},
        {manifestJson(entryJson(R"("source":"local","id":"a","enabled":true,"fingerprint":false)")),
         "mods[0].fingerprint:"},
    };
    for (const Case& c : cases) {
        INFO(c.json);
        const Error e = parseError(c.json);
        CHECK(e.code == ErrorCode::ValidationFailed);
        CHECK_THAT(e.message, StartsWith(c.path));
    }
}

TEST_CASE("Field lengths are bounded in bytes of UTF-8", "[sharing][manifest]") {
    Manifest m = test::sampleManifest();

    SECTION("playsetName") {
        m.playsetName = std::string(256, 'n');
        CHECK(validateManifest(m).ok());
        // 128 two-byte characters = 256 bytes is still fine; one more byte is not.
        std::string twoByte;
        for (int i = 0; i < 128; ++i)
            twoByte += "é";
        m.playsetName = twoByte;
        CHECK(validateManifest(m).ok());
        m.playsetName = twoByte + "x";
        CHECK_THAT(validateManifest(m).error().message, StartsWith("playsetName:"));
        m.playsetName.clear();
        CHECK_THAT(validateManifest(m).error().message,
                   StartsWith("playsetName: must not be empty"));
    }
    SECTION("gameVersionHint") {
        m.gameVersionHint = std::string(64, 'v');
        CHECK(validateManifest(m).ok());
        m.gameVersionHint += "v";
        CHECK_THAT(validateManifest(m).error().message, StartsWith("gameVersionHint:"));
    }
    SECTION("nameHint") {
        m.mods[1].nameHint = std::string(256, 'h');
        CHECK(validateManifest(m).ok());
        m.mods[1].nameHint += "h";
        CHECK_THAT(validateManifest(m).error().message, StartsWith("mods[1].nameHint:"));
    }
    SECTION("versionHint") {
        m.mods[0].versionHint = std::string(64, '1');
        CHECK(validateManifest(m).ok());
        m.mods[0].versionHint += "1";
        CHECK_THAT(validateManifest(m).error().message, StartsWith("mods[0].versionHint:"));
    }
    SECTION("local id") {
        m.mods[2].id = std::string(128, 'l');
        CHECK(validateManifest(m).ok());
        m.mods[2].id += "l";
        CHECK_THAT(validateManifest(m).error().message, StartsWith("mods[2].id:"));
    }
}

TEST_CASE("Text fields reject control characters and invalid UTF-8", "[sharing][manifest]") {
    Manifest m = test::sampleManifest();
    for (const std::string& bad :
         {std::string("a\nb"), std::string("tab\there"), std::string("nul\0x", 5),
          std::string("del\x7F"), std::string("nel\xC2\x85"), std::string("bad\xFF")}) {
        Manifest copy = m;
        copy.playsetName = bad;
        CHECK(validateManifest(copy).error().code == ErrorCode::ValidationFailed);
        copy = m;
        copy.mods[0].nameHint = bad;
        CHECK_THAT(validateManifest(copy).error().message, StartsWith("mods[0].nameHint:"));
        copy = m;
        copy.mods[2].id = bad;
        CHECK_THAT(validateManifest(copy).error().message, StartsWith("mods[2].id:"));
    }
    // JSON escapes cannot smuggle control characters in either.
    const Error e =
        parseError(R"({"schemaVersion":1,"game":"stellaris","playsetName":"a\u0000b","mods":[]})");
    CHECK_THAT(e.message, StartsWith("playsetName:"));
}

TEST_CASE("Steam Workshop ids are 1-20 digits within uint64", "[sharing][manifest]") {
    Manifest m = test::sampleManifest();
    m.mods.resize(1);
    for (const char* good :
         {"1", "9", "1234567890", "18446744073709551615", "10000000000000000000"}) {
        m.mods[0].id = good;
        INFO(good);
        CHECK(validateManifest(m).ok());
    }
    for (const char* bad : {"", "0", "0123", "18446744073709551616", "99999999999999999999",
                            "123456789012345678901", "12a4", "-5", "+5", " 5", "1.0", "１２"}) {
        m.mods[0].id = bad;
        INFO(bad);
        const Status s = validateManifest(m);
        REQUIRE_FALSE(s.ok());
        CHECK(s.error().code == ErrorCode::ValidationFailed);
        CHECK_THAT(s.error().message, StartsWith("mods[0].id:"));
    }
}

TEST_CASE("Local ids cannot look like paths", "[sharing][manifest]") {
    Manifest m = test::sampleManifest();
    for (const char* good : {"my mod", "Ünïcødé 日本語", "mod.v2", "...x"}) {
        m.mods[2].id = good;
        INFO(good);
        CHECK(validateManifest(m).ok());
    }
    for (const char* bad : {"", ".", "..", "a/b", "a\\b", "C:mod", "../../etc"}) {
        m.mods[2].id = bad;
        INFO(bad);
        CHECK_THAT(validateManifest(m).error().message, StartsWith("mods[2].id:"));
    }
}

TEST_CASE("Fingerprints must be sha256 with lowercase hex", "[sharing][manifest]") {
    Manifest m = test::sampleManifest();
    for (const std::string& bad :
         {"sha256:" + std::string(63, 'a'), "sha256:" + std::string(65, 'a'),
          "sha256:" + std::string(64, 'A'), "md5:" + std::string(64, 'a'),
          "SHA256:" + std::string(64, 'a'), std::string(64, 'a')}) {
        m.mods[2].fingerprint = bad;
        INFO(bad);
        CHECK_THAT(validateManifest(m).error().message, StartsWith("mods[2].fingerprint:"));
    }
}

TEST_CASE("Games and sources are validated against the catalog", "[sharing][manifest]") {
    Manifest m = test::sampleManifest();

    m.game = "skyrim";
    Status s = validateManifest(m);
    CHECK(s.error().code == ErrorCode::ValidationFailed);
    CHECK_THAT(s.error().message, StartsWith("game:"));

    m.game = "";
    CHECK(validateManifest(m).error().code == ErrorCode::ValidationFailed);

    m.game = "hoi4";
    s = validateManifest(m);
    CHECK(s.error().code == ErrorCode::UnsupportedGame);
    CHECK_THAT(s.error().message, ContainsSubstring("Hearts of Iron IV"));

    m.game = "eu4";
    CHECK(validateManifest(m).ok());
    m.mods[1].source = "paradox-mods";
    s = validateManifest(m);
    CHECK(s.error().code == ErrorCode::ValidationFailed);
    CHECK_THAT(s.error().message, StartsWith("mods[1].source:"));
    m.mods[1].source = "Steam-Workshop";
    CHECK(validateManifest(m).error().code == ErrorCode::ValidationFailed);
}

TEST_CASE("Duplicate identities name both entries", "[sharing][manifest]") {
    Manifest m = test::sampleManifest();
    m.mods.push_back(m.mods[1]);
    m.mods.back().enabled = !m.mods.back().enabled;
    const Status s = validateManifest(m);
    REQUIRE_FALSE(s.ok());
    CHECK(s.error().code == ErrorCode::ValidationFailed);
    CHECK_THAT(s.error().message, StartsWith("mods[4]:"));
    CHECK_THAT(s.error().message, ContainsSubstring("mods[1]"));

    // The same id under a different source is a different mod.
    Manifest other = test::sampleManifest();
    other.mods.push_back({"local", "1234567890", "", true, "", ""});
    CHECK(validateManifest(other).ok());
}

TEST_CASE("Entry count is limited", "[sharing][manifest]") {
    Manifest m = test::sampleManifest();
    m.mods.clear();
    for (std::size_t i = 0; i < kMaxEntries; ++i)
        m.mods.push_back({"steam-workshop", std::to_string(1000000 + i), "", true, "", ""});
    CHECK(validateManifest(m).ok());
    m.mods.push_back({"steam-workshop", "1", "", true, "", ""});
    CHECK(validateManifest(m).error().code == ErrorCode::LimitExceeded);
}

TEST_CASE("Deep nesting is rejected without recursion", "[sharing][manifest]") {
    const std::string deep = std::string(100000, '[') + std::string(100000, ']');
    const Error e = parseError(manifestJson("[]", R"("x":)" + deep + ","));
    CHECK(e.code == ErrorCode::ValidationFailed);
    CHECK_THAT(e.message, ContainsSubstring("nesting"));
}

TEST_CASE("Objects with very many keys are rejected early", "[sharing][manifest]") {
    std::string json = R"({"schemaVersion":1)";
    for (int i = 0; i < 100000; ++i)
        json += ",\"k" + std::to_string(i) + "\":0";
    json += "}";
    const Error e = parseError(json);
    CHECK(e.code == ErrorCode::ValidationFailed);
    CHECK_THAT(e.message, ContainsSubstring("too many keys"));
}

TEST_CASE("A newer schemaVersion takes precedence over the structural limits",
          "[sharing][manifest][limits]") {
    // Protocol section 7.2: a later schema may relax the entry, nesting and
    // key limits, so its codes must get "update the app" rather than a limit
    // error, wherever schemaVersion appears in the document.
    const std::string oversized[] = {
        R"("mods":)" + repeatedArray("{}", kMaxEntries + 1),
        R"("newField":)" + repeatedArray("0", kMaxEntries + 1),
        R"("newField":)" + std::string(18, '[') + std::string(18, ']'), // limit: 16 levels
        R"("newField":{)" + numberedMembers(70) + "}",                  // limit: 64 keys
        numberedMembers(70),                                            // at the root
    };
    for (const std::string& member : oversized) {
        INFO(member.substr(0, 60));
        const Error limit = parseError("{\"schemaVersion\":1," + member + "}");
        CHECK(
            (limit.code == ErrorCode::LimitExceeded || limit.code == ErrorCode::ValidationFailed));

        for (const std::string& json : {
                 "{\"schemaVersion\":2," + member + "}",
                 "{" + member + ",\"schemaVersion\":2}",
                 "{\"game\":\"stellaris\"," + member + ",\"x\":[{\"y\":1}],\"schemaVersion\":3}",
             }) {
            const Error e = parseError(json);
            CHECK(e.code == ErrorCode::UnsupportedVersion);
            CHECK_THAT(e.message, ContainsSubstring("newer version"));
            CHECK_THAT(e.message, ContainsSubstring("Update the app"));
            CHECK(e.detail == "also exceeds this version's limits: " + limit.message);
        }
        CHECK(parseError("{" + member + ",\"schemaVersion\":0}").code ==
              ErrorCode::UnsupportedVersion);

        // Schema 1, or no usable root schemaVersion: the limit error stands.
        for (const std::string& json : {
                 "{" + member + ",\"schemaVersion\":1}",
                 "{" + member + "}",
                 "{" + member + ",\"schemaVersion\":\"2\"}",
                 "{" + member + ",\"schemaVersion\":2.0}",
                 "{" + member + ",\"schemaVersion\":[2]}",
                 "{" + member + ",\"x\":{\"schemaVersion\":2}}",
                 "{" + member + ",\"x\":tru,\"schemaVersion\":2}",
                 "{" + member + ",\"schemaVersion\":1,\"schemaVersion\":2}",
                 "{\"schemaVersion\":1," + member + ",\"schemaVersion\":2}",
             }) {
            const Error e = parseError(json);
            CHECK(e.code == limit.code);
            CHECK(e.message == limit.message);
        }
    }

    // The root key that overflows the key limit may itself be schemaVersion.
    const Error e = parseError("{" + numberedMembers(64) + ",\"schemaVersion\":2}");
    CHECK(e.code == ErrorCode::UnsupportedVersion);
    CHECK_THAT(e.detail, ContainsSubstring("too many keys"));
}

TEST_CASE("serializeManifestJson refuses invalid manifests", "[sharing][manifest]") {
    Manifest m = test::sampleManifest();
    m.playsetName.clear();
    CHECK_FALSE(serializeManifestJson(m).ok());
    m = test::sampleManifest();
    m.schemaVersion = 2;
    CHECK(serializeManifestJson(m).error().code == ErrorCode::UnsupportedVersion);
}
