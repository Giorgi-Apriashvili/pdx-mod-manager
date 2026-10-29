#include "persistence/internal/mapping.hpp"
#include "persistence/internal/utf8.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace pmm;
using namespace pmm::persistence;
using namespace pmm::persistence::internal;
using namespace pmm::persistence::test;

namespace {
const std::string kFffd = "\xEF\xBF\xBD";
}

TEST_CASE("UTF-8 validation follows RFC 3629", "[persistence][encoding]") {
    for (const char* valid : {"", "plain ascii", "caf\xC3\xA9", "\xE6\x97\xA5\xE6\x9C\xAC",
                              "\xF0\x9F\x98\x80", "\xEF\xBF\xBD", "\xF4\x8F\xBF\xBF"})
        CHECK(isValidUtf8(valid));
    for (const char* invalid :
         {"caf\xE9", "\xC0\xAF", "\xE0\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80",
          "\xF8\x88\x80\x80\x80", "\xE2\x82", "\x80", "ok\xFF"})
        CHECK_FALSE(isValidUtf8(invalid));
}

TEST_CASE("sanitizeUtf8 replaces each maximal ill-formed subpart once", "[persistence][encoding]") {
    CHECK(sanitizeUtf8("caf\xC3\xA9") == "caf\xC3\xA9");
    CHECK(sanitizeUtf8("caf\xE9") == "caf" + kFffd);
    CHECK(sanitizeUtf8("\xE2\x82x") == kFffd + "x");              // truncated 3-byte sequence
    CHECK(sanitizeUtf8("\xFF\xFE") == kFffd + kFffd);             // two stray bytes
    CHECK(sanitizeUtf8("\xED\xA0\x80") == kFffd + kFffd + kFffd); // encoded surrogate
    CHECK(sanitizeUtf8("a\xF0\x9F\x98") == "a" + kFffd);
}

TEST_CASE("UTF-16 conversion replaces unpaired surrogates", "[persistence][encoding]") {
    const std::u16string text = u"café \U0001F600";
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

    CHECK(utf8ToUtf16("caf\xE9") == u"caf�");
    CHECK(utf8ToUtf16("\xED\xA0\x80") == u"���");
    CHECK(utf8ToUtf16("").empty());
}

TEST_CASE("Valid paths are stored as exact UTF-8 and read back unchanged",
          "[persistence][encoding]") {
    const std::string text = "C:/Users/\xC3\x9Cn\xC3\xAF/\xE6\x97\xA5\xE6\x9C\xAC "
                             "\xF0\x9F\x98\x80/descriptor.mod";
    const fs::path path = pathFromText(text);
    CHECK(pathToText(path) == text);
    CHECK(path == fs::path(u8"C:/Users/Ün\u00ef/日本 😀/descriptor.mod"));
    CHECK(pathToText(fs::path()).empty());
    CHECK(pathFromText("").empty());
    CHECK(isStorablePath(path));
    CHECK(checkStorablePath(path, "The file").ok());
}

#if defined(_WIN32)
TEST_CASE("Windows path conversion never throws on ill-formed text", "[persistence][encoding]") {
    // An unpaired surrogate has no UTF-8 form; it is written as U+FFFD.
    std::string text;
    REQUIRE_NOTHROW(text = pathToText(loneSurrogatePath(L"C:/x/bad", L".mod")));
    CHECK(text == "C:/x/bad\xEF\xBF\xBD.mod");
    std::wstring lowFirst = L"a";
    lowFirst += static_cast<wchar_t>(0xDC00);
    lowFirst += static_cast<wchar_t>(0xD800);
    CHECK(pathToText(fs::path(lowFirst)) == "a\xEF\xBF\xBD\xEF\xBF\xBD");
    CHECK(pathToText(fs::path(L"pair \U0001F600")) == "pair \xF0\x9F\x98\x80");

    // Stored text that is not UTF-8 (damaged, or written on another OS).
    fs::path path;
    REQUIRE_NOTHROW(path = pathFromText("\xFF\xFE"));
    CHECK(path.native() == L"\uFFFD\uFFFD");
    REQUIRE_NOTHROW(path = pathFromText("caf\xE9/x"));
    CHECK(path.native() == L"caf\uFFFD/x");

    const fs::path unstorable = loneSurrogatePath(L"C:/x/bad", L".mod");
    CHECK_FALSE(isStorablePath(unstorable));
    auto status = checkStorablePath(unstorable, "The file");
    REQUIRE_FALSE(status.ok());
    CHECK(status.error().code == ErrorCode::InvalidArgument);
    CHECK(status.error().message.rfind("The file ", 0) == 0);
    CHECK(status.error().detail.find("C:/x/bad" + kFffd + ".mod") != std::string::npos);
}
#else
TEST_CASE("POSIX paths are stored as their native bytes", "[persistence][encoding]") {
    const std::string bytes = "/home/example/mod/caf\xE9\xFF.mod";
    CHECK(pathToText(fs::path(bytes)) == bytes);
    CHECK(pathFromText(bytes).native() == bytes);
    CHECK(isStorablePath(fs::path(bytes)));
}
#endif
