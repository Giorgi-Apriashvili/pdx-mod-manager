#include "core/utf8.hpp"
#include "games/mod_metadata.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

using namespace pmm;
using namespace pmm::games;
using pmm::test::TempDir;

namespace {

using Pairs = std::vector<std::pair<std::string, std::string>>;

// The layout of a Victoria 3 Workshop item on the development machine (tabs,
// spaces before the colons, an object-form dependency, no trailing newline),
// with made-up values.
constexpr std::string_view kWorkshopMetadata =
    "{\n"
    "\t\"name\" : \"Example Ledger\",\n"
    "\t\"id\" : \"5000000001\",\n"
    "\t\"version\" : \"1.1.0\",\n"
    "\t\"supported_game_version\" : \"1.15.*\",\n"
    "\t\"short_description\" : \"Tracks the treasury.\",\n"
    "\t\"tags\" : [ \"UI\", \"Quality of Life\", \"1.15\" ],\n"
    "\t\"relationships\" : [ { \"rel_type\": \"dependency\", \"id\": \"com.example.core\", "
    "\"display_name\": \"Example Core\", \"resource_type\": \"mod\", \"version\": \"1.*\" } ],\n"
    "\t\"game_custom_data\" : { \"multiplayer_synchronized\" : true }\n"
    "}";

ModMetadata parsed(std::string_view text) {
    auto result = parseModMetadata(text);
    REQUIRE(result.ok());
    return std::move(result).value();
}

} // namespace

TEST_CASE("metadata.json is read as Victoria 3's launcher and Workshop write it",
          "[games][metadata]") {
    const ModMetadata m = parsed(kWorkshopMetadata);
    const ModDescriptor& d = m.descriptor;
    CHECK(d.name == "Example Ledger");
    CHECK(d.version == "1.1.0");
    CHECK(d.supportedVersion == "1.15.*");
    CHECK(d.tags == std::vector<std::string>{"UI", "Quality of Life", "1.15"});
    CHECK(d.dependencies == std::vector<std::string>{"Example Core"});
    CHECK(d.picture.empty());
    CHECK(d.path.empty());
    CHECK(d.archive.empty());
    CHECK(d.remoteFileId.empty()); // the metadata id is the author's, not Steam's
    CHECK(d.extra == Pairs{{"id", "5000000001"}, {"short_description", "Tracks the treasury."}});
    CHECK(m.hasGameCustomData);

    // The same document with CRLF line ends, a BOM and a trailing newline.
    const std::string crlf =
        "\xEF\xBB\xBF" + test::replaceAll(std::string(kWorkshopMetadata), "\n", "\r\n") + "\r\n";
    const ModMetadata again = parsed(crlf);
    CHECK(again.descriptor.name == d.name);
    CHECK(again.descriptor.extra == d.extra);
    CHECK(again.descriptor.dependencies == d.dependencies);
    CHECK(again.hasGameCustomData);
}

TEST_CASE("metadata.json written by the launcher for a local mod", "[games][metadata]") {
    // Two-space indent, LF, a trailing newline; empty id, an unknown key kept.
    const ModMetadata m = parsed("{\n"
                                 "  \"name\": \"Local Almanac\",\n"
                                 "  \"id\": \"\",\n"
                                 "  \"version\": \"\",\n"
                                 "  \"supported_game_version\": \"\",\n"
                                 "  \"short_description\": \"\",\n"
                                 "  \"picture\": \"thumbnail.png\",\n"
                                 "  \"tags\": [],\n"
                                 "  \"relationships\": [],\n"
                                 "  \"game_id\": \"victoria3\",\n"
                                 "  \"multiplayer\": false,\n"
                                 "  \"revision\": 3,\n"
                                 "  \"game_custom_data\": {\n"
                                 "    \"multiplayer_synchronized\": true,\n"
                                 "    \"replace_paths\": [\"common/history\"]\n"
                                 "  }\n"
                                 "}\n");
    const ModDescriptor& d = m.descriptor;
    CHECK(d.name == "Local Almanac");
    CHECK(d.version.empty());
    CHECK(d.supportedVersion.empty());
    CHECK(d.picture == "thumbnail.png"); // kept as written
    CHECK(d.tags.empty());
    CHECK(d.dependencies.empty());
    CHECK(d.extra == Pairs{{"id", ""},
                           {"short_description", ""},
                           {"game_id", "victoria3"},
                           {"multiplayer", "false"},
                           {"revision", "3"}});
    CHECK(m.hasGameCustomData);
}

TEST_CASE("metadata.json relationships come in two forms", "[games][metadata]") {
    // Objects: rel_type "dependency" counts, by display_name, else id.
    const ModMetadata objects = parsed(
        R"({"name": "x", "relationships": [
              {"rel_type": "dependency", "id": "com.example.a", "display_name": "Example A",
               "resource_type": "mod", "version": "1.*"},
              {"rel_type": "dependency", "id": "com.example.b", "display_name": ""},
              {"rel_type": "load_after", "id": "com.example.c", "display_name": "Example C"},
              {"rel_type": "incompatible_with", "display_name": "Example D"},
              {"rel_type": "dependency", "display_name": "Some DLC", "resource_type": "dlc"},
              {"rel_type": "dependency"},
              "Example E", 7, null]})");
    CHECK(objects.descriptor.dependencies ==
          std::vector<std::string>{"Example A", "com.example.b", "Example E"});

    // The older form: a plain list of names.
    const ModMetadata names =
        parsed(R"({"name": "x", "relationships": ["Example Core", "", "Example Maps"]})");
    CHECK(names.descriptor.dependencies ==
          std::vector<std::string>{"Example Core", "Example Maps"});

    // Anything else is ignored.
    CHECK(parsed(R"({"relationships": {"rel_type": "dependency", "id": "a"}})")
              .descriptor.dependencies.empty());
}

TEST_CASE("metadata.json without game_custom_data is read and flagged", "[games][metadata]") {
    CHECK_FALSE(parsed(R"({"name": "x", "id": ""})").hasGameCustomData);
    CHECK_FALSE(parsed(R"({"name": "x", "game_custom_data": null})").hasGameCustomData);
    CHECK_FALSE(parsed(R"({"name": "x", "game_custom_data": []})").hasGameCustomData);
    CHECK(parsed(R"({"name": "x", "game_custom_data": {}})").hasGameCustomData);
    // Values of the wrong type are left out, not errors.
    const ModMetadata odd = parsed(R"({"name": 5, "tags": "UI", "version": ["1"], "id": {}})");
    CHECK(odd.descriptor.name.empty());
    CHECK(odd.descriptor.tags.empty());
    CHECK(odd.descriptor.version.empty());
    CHECK(odd.descriptor.extra.empty());
}

TEST_CASE("Invalid metadata.json is a parse error that names the file", "[games][metadata]") {
    // A trailing comma: strict JSON, as the game reads it.
    auto comma = parseModMetadata("{\n  \"name\": \"x\",\n}");
    REQUIRE_FALSE(comma.ok());
    CHECK(comma.error().code == ErrorCode::ParseError);
    CHECK(comma.error().message.rfind("The mod metadata is not valid JSON (line 3, column 1: ",
                                      0) == 0);
    // Comments are not JSON either.
    CHECK_FALSE(parseModMetadata("{\"name\": \"x\" // comment\n}").ok());
    CHECK_FALSE(parseModMetadata("").ok());
    auto array = parseModMetadata("[]");
    REQUIRE_FALSE(array.ok());
    CHECK(array.error().message == "The mod metadata is not a JSON object.");

    TempDir dir("pmm metadata");
    const fs::path file = modMetadataPath(dir.path() / "Example Mod");
    CHECK(file == dir.path() / "Example Mod" / ".metadata" / "metadata.json");
    test::writeText(file, "{\"name\": \"x\",}");
    auto read = readModMetadata(file);
    REQUIRE_FALSE(read.ok());
    CHECK(read.error().code == ErrorCode::ParseError);
    CHECK(read.error().message.rfind(
              "The mod metadata \".metadata/metadata.json\" is not valid JSON (line 1, column 14: ",
              0) == 0);
    CHECK(read.error().detail.find(platform::pathToUtf8(file)) == 0);

    // More than 1 MiB.
    test::writeText(file, "{\"name\": \"" + std::string(1024 * 1024, 'x') + "\"}");
    auto large = readModMetadata(file);
    REQUIRE_FALSE(large.ok());
    CHECK(large.error().code == ErrorCode::LimitExceeded);

    auto missing = readModMetadata(dir.path() / "absent" / "metadata.json");
    CHECK_FALSE(missing.ok());
}

TEST_CASE("metadata.json with ill-formed UTF-8 reads as U+FFFD", "[games][metadata]") {
    const ModMetadata m = parsed("{\"name\": \"Caf\xE9 \xF0\x9F\x8E\xA9\", \"id\": \"\xC3\"}");
    CHECK(m.descriptor.name == "Caf\xEF\xBF\xBD \xF0\x9F\x8E\xA9");
    CHECK(m.descriptor.extra == Pairs{{"id", "\xEF\xBF\xBD"}});
    CHECK(isValidUtf8(m.descriptor.name));
}
