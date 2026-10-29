#include "core/utf8.hpp"
#include "games/descriptor.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>
#include <utility>
#include <vector>

using namespace pmm;
using namespace pmm::games;
using pmm::test::TempDir;

namespace {

using Pairs = std::vector<std::pair<std::string, std::string>>;

// Exactly what the Paradox launcher writes for a Workshop item (LF, tabs, no
// trailing newline), modelled on a real Stellaris registration.
constexpr std::string_view kLauncherDescriptor = "name=\"Example Outliner\"\n"
                                                 "dependencies={\n"
                                                 "\t\"Example Overhaul\"\n"
                                                 "\t\"Tiny Outliner v2\"\n"
                                                 "}\n"
                                                 "tags={\n"
                                                 "\t\"Fixes\"\n"
                                                 "\t\"Graphics\"\n"
                                                 "}\n"
                                                 "picture=\"thumbnail.png\"\n"
                                                 "version=\"4.4.*\"\n"
                                                 "supported_version=\"v4.4.*\"\n"
                                                 "path=\"C:/Program Files (x86)/Steam/steamapps/"
                                                 "workshop/content/281990/1628912584\"\n"
                                                 "remote_file_id=\"1628912584\"";

ModDescriptor parsed(std::string_view text) {
    auto result = parseDescriptor(text);
    REQUIRE(result.ok());
    return std::move(result).value();
}

} // namespace

TEST_CASE("Descriptor parser reads the launcher format", "[games][descriptor]") {
    const ModDescriptor d = parsed(kLauncherDescriptor);
    CHECK(d.name == "Example Outliner");
    CHECK(d.dependencies == std::vector<std::string>{"Example Overhaul", "Tiny Outliner v2"});
    CHECK(d.tags == std::vector<std::string>{"Fixes", "Graphics"});
    CHECK(d.picture == "thumbnail.png");
    CHECK(d.version == "4.4.*");
    CHECK(d.supportedVersion == "v4.4.*");
    CHECK(d.path == "C:/Program Files (x86)/Steam/steamapps/workshop/content/281990/1628912584");
    CHECK(d.remoteFileId == "1628912584");
    CHECK(d.archive.empty());
    CHECK(d.extra.empty());
}

TEST_CASE("Descriptor writer reproduces the launcher format byte for byte", "[games][descriptor]") {
    auto document = parseDescriptorDocument(kLauncherDescriptor);
    REQUIRE(document.ok());
    CHECK(document->warnings.empty());
    CHECK(writeDescriptor(*document) == kLauncherDescriptor);
}

TEST_CASE("Descriptor parser tolerates BOM, CRLF, comments, bare values and escapes",
          "[games][descriptor]") {
    const std::string text = "\xEF\xBB\xBF# leading comment\r\n"
                             "name = \"Say \\\"hi\\\" \\\\ there\" # trailing comment\r\n"
                             "version=1.2\r\n"
                             "remote_file_id = 123456\r\n"
                             "tags = { \"A b\" Bare # comment inside\r\n \"C\" }\r\n"
                             "replace_path=\"common/a\"\r\n"
                             "user_dir=\"my dir\"\r\n"
                             "replace_path=\"common/b\"\r\n"
                             "extra_list={ \"x \\\"y\\\"\" z }\r\n"
                             "path=\"C:\\Users\\Example\\mod\"\r\n";
    const ModDescriptor d = parsed(text);
    CHECK(d.name == "Say \"hi\" \\ there");
    CHECK(d.version == "1.2");
    CHECK(d.remoteFileId == "123456");
    CHECK(d.tags == std::vector<std::string>{"A b", "Bare", "C"});
    CHECK(d.path == "C:\\Users\\Example\\mod"); // other backslashes stay literal
    // Unknown keys keep file order and duplicates; lists are marked with "{}".
    CHECK(d.extra == Pairs{{"replace_path", "common/a"},
                           {"user_dir", "my dir"},
                           {"replace_path", "common/b"},
                           {"extra_list{}", "\"x \\\"y\\\"\" \"z\""}});
    CHECK(decodeListValue(d.extra[3].second) == std::vector<std::string>{"x \"y\"", "z"});
}

TEST_CASE("Descriptor round trip preserves every key", "[games][descriptor]") {
    const std::string text = "name=\"Ünïcødé 日本語\"\n"
                             "tags={ \"One\" \"Two\" }\n"
                             "replace_path=\"history/provinces\"\n"
                             "replace_path=\"history/countries\"\n"
                             "custom_list={ \"a\\\\\" \"b\" }\n"
                             "picture=\"thumb.png\"\n"
                             "archive=\"mod/legacy.zip\"\n"
                             "path=\"D:\\Mods\\Thing\\\\\"\n"
                             "remote_file_id=\"42\"\n"
                             "supported_version=\"v4.5.*\"\n";
    const ModDescriptor first = parsed(text);
    const std::string written = writeDescriptor(first);
    const ModDescriptor second = parsed(written);
    CHECK(second.name == first.name);
    CHECK(second.tags == first.tags);
    CHECK(second.picture == first.picture);
    CHECK(second.archive == first.archive);
    CHECK(second.path == "D:\\Mods\\Thing\\");
    CHECK(second.path == first.path);
    CHECK(second.remoteFileId == first.remoteFileId);
    CHECK(second.supportedVersion == first.supportedVersion);
    CHECK(second.extra == first.extra);
    CHECK(writeDescriptor(second) == written); // deterministic

    // Canonical order of a ModDescriptor-only document.
    CHECK(written == "tags={\n\t\"One\"\n\t\"Two\"\n}\n"
                     "name=\"Ünïcødé 日本語\"\n"
                     "picture=\"thumb.png\"\n"
                     "supported_version=\"v4.5.*\"\n"
                     "replace_path=\"history/provinces\"\n"
                     "replace_path=\"history/countries\"\n"
                     "custom_list={\n\t\"a\\\\\"\n\t\"b\"\n}\n"
                     "path=\"D:\\Mods\\Thing\\\\\"\n"
                     "archive=\"mod/legacy.zip\"\n"
                     "remote_file_id=\"42\"");
}

TEST_CASE("Descriptor parser reports malformed input with the line", "[games][descriptor]") {
    struct Case {
        std::string text;
        std::string where;
    };
    const auto c = GENERATE(Case{"name=\"ok\"\nversion=\"unterminated\n", "line 2"},
                            Case{"name=\"x\"\ntags={\n\"a\"\n", "line 4"},
                            Case{"name=\"x\"\n}\n", "line 2"}, Case{"= \"x\"", "line 1"},
                            Case{"{ \"a\" }", "line 1"}, Case{"name\n\"x\"", "line 1"},
                            // Reads as name=version, then a stray '='.
                            Case{"name=\nversion=\"1\"", "line 2"}, Case{"\n\nname=", "line 3"});
    CAPTURE(c.text);
    const auto result = parseDescriptor(c.text);
    REQUIRE_FALSE(result.ok());
    CHECK(result.error().code == ErrorCode::ParseError);
    CHECK(result.error().message.find(c.where) != std::string::npos);
}

TEST_CASE("Descriptor parser is lenient where the games are", "[games][descriptor]") {
    auto document = parseDescriptorDocument("name=\"first\"\n"
                                            "tags { \"NoEquals\" }\n"
                                            "nested={ inner=\"x\" }\n"
                                            "deeper={ { \"y\" } }\n"
                                            "name=\"second\"\n"
                                            "tags=\"Scalar tag\"\n"
                                            "empty={}\n");
    REQUIRE(document.ok());
    CHECK(document->warnings.size() == 2);
    CHECK(document->warnings[0].find("line 3") != std::string::npos);
    const ModDescriptor d = toModDescriptor(*document);
    CHECK(d.name == "second"); // last occurrence wins
    CHECK(d.tags == std::vector<std::string>{"NoEquals", "Scalar tag"});
    CHECK(d.extra == Pairs{{"empty{}", ""}});
    CHECK(writeDescriptor(d).find("empty={\n}") != std::string::npos);

    CHECK(parseDescriptor("").ok());
    CHECK(parseDescriptor("\xEF\xBB\xBF   # nothing\n").ok());
}

TEST_CASE("Descriptor text is made valid UTF-8", "[games][descriptor]") {
    const ModDescriptor d = parsed("name=\"Bad \xC3\x28 \xFF name\"\ntags={ \"\xE2\x82\" }\n"
                                   "odd_\xF0\x9F=\"v\xC0\xAF\"\n");
    CHECK(isValidUtf8(d.name));
    CHECK(d.name == "Bad \xEF\xBF\xBD( \xEF\xBF\xBD name");
    // A truncated sequence is one maximal ill-formed subpart: one U+FFFD.
    CHECK(d.tags == std::vector<std::string>{"\xEF\xBF\xBD"});
    REQUIRE(d.extra.size() == 1);
    CHECK(isValidUtf8(d.extra[0].first));
    CHECK(isValidUtf8(d.extra[0].second));

    CHECK(isValidUtf8("Ünïcødé 日本語 \xF0\x9F\x98\x80"));
    CHECK_FALSE(isValidUtf8("\xED\xA0\x80"));     // surrogate
    CHECK_FALSE(isValidUtf8("\xC0\xAF"));         // overlong
    CHECK_FALSE(isValidUtf8("\xF4\x90\x80\x80")); // above U+10FFFF
    CHECK(sanitizeUtf8("ok") == "ok");
}

TEST_CASE("relocateDescriptor moves path and remote_file_id to the end", "[games][descriptor]") {
    auto document = parseDescriptorDocument("version=\"2.6\"\n"
                                            "name=\"Example\"\n"
                                            "path=\"C:/Users/Author/old\"\n"
                                            "archive=\"old.zip\"\n"
                                            "remote_file_id=\"1\"\n"
                                            "picture=\"p.png\"");
    REQUIRE(document.ok());
    const auto folder = relocateDescriptor(*document, {"/new/place", "", "99"});
    CHECK(writeDescriptor(folder) == "version=\"2.6\"\nname=\"Example\"\npicture=\"p.png\"\n"
                                     "path=\"/new/place\"\nremote_file_id=\"99\"");
    const auto zipped = relocateDescriptor(*document, {"", "/new/legacy.zip", "99"});
    CHECK(writeDescriptor(zipped) == "version=\"2.6\"\nname=\"Example\"\npicture=\"p.png\"\n"
                                     "archive=\"/new/legacy.zip\"\nremote_file_id=\"99\"");
}

TEST_CASE("Descriptor files are read with a size limit and named in errors",
          "[games][descriptor]") {
    TempDir temp;
    const fs::path dir = temp.path() / test::u8path("Ünïcødé dir/日本語 folder");
    test::writeText(dir / "good.mod", kLauncherDescriptor);
    test::writeText(dir / "bad.mod", "name=\"x\"\n}");
    test::writeText(dir / "huge.mod", std::string(1024 * 1024 + 1, ' '));

    auto good = readDescriptor(dir / "good.mod");
    REQUIRE(good.ok());
    CHECK(good->remoteFileId == "1628912584");

    auto bad = readDescriptor(dir / "bad.mod");
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().code == ErrorCode::ParseError);
    CHECK(bad.error().message.find("\"bad.mod\"") != std::string::npos);
    CHECK(bad.error().message.find("line 2") != std::string::npos);

    auto huge = readDescriptor(dir / "huge.mod");
    REQUIRE_FALSE(huge.ok());
    CHECK(huge.error().code == ErrorCode::LimitExceeded);

    CHECK_FALSE(readDescriptor(dir / "missing.mod").ok());
}

TEST_CASE("List values in extra survive encode/decode", "[games][descriptor]") {
    const std::vector<std::string> items{"plain",       "with space", "quote\"inside",
                                         "back\\slash", "trailing\\", ""};
    const std::string encoded = encodeListValue(items);
    CHECK(decodeListValue(encoded) == items);
    CHECK(isListExtraKey("replace_path{}"));
    CHECK_FALSE(isListExtraKey("replace_path"));
    CHECK_FALSE(isListExtraKey("{}"));
}
