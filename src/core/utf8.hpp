#pragma once

// The one UTF-8 implementation shared by every module (RFC 3629: no overlong
// forms, no surrogates U+D800..U+DFFF, nothing above U+10FFFF, no truncated
// sequences), plus portable UTF-16 conversions. Nothing here throws except
// std::bad_alloc.

#include <cstddef>
#include <string>
#include <string_view>

namespace pmm {

// U+FFFD REPLACEMENT CHARACTER, UTF-8 encoded.
inline constexpr std::string_view kUtf8ReplacementCharacter = "\xEF\xBF\xBD";

// Length (1..4) of the well-formed sequence starting at text[pos], or 0 when
// the bytes there are ill-formed. Requires pos < text.size().
std::size_t utf8SequenceLength(std::string_view text, std::size_t pos) noexcept;

// Byte offset of the first ill-formed sequence, or std::string_view::npos
// when `text` is well-formed UTF-8.
std::size_t findInvalidUtf8(std::string_view text) noexcept;

inline bool isValidUtf8(std::string_view text) noexcept {
    return findInvalidUtf8(text) == std::string_view::npos;
}

// `text` with every ill-formed sequence replaced by U+FFFD, one per maximal
// ill-formed subpart (the Unicode best practice that Windows and browsers
// follow): a truncated "\xE2\x82" becomes one U+FFFD, two stray bytes two.
std::string sanitizeUtf8(std::string_view text);

// Longest prefix of well-formed `text` that has at most `maxBytes` bytes and
// does not end inside a character.
std::string_view utf8Prefix(std::string_view text, std::size_t maxBytes) noexcept;

// UTF-16 <-> UTF-8. Unpaired surrogates and ill-formed UTF-8 become U+FFFD,
// so only well-formed input round-trips (see isValidUtf16()).
bool isValidUtf16(std::u16string_view text) noexcept;
std::string utf16ToUtf8(std::u16string_view text);
std::u16string utf8ToUtf16(std::string_view text);

// The same for wchar_t, which is UTF-16 on Windows (native path strings).
bool isValidUtf16(std::wstring_view text) noexcept;
std::string utf16ToUtf8(std::wstring_view text);
std::wstring utf8ToWide(std::string_view text);

} // namespace pmm
