#include "core/result.hpp"
#include "sharing/codec.hpp"
#include "sharing/utf8.hpp"

#include <catch2/catch_test_macros.hpp>
#include <zlib.h>

#include <string>
#include <vector>

using namespace pmm;
using namespace pmm::sharing;

TEST_CASE("Base64URL encodes RFC 4648 vectors without padding", "[sharing][codec]") {
    CHECK(base64UrlEncode("").empty());
    CHECK(base64UrlEncode("f") == "Zg");
    CHECK(base64UrlEncode("fo") == "Zm8");
    CHECK(base64UrlEncode("foo") == "Zm9v");
    CHECK(base64UrlEncode("foob") == "Zm9vYg");
    CHECK(base64UrlEncode("fooba") == "Zm9vYmE");
    CHECK(base64UrlEncode("foobar") == "Zm9vYmFy");
    // URL-safe alphabet: 0xfb 0xff is "+/8" in standard Base64.
    CHECK(base64UrlEncode(std::string("\xfb\xff", 2)) == "-_8");
}

TEST_CASE("Base64URL round-trips every byte value at every length remainder", "[sharing][codec]") {
    std::string bytes;
    for (int i = 0; i < 256; ++i)
        bytes += static_cast<char>(i);
    for (std::size_t len = 0; len <= bytes.size(); len += 1) {
        const std::string input = bytes.substr(0, len);
        auto decoded = base64UrlDecode(base64UrlEncode(input));
        REQUIRE(decoded.ok());
        REQUIRE(*decoded == input);
    }
}

TEST_CASE("Base64URL decoding is strict and canonical", "[sharing][codec]") {
    const auto rejects = [](std::string_view text) {
        auto r = base64UrlDecode(text);
        return !r.ok() && r.error().code == ErrorCode::ParseError;
    };
    CHECK(rejects("Zg=="));    // padding
    CHECK(rejects("Zg="));     // padding
    CHECK(rejects("Z"));       // length % 4 == 1
    CHECK(rejects("Zm9vY"));   // length % 4 == 1
    CHECK(rejects("Zh"));      // non-zero unused bits ('h' = 0b100001)
    CHECK(rejects("Zm9"));     // non-zero unused bits ('9' = 0b111101)
    CHECK(rejects("Zm9v Yg")); // whitespace
    CHECK(rejects("Zm9v\nYg"));
    CHECK(rejects("+/8A")); // standard alphabet
    CHECK(rejects("Zm9v!"));
    CHECK(rejects("Zm9\xC3\xA9"));
    auto ok = base64UrlDecode("Zm8");
    REQUIRE(ok.ok());
    CHECK(*ok == "fo");
}

TEST_CASE("zlib round-trip produces an RFC 1950 stream", "[sharing][codec]") {
    const std::string data(10000, 'x');
    auto compressed = zlibCompress(data);
    REQUIRE(compressed.ok());
    REQUIRE(compressed->size() >= 2);
    // CMF 0x78 = deflate with a 32 KiB window; FLEVEL bits of 0xDA = maximum compression.
    CHECK(static_cast<unsigned char>((*compressed)[0]) == 0x78);
    CHECK(static_cast<unsigned char>((*compressed)[1]) == 0xDA);
    auto inflated = zlibInflate(*compressed, data.size());
    REQUIRE(inflated.ok());
    CHECK(*inflated == data);
}

TEST_CASE("zlib inflation enforces the output cap exactly", "[sharing][codec]") {
    const std::string data(1000, 'y');
    auto compressed = zlibCompress(data);
    REQUIRE(compressed.ok());
    CHECK(zlibInflate(*compressed, 1000).ok());
    auto over = zlibInflate(*compressed, 999);
    REQUIRE_FALSE(over.ok());
    CHECK(over.error().code == ErrorCode::LimitExceeded);
}

TEST_CASE("zlib inflation rejects truncated, trailing and foreign streams", "[sharing][codec]") {
    const std::string data = "The quick brown fox jumps over the lazy dog";
    auto compressed = zlibCompress(data);
    REQUIRE(compressed.ok());

    SECTION("truncated") {
        for (std::size_t cut = 0; cut < compressed->size(); ++cut) {
            auto r = zlibInflate(compressed->substr(0, cut), 1024);
            REQUIRE_FALSE(r.ok());
            CHECK(r.error().code == ErrorCode::ParseError);
        }
    }
    SECTION("trailing bytes") {
        auto r = zlibInflate(*compressed + "x", 1024);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().code == ErrorCode::ParseError);
        auto twice = zlibInflate(*compressed + *compressed, 1024);
        REQUIRE_FALSE(twice.ok());
        CHECK(twice.error().code == ErrorCode::ParseError);
    }
    SECTION("not zlib at all") {
        auto r = zlibInflate(data, 1024);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().code == ErrorCode::ParseError);
    }
    SECTION("raw deflate and gzip are rejected") {
        for (const int windowBits : {-15, 31}) {
            z_stream s{};
            REQUIRE(deflateInit2(&s, 9, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) == Z_OK);
            std::string out(deflateBound(&s, static_cast<uLong>(data.size())) + 32, '\0');
            s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
            s.avail_in = static_cast<uInt>(data.size());
            s.next_out = reinterpret_cast<Bytef*>(out.data());
            s.avail_out = static_cast<uInt>(out.size());
            REQUIRE(deflate(&s, Z_FINISH) == Z_STREAM_END);
            out.resize(out.size() - s.avail_out);
            deflateEnd(&s);
            auto r = zlibInflate(out, 1024);
            REQUIRE_FALSE(r.ok());
            CHECK(r.error().code == ErrorCode::ParseError);
        }
    }
    SECTION("preset dictionary") {
        z_stream s{};
        REQUIRE(deflateInit(&s, 9) == Z_OK);
        const std::string dict = "quick brown fox";
        REQUIRE(deflateSetDictionary(&s, reinterpret_cast<const Bytef*>(dict.data()),
                                     static_cast<uInt>(dict.size())) == Z_OK);
        std::string out(256, '\0');
        s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
        s.avail_in = static_cast<uInt>(data.size());
        s.next_out = reinterpret_cast<Bytef*>(out.data());
        s.avail_out = static_cast<uInt>(out.size());
        REQUIRE(deflate(&s, Z_FINISH) == Z_STREAM_END);
        out.resize(out.size() - s.avail_out);
        deflateEnd(&s);
        auto r = zlibInflate(out, 1024);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().code == ErrorCode::ParseError);
    }
}

TEST_CASE("UTF-8 validation follows RFC 3629", "[sharing][utf8]") {
    CHECK(isValidUtf8(""));
    CHECK(isValidUtf8("plain ASCII"));
    CHECK(isValidUtf8("Ünïcødé 日本語 🚀"));
    CHECK(isValidUtf8("\xF4\x8F\xBF\xBF")); // U+10FFFF
    CHECK(isValidUtf8("\xEF\xBF\xBF"));     // U+FFFF

    CHECK(findInvalidUtf8("ab\x80") == 2);           // lone continuation byte
    CHECK(findInvalidUtf8("\xC0\xAF") == 0);         // overlong '/'
    CHECK(findInvalidUtf8("\xC1\xBF") == 0);         // overlong
    CHECK(findInvalidUtf8("\xE0\x80\xAF") == 0);     // overlong 3-byte
    CHECK(findInvalidUtf8("\xF0\x80\x80\xAF") == 0); // overlong 4-byte
    CHECK(findInvalidUtf8("x\xED\xA0\x80") == 1);    // UTF-16 surrogate U+D800
    CHECK(findInvalidUtf8("\xF4\x90\x80\x80") == 0); // above U+10FFFF
    CHECK(findInvalidUtf8("\xF5\x80\x80\x80") == 0);
    CHECK(findInvalidUtf8("\xFF") == 0);
    CHECK(findInvalidUtf8("ok\xE3\x81") == 2); // truncated sequence
    CHECK(findInvalidUtf8("\xE3\x81\x41") == 0);
}

TEST_CASE("Control characters include C0, DEL and C1", "[sharing][utf8]") {
    CHECK(containsControlCharacters(std::string_view("\0", 1)));
    CHECK(containsControlCharacters("a\tb"));
    CHECK(containsControlCharacters("line\nbreak"));
    CHECK(containsControlCharacters("\x7F"));
    CHECK(containsControlCharacters("\xC2\x85")); // NEL
    CHECK(containsControlCharacters("\xC2\x9F"));
    CHECK_FALSE(containsControlCharacters("\xC2\xA0")); // no-break space is printable
    CHECK_FALSE(containsControlCharacters("Ünïcødé 日本語"));
}

TEST_CASE("Display snippets are short, printable and cut on character boundaries",
          "[sharing][utf8]") {
    CHECK(displaySnippet("short") == "short");
    CHECK(displaySnippet("a\nb") == "a?b");
    CHECK(displaySnippet("bad\xFFz") == "bad\xEF\xBF\xBDz");
    const std::string snippet = displaySnippet("日本語日本語", 10); // 3 bytes per character
    CHECK(snippet == "日本語...");
    CHECK(isValidUtf8(snippet));
}
