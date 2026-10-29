#include "core/sha256.hpp"
#include "sharing/codec.hpp"
#include "sharing/share_code.hpp"
#include "sharing_test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <zlib.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::sharing;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;
using test::codeFromCompressed;
using test::codeFromJson;
using test::manifestJson;

namespace {

// Golden codes produced by this encoder (zlib 1.3.1). Never regenerate an
// existing fixture: a mismatch means the protocol output changed.
const char* const kGoldenFixtures[] = {"01-stellaris-basic", "02-eu4-unicode-local",
                                       "03-minimal-empty"};

Error decodeError(std::string_view input) {
    auto r = decodeShareCode(input);
    REQUIRE_FALSE(r.ok());
    return r.error();
}

std::string validCode() {
    auto code = encodeShareCode(test::sampleManifest());
    REQUIRE(code.ok());
    return *code;
}

struct CodeParts {
    std::string payload;
    std::string digest;
};

CodeParts split(const std::string& code) {
    const std::size_t first = code.find('.');
    const std::size_t second = code.find('.', first + 1);
    return {code.substr(first + 1, second - first - 1), code.substr(second + 1)};
}

// Streams `prefix` followed by `count` filler bytes through deflate without
// materialising the whole input.
std::string deflateRepeated(std::string_view prefix, char filler, std::size_t count) {
    z_stream s{};
    REQUIRE(deflateInit(&s, 1) == Z_OK);
    std::string out;
    std::vector<char> buffer(64 * 1024);
    const std::string chunk(1024 * 1024, filler);
    const auto pump = [&](const char* data, std::size_t size, int flush) {
        s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));
        s.avail_in = static_cast<uInt>(size);
        do {
            s.next_out = reinterpret_cast<Bytef*>(buffer.data());
            s.avail_out = static_cast<uInt>(buffer.size());
            const int ret = deflate(&s, flush);
            REQUIRE(ret != Z_STREAM_ERROR);
            out.append(buffer.data(), buffer.size() - s.avail_out);
        } while (s.avail_out == 0);
    };
    pump(prefix.data(), prefix.size(), Z_NO_FLUSH);
    std::size_t remaining = count;
    while (remaining > chunk.size()) {
        pump(chunk.data(), chunk.size(), Z_NO_FLUSH);
        remaining -= chunk.size();
    }
    pump(chunk.data(), remaining, Z_FINISH);
    deflateEnd(&s);
    return out;
}

class TempDir {
public:
    TempDir() {
        std::random_device rd;
        m_root = std::filesystem::temp_directory_path() /
                 ("pmm sharing test " + std::to_string(rd()) + "-" +
                  std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(m_root);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const std::filesystem::path& path() const { return m_root; }

private:
    std::filesystem::path m_root;
};

} // namespace

TEST_CASE("Round trip preserves game, order, ids and enabled flags exactly", "[sharing][code]") {
    const Manifest original = test::sampleManifest();
    auto code = encodeShareCode(original);
    REQUIRE(code.ok());
    CHECK_THAT(*code, StartsWith("PMM1."));
    auto decoded = decodeShareCode(*code);
    REQUIRE(decoded.ok());
    CHECK(*decoded == original);
    CHECK(decoded->mods[3].id == "18446744073709551615");
    CHECK(decoded->mods[3].nameHint == "Ünïcødé 日本語 🚀");
    CHECK_FALSE(decoded->mods[1].enabled);
    CHECK_FALSE(decoded->mods[3].enabled);
}

TEST_CASE("Playset export includes disabled entries and imports in the same order",
          "[sharing][code]") {
    Playset playset;
    playset.id = 17;
    playset.game = "eu4";
    playset.name = "Campaign";
    playset.createdAt = 1;
    playset.updatedAt = 2;
    playset.entries = {
        {{"eu4", "steam-workshop", "300"}, false, "Third", "", ""},
        {{"eu4", "local", "my mod"}, true, "Local", "1.0", "sha256:" + std::string(64, 'b')},
        {{"eu4", "steam-workshop", "100"}, true, "First", "", ""},
    };
    auto manifest = manifestFromPlayset(playset, "1.37.2");
    REQUIRE(manifest.ok());
    CHECK(manifest->gameVersionHint == "1.37.2");
    auto code = encodeShareCode(*manifest);
    REQUIRE(code.ok());
    // No ids, timestamps or other volatile data leak into the code.
    auto json = zlibInflate(*base64UrlDecode(split(*code).payload), kMaxDecompressedBytes);
    REQUIRE(json.ok());
    CHECK_THAT(*json, !ContainsSubstring("17"));
    CHECK_THAT(*json, !ContainsSubstring("createdAt"));

    auto decoded = decodeShareCode(*code);
    REQUIRE(decoded.ok());
    CHECK(playsetEntriesFromManifest(*decoded) == playset.entries);
}

TEST_CASE("manifestFromPlayset refuses entries of another game", "[sharing][code]") {
    Playset playset;
    playset.game = "eu4";
    playset.name = "Mixed";
    playset.entries = {{{"stellaris", "steam-workshop", "1"}, true, "", "", ""}};
    auto manifest = manifestFromPlayset(playset, "");
    REQUIRE_FALSE(manifest.ok());
    CHECK(manifest.error().code == ErrorCode::WrongGame);
}

TEST_CASE("Encoding is deterministic", "[sharing][code]") {
    const std::string a = validCode();
    const std::string b = validCode();
    CHECK(a == b);

    Manifest changed = test::sampleManifest();
    changed.mods[0].enabled = false;
    auto c = encodeShareCode(changed);
    REQUIRE(c.ok());
    CHECK(*c != a);

    Manifest reordered = test::sampleManifest();
    std::swap(reordered.mods[0], reordered.mods[1]);
    auto d = encodeShareCode(reordered);
    REQUIRE(d.ok());
    CHECK(*d != a);
}

TEST_CASE("Golden v1 fixtures decode and re-encode byte-exactly", "[sharing][code][fixtures]") {
    for (const char* name : kGoldenFixtures) {
        INFO(name);
        const std::string code = test::readFixture(std::string("v1/") + name + ".code");
        const std::string json = test::readFixture(std::string("v1/") + name + ".json");

        auto decoded = decodeShareCode(code);
        REQUIRE(decoded.ok());
        auto canonical = serializeManifestJson(*decoded);
        REQUIRE(canonical.ok());
        CHECK(*canonical == json);

        // The payload is exactly the canonical JSON.
        auto inflated = zlibInflate(*base64UrlDecode(split(code).payload), kMaxDecompressedBytes);
        REQUIRE(inflated.ok());
        CHECK(*inflated == json);

        auto reencoded = encodeShareCode(*decoded);
        REQUIRE(reencoded.ok());
        CHECK(*reencoded == code);
    }
}

TEST_CASE("Golden fixture content is what it claims", "[sharing][code][fixtures]") {
    auto basic = decodeShareCode(test::readFixture("v1/01-stellaris-basic.code"));
    REQUIRE(basic.ok());
    CHECK(basic->game == "stellaris");
    REQUIRE(basic->mods.size() == 3);
    CHECK(basic->mods[1].id == "1234567891");
    CHECK_FALSE(basic->mods[1].enabled);

    auto unicode = decodeShareCode(test::readFixture("v1/02-eu4-unicode-local.code"));
    REQUIRE(unicode.ok());
    CHECK(unicode->playsetName == "Ünïcødé 日本語 \"quoted\" \\ playset");
    CHECK(unicode->mods[0].id == "18446744073709551615");
    CHECK(unicode->mods[1].source == "local");
    // Names are hints: URL- or command-like text is carried verbatim, never acted on.
    CHECK(unicode->mods[3].nameHint == "https://example.invalid/x.exe $(echo not-a-command)");

    auto empty = decodeShareCode(test::readFixture("v1/03-minimal-empty.code"));
    REQUIRE(empty.ok());
    CHECK(empty->mods.empty());
}

TEST_CASE("Codes from other encoders with non-canonical JSON decode", "[sharing][code][fixtures]") {
    // Produced by a different zlib implementation from indented, reordered JSON
    // with \u escapes and an empty optional field.
    const std::string code = test::readFixture("v1/04-foreign-noncanonical.code");
    const std::string json = test::readFixture("v1/04-foreign-noncanonical.json");
    auto decoded = decodeShareCode(code);
    REQUIRE(decoded.ok());
    auto canonical = serializeManifestJson(*decoded);
    REQUIRE(canonical.ok());
    CHECK(*canonical == json);
    auto reencoded = encodeShareCode(*decoded);
    REQUIRE(reencoded.ok());
    CHECK(*reencoded != code); // canonical output differs, content is the same
    auto again = decodeShareCode(*reencoded);
    REQUIRE(again.ok());
    CHECK(*again == *decoded);
}

TEST_CASE("Surrounding ASCII whitespace is accepted, other text is not", "[sharing][code]") {
    const std::string code = validCode();
    CHECK(decodeShareCode(" \t\r\n" + code + "\r\n \t").ok());
    CHECK(decodeShareCode(code + "\n").ok());

    CHECK(decodeError("\xC2\xA0" + code).code == ErrorCode::ParseError);     // no-break space
    CHECK(decodeError("\xEF\xBB\xBF" + code).code == ErrorCode::ParseError); // byte order mark
    CHECK(decodeError("\v" + code).code == ErrorCode::ParseError);
    CHECK(decodeError("Code: " + code).code == ErrorCode::ParseError);
    const CodeParts parts = split(code);
    CHECK(decodeError("PMM1." + parts.payload.substr(0, 8) + " " + parts.payload.substr(8) + "." +
                      parts.digest)
              .code == ErrorCode::ParseError);
}

TEST_CASE("Input size is limited before anything else", "[sharing][code][limits]") {
    const std::string code = validCode();
    std::string padded = code + std::string(kMaxEncodedBytes - code.size(), ' ');
    CHECK(decodeShareCode(padded).ok());
    padded += ' ';
    const Error e = decodeError(padded);
    CHECK(e.code == ErrorCode::LimitExceeded);
    CHECK(decodeError(std::string(kMaxEncodedBytes + 1, 'A')).code == ErrorCode::LimitExceeded);
}

TEST_CASE("Envelope prefix and version are checked", "[sharing][code]") {
    for (const char* text : {"", "   ", "hello", "pmm1.abc.def", "PMM.abc.def", "PMM1", "PMMX.a.b",
                             "PMM01.abc.def", "PM1.abc.def", "https://example.invalid/code"}) {
        INFO(text);
        const Error e = decodeError(text);
        CHECK(e.code == ErrorCode::ParseError);
        CHECK_THAT(e.message, ContainsSubstring("not a Paradox Mod Manager share code"));
    }
    for (const char* text : {"PMM2.abc.def", "PMM0.x", "PMM17.anything", "PMM2."}) {
        INFO(text);
        const Error e = decodeError(text);
        CHECK(e.code == ErrorCode::UnsupportedVersion);
        CHECK_THAT(e.message, ContainsSubstring("newer version"));
        CHECK_THAT(e.message, ContainsSubstring("update the app"));
    }
    CHECK_THAT(decodeError("PMM2.abc.def").message, ContainsSubstring("protocol version 2"));
}

TEST_CASE("Envelope structure is strict", "[sharing][code]") {
    const std::string code = validCode();
    const CodeParts parts = split(code);

    SECTION("wrong part count") {
        CHECK(decodeError("PMM1." + parts.payload).code == ErrorCode::ParseError);
        CHECK(decodeError("PMM1." + parts.payload + "." + parts.digest + ".x").code ==
              ErrorCode::ParseError);
        CHECK(decodeError("PMM1." + parts.payload + ".." + parts.digest).code ==
              ErrorCode::ParseError);
    }
    SECTION("empty payload") {
        CHECK(decodeError("PMM1.." + parts.digest).code == ErrorCode::ParseError);
    }
    SECTION("padding") {
        std::string payload = parts.payload;
        while (payload.size() % 4 != 0)
            payload += '=';
        if (payload == parts.payload)
            payload += "====";
        CHECK(decodeError("PMM1." + payload + "." + parts.digest).code == ErrorCode::ParseError);
    }
    SECTION("invalid Base64URL characters") {
        for (const char bad : {'+', '/', '!', '~', '*'}) {
            std::string payload = parts.payload;
            payload[payload.size() / 2] = bad;
            CHECK(decodeError("PMM1." + payload + "." + parts.digest).code ==
                  ErrorCode::ParseError);
        }
    }
    SECTION("non-canonical final character") {
        auto compressed = base64UrlDecode(parts.payload);
        REQUIRE(compressed.ok());
        std::string bytes = *compressed;
        while (bytes.size() % 3 == 0)
            bytes += 'x'; // need unused bits in the last character
        std::string payload = base64UrlEncode(bytes);
        // Canonical final characters have zero unused low bits; set the lowest.
        const std::string_view alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        payload.back() = alphabet[alphabet.find(payload.back()) | 1];
        const Error e = decodeError("PMM1." + payload + "." + sha256Hex(bytes));
        CHECK(e.code == ErrorCode::ParseError);
        CHECK_THAT(e.message, ContainsSubstring("canonical"));
    }
}

TEST_CASE("Truncation and corruption fail the integrity check", "[sharing][code]") {
    const std::string code = validCode();
    const CodeParts parts = split(code);

    SECTION("truncated digest") {
        for (std::size_t keep : {0u, 1u, 32u, 63u}) {
            const Error e =
                decodeError("PMM1." + parts.payload + "." + parts.digest.substr(0, keep));
            CHECK(e.code == ErrorCode::IntegrityCheckFailed);
            CHECK_THAT(e.message, ContainsSubstring("truncated or damaged"));
        }
        CHECK(decodeError(code + "0").code == ErrorCode::IntegrityCheckFailed);
    }
    SECTION("uppercase digest") {
        std::string upper = parts.digest;
        for (char& c : upper)
            c = (c >= 'a' && c <= 'f') ? static_cast<char>(c - 'a' + 'A') : c;
        REQUIRE(upper != parts.digest);
        CHECK(decodeError("PMM1." + parts.payload + "." + upper).code ==
              ErrorCode::IntegrityCheckFailed);
    }
    SECTION("digest mismatch") {
        std::string digest = parts.digest;
        digest[10] = digest[10] == '0' ? '1' : '0';
        const Error e = decodeError("PMM1." + parts.payload + "." + digest);
        CHECK(e.code == ErrorCode::IntegrityCheckFailed);
        CHECK_THAT(e.message, ContainsSubstring("does not match"));
    }
    SECTION("truncated payload") {
        // Keep whole Base64 quads so the payload itself is still well-formed.
        const std::size_t keep = (parts.payload.size() / 2) / 4 * 4;
        CHECK(decodeError("PMM1." + parts.payload.substr(0, keep) + "." + parts.digest).code ==
              ErrorCode::IntegrityCheckFailed);
    }
    SECTION("every truncation of the whole code fails safely") {
        for (std::size_t len = 0; len < code.size(); ++len)
            CHECK_FALSE(decodeShareCode(code.substr(0, len)).ok());
    }
    SECTION("changed payload character") {
        std::string payload = parts.payload;
        payload[5] = payload[5] == 'A' ? 'B' : 'A';
        CHECK(decodeError("PMM1." + payload + "." + parts.digest).code ==
              ErrorCode::IntegrityCheckFailed);
    }
}

TEST_CASE("Compressed data must be exactly one zlib stream", "[sharing][code]") {
    const std::string json = manifestJson();
    auto compressed = zlibCompress(json);
    REQUIRE(compressed.ok());
    CHECK(decodeShareCode(codeFromCompressed(*compressed)).ok());

    CHECK(decodeError(codeFromCompressed(*compressed + "garbage")).code == ErrorCode::ParseError);
    CHECK(decodeError(codeFromCompressed(compressed->substr(0, compressed->size() - 3))).code ==
          ErrorCode::ParseError);
    CHECK(decodeError(codeFromCompressed(json)).code == ErrorCode::ParseError); // not compressed
    CHECK(decodeError(codeFromCompressed(std::string(40, '\0'))).code == ErrorCode::ParseError);
}

TEST_CASE("Decoded JSON is validated strictly", "[sharing][code]") {
    const auto decodeJson = [](const std::string& json) { return decodeError(codeFromJson(json)); };

    CHECK(decodeJson(std::string(R"({"schemaVersion":1,"game":"stellaris","playsetName":")") +
                     "\xFF" + R"(","mods":[]})")
              .code == ErrorCode::ParseError);
    CHECK(decodeJson(R"({"schemaVersion":1,"game":"stellaris",)").code == ErrorCode::ParseError);
    CHECK(
        decodeJson(R"({"schemaVersion":2,"game":"stellaris","playsetName":"P","mods":[]})").code ==
        ErrorCode::UnsupportedVersion);
    CHECK_THAT(
        decodeJson(
            R"({"schemaVersion":1,"game":"stellaris","playsetName":"P","playsetName":"Q","mods":[]})")
            .message,
        ContainsSubstring("duplicate key"));
    CHECK_THAT(decodeJson(manifestJson("[]", R"("extra":1,)")).message,
               ContainsSubstring("unknown field"));
    CHECK_THAT(decodeJson(manifestJson(R"([{"source":"steam-workshop","id":"1","enabled":"yes"}])"))
                   .message,
               StartsWith("mods[0].enabled:"));
    CHECK_THAT(decodeJson(manifestJson(R"([{"source":"steam-workshop","id":"01","enabled":true}])"))
                   .message,
               StartsWith("mods[0].id:"));
    CHECK_THAT(
        decodeJson(manifestJson("[]", R"("gameVersionHint":")" + std::string(65, 'v') + R"(",)"))
            .message,
        StartsWith("gameVersionHint:"));

    const Error dup =
        decodeJson(manifestJson(R"([{"source":"steam-workshop","id":"5","enabled":true},)"
                                R"({"source":"local","id":"5","enabled":true},)"
                                R"({"source":"steam-workshop","id":"5","enabled":false}])"));
    CHECK(dup.code == ErrorCode::ValidationFailed);
    CHECK_THAT(dup.message, StartsWith("mods[2]:"));
    CHECK_THAT(dup.message, ContainsSubstring("mods[0]"));

    const Error source =
        decodeJson(manifestJson(R"([{"source":"paradox-mods","id":"5","enabled":true}])"));
    CHECK(source.code == ErrorCode::ValidationFailed);
    CHECK_THAT(source.message, StartsWith("mods[0].source:"));

    const Error unknownGame =
        decodeJson(R"({"schemaVersion":1,"game":"skyrim","playsetName":"P","mods":[]})");
    CHECK(unknownGame.code == ErrorCode::ValidationFailed);

    const Error hoi4 =
        decodeJson(R"({"schemaVersion":1,"game":"hoi4","playsetName":"P","mods":[]})");
    CHECK(hoi4.code == ErrorCode::UnsupportedGame);
    CHECK_THAT(hoi4.message, ContainsSubstring("Hearts of Iron IV"));
}

TEST_CASE("More than 10000 entries are rejected", "[sharing][code][limits]") {
    std::string mods = "[";
    for (std::size_t i = 0; i <= kMaxEntries; ++i) {
        if (i > 0)
            mods += ',';
        mods += R"({"source":"steam-workshop","id":")" + std::to_string(100000 + i) +
                R"(","enabled":true})";
    }
    mods += "]";
    const Error e = decodeError(codeFromJson(manifestJson(mods)));
    CHECK(e.code == ErrorCode::LimitExceeded);
    CHECK_THAT(e.message, ContainsSubstring("mods"));
}

TEST_CASE("A 10000-entry playset encodes and decodes within limits", "[sharing][code][limits]") {
    Manifest m;
    m.game = "stellaris";
    m.playsetName = "Huge synthetic playset";
    m.gameVersionHint = "v4.1.5";
    for (std::size_t i = 0; i < kMaxEntries; ++i) {
        ManifestEntry entry;
        entry.source = i % 10 == 9 ? "local" : "steam-workshop";
        entry.id = i % 10 == 9 ? "local mod " + std::to_string(i)
                               : std::to_string(2000000000ULL + i * 7919);
        entry.nameHint =
            "Synthetic mod number " + std::to_string(i) + " with a realistic display name";
        entry.enabled = i % 3 != 0;
        entry.versionHint = i % 2 == 0 ? "4.1.*" : "";
        m.mods.push_back(std::move(entry));
    }
    auto code = encodeShareCode(m);
    REQUIRE(code.ok());
    CHECK(code->size() < kMaxEncodedBytes);
    auto decoded = decodeShareCode(*code);
    REQUIRE(decoded.ok());
    CHECK(*decoded == m);
}

TEST_CASE("The encoder refuses codes the decoder would reject", "[sharing][code][limits]") {
    // Incompressible maximum-length fields: valid JSON below 8 MiB, but the
    // code would exceed 2 MiB.
    std::mt19937 rng(12345);
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const auto randomText = [&](std::size_t n) {
        std::string s;
        for (std::size_t i = 0; i < n; ++i)
            s += alphabet[rng() % alphabet.size()];
        return s;
    };
    Manifest m;
    m.game = "eu4";
    m.playsetName = "Too big";
    for (std::size_t i = 0; i < kMaxEntries; ++i)
        m.mods.push_back({"local", randomText(kMaxLocalIdBytes), randomText(kMaxNameHintBytes),
                          true, randomText(kMaxVersionHintBytes), ""});
    auto code = encodeShareCode(m);
    REQUIRE_FALSE(code.ok());
    CHECK(code.error().code == ErrorCode::LimitExceeded);
}

TEST_CASE("encodeShareCode validates before encoding", "[sharing][code]") {
    Manifest m = test::sampleManifest();
    m.mods.push_back(m.mods[0]);
    CHECK(encodeShareCode(m).error().code == ErrorCode::ValidationFailed);
    m = test::sampleManifest();
    m.game = "ck3";
    CHECK(encodeShareCode(m).error().code == ErrorCode::UnsupportedGame);
}

TEST_CASE("Decompressed size is capped at exactly 8 MiB", "[sharing][code][limits]") {
    const std::string json = manifestJson();
    // JSON allows trailing whitespace, so pad the document to the limit.
    const std::string atLimit = deflateRepeated(json, ' ', kMaxDecompressedBytes - json.size());
    CHECK(decodeShareCode(codeFromCompressed(atLimit)).ok());
    const std::string overLimit =
        deflateRepeated(json, ' ', kMaxDecompressedBytes - json.size() + 1);
    CHECK(decodeError(codeFromCompressed(overLimit)).code == ErrorCode::LimitExceeded);
}

TEST_CASE("A decompression bomb is rejected quickly", "[sharing][code][limits]") {
    // ~64 KiB of compressed data that would inflate to 64 MiB.
    const std::string bomb = deflateRepeated(
        R"({"schemaVersion":1,"game":"stellaris","playsetName":")", 'a', 64ull * 1024 * 1024);
    const std::string code = codeFromCompressed(bomb);
    REQUIRE(code.size() < kMaxEncodedBytes);

    const auto start = std::chrono::steady_clock::now();
    const Error e = decodeError(code);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(e.code == ErrorCode::LimitExceeded);
    CHECK(elapsed < std::chrono::seconds(5));
}

TEST_CASE("Share codes survive a file round trip in a Unicode path", "[sharing][code]") {
    TempDir temp;
    const std::filesystem::path dir = temp.path() / u8"Ünïcødé dir" / u8"日本語";
    std::filesystem::create_directories(dir);
    const std::filesystem::path file = dir / u8"playset code.txt";
    const std::string code = validCode();
    {
        std::ofstream out(file, std::ios::binary);
        out << code << "\r\n";
    }
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto decoded = decodeShareCode(text);
    REQUIRE(decoded.ok());
    CHECK(*decoded == test::sampleManifest());
}

// Prints codes for the golden fixtures from their canonical JSON. Hidden: only
// for adding NEW fixtures (`pmm_sharing_tests "[.generate]"`); existing ones
// must never be regenerated.
TEST_CASE("Print golden fixture codes", "[.generate]") {
    for (const char* name : kGoldenFixtures) {
        auto manifest = parseManifestJson(test::readFixture(std::string("v1/") + name + ".json"));
        REQUIRE(manifest.ok());
        auto code = encodeShareCode(*manifest);
        REQUIRE(code.ok());
        std::cout << name << ' ' << *code << '\n';
    }
}
