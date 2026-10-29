#include "core/utf8.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using namespace pmm;

namespace {
const std::string kFffd(kUtf8ReplacementCharacter);
constexpr auto npos = std::string_view::npos;
} // namespace

TEST_CASE("UTF-8 validation follows RFC 3629", "[core][utf8]") {
    for (const char* valid :
         {"", "plain ascii", "caf\xC3\xA9", "\xE6\x97\xA5\xE6\x9C\xAC", "\xF0\x9F\x98\x80",
          "\xEF\xBF\xBD", "\xF4\x8F\xBF\xBF", "\xED\x9F\xBF", "\xEE\x80\x80"})
        CHECK(isValidUtf8(valid));
    CHECK(isValidUtf8(std::string_view("nul\0inside", 10)));

    for (const char* invalid :
         {"caf\xE9", "\xC0\xAF", "\xC1\xBF", "\xE0\x80\xAF", "\xF0\x80\x80\xAF", "\xED\xA0\x80",
          "\xED\xBF\xBF", "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xF8\x88\x80\x80\x80",
          "\xE2\x82", "\x80", "ok\xFF", "\xFE"})
        CHECK_FALSE(isValidUtf8(invalid));
}

TEST_CASE("findInvalidUtf8 reports the offset of the first ill-formed sequence", "[core][utf8]") {
    CHECK(findInvalidUtf8("") == npos);
    CHECK(findInvalidUtf8("caf\xC3\xA9 \xF0\x9F\x98\x80") == npos);
    CHECK(findInvalidUtf8("ab\x80") == 2);           // lone continuation byte
    CHECK(findInvalidUtf8("\xC0\xAF") == 0);         // overlong '/'
    CHECK(findInvalidUtf8("x\xED\xA0\x80") == 1);    // UTF-16 surrogate U+D800
    CHECK(findInvalidUtf8("\xF4\x90\x80\x80") == 0); // above U+10FFFF
    CHECK(findInvalidUtf8("ok\xE3\x81") == 2);       // truncated sequence
    CHECK(findInvalidUtf8("\xE3\x81\x41") == 0);     // continuation missing
    CHECK(findInvalidUtf8("\xC3\xA9\xC3") == 2);
}

TEST_CASE("utf8SequenceLength measures one well-formed sequence", "[core][utf8]") {
    const std::string text = "a\xC3\xA9\xE6\x97\xA5\xF0\x9F\x98\x80\xFF";
    CHECK(utf8SequenceLength(text, 0) == 1);
    CHECK(utf8SequenceLength(text, 1) == 2);
    CHECK(utf8SequenceLength(text, 2) == 0); // inside a character
    CHECK(utf8SequenceLength(text, 3) == 3);
    CHECK(utf8SequenceLength(text, 6) == 4);
    CHECK(utf8SequenceLength(text, 10) == 0);
    CHECK(utf8SequenceLength("\xE6\x97", 0) == 0); // truncated at the end
}

TEST_CASE("sanitizeUtf8 replaces each maximal ill-formed subpart once", "[core][utf8]") {
    CHECK(sanitizeUtf8("") == "");
    CHECK(sanitizeUtf8("caf\xC3\xA9") == "caf\xC3\xA9");
    CHECK(sanitizeUtf8("caf\xE9") == "caf" + kFffd);
    CHECK(sanitizeUtf8("\xE2\x82x") == kFffd + "x");              // truncated 3-byte sequence
    CHECK(sanitizeUtf8("a\xF0\x9F\x98") == "a" + kFffd);          // truncated at the end
    CHECK(sanitizeUtf8("\xFF\xFE") == kFffd + kFffd);             // two stray bytes
    CHECK(sanitizeUtf8("\xED\xA0\x80") == kFffd + kFffd + kFffd); // encoded surrogate
    CHECK(sanitizeUtf8("\xC0\xAF") == kFffd + kFffd);             // overlong
    CHECK(sanitizeUtf8("\xF4\x90\x80\x80") == kFffd + kFffd + kFffd + kFffd);
    CHECK(sanitizeUtf8("\xC3\x28") == kFffd + "(");
    CHECK(isValidUtf8(sanitizeUtf8("\x80\xBF\xF8\xE0\xA0\xF0\x90\x80")));
}

TEST_CASE("utf8Prefix never cuts inside a character", "[core][utf8]") {
    const std::string text = "ab\xE6\x97\xA5\xF0\x9F\x98\x80";
    CHECK(utf8Prefix(text, 100) == text);
    CHECK(utf8Prefix(text, text.size()) == text);
    CHECK(utf8Prefix(text, 2) == "ab");
    CHECK(utf8Prefix(text, 3) == "ab");
    CHECK(utf8Prefix(text, 4) == "ab");
    CHECK(utf8Prefix(text, 5) == "ab\xE6\x97\xA5");
    CHECK(utf8Prefix(text, 8) == "ab\xE6\x97\xA5");
    CHECK(utf8Prefix(text, 0).empty());
    CHECK(utf8Prefix("\xE6\x97\xA5", 2).empty());
}

TEST_CASE("UTF-16 conversion replaces unpaired surrogates", "[core][utf8]") {
    const std::u16string text = u"caf\xE9 \U0001F600";
    CHECK(isValidUtf16(text));
    CHECK(utf16ToUtf8(text) == "caf\xC3\xA9 \xF0\x9F\x98\x80");
    CHECK(utf8ToUtf16("caf\xC3\xA9 \xF0\x9F\x98\x80") == text);

    const std::u16string lone{u'a', static_cast<char16_t>(0xD800), u'b',
                              static_cast<char16_t>(0xDC00)};
    CHECK_FALSE(isValidUtf16(lone));
    CHECK(utf16ToUtf8(lone) == "a" + kFffd + "b" + kFffd);
    const std::u16string reversed{static_cast<char16_t>(0xDE00), static_cast<char16_t>(0xD83D)};
    CHECK_FALSE(isValidUtf16(reversed));
    CHECK(utf16ToUtf8(reversed) == kFffd + kFffd);

    CHECK(utf8ToUtf16("caf\xE9") == u"caf\xFFFD");
    CHECK(utf8ToUtf16("\xED\xA0\x80") == u"\xFFFD\xFFFD\xFFFD");
    CHECK(utf8ToUtf16("").empty());

#if defined(_WIN32)
    const std::wstring wide = L"caf\xD800";
    CHECK_FALSE(isValidUtf16(std::wstring_view(wide)));
    CHECK(utf16ToUtf8(std::wstring_view(wide)) == "caf" + kFffd);
    CHECK(utf8ToWide("caf\xC3\xA9 \xF0\x9F\x98\x80") == L"caf\xE9 \U0001F600");
    CHECK(isValidUtf16(std::wstring_view(L"caf\xE9")));
#endif
}
