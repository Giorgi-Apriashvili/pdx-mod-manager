#pragma once

// Unicode helpers for the repositories. Persistence depends only on core, so
// it carries its own small converters instead of using platform's. Nothing
// here throws except std::bad_alloc.

#include <string>
#include <string_view>

namespace pmm::persistence::internal {

// True if `text` is well-formed UTF-8 (RFC 3629: no overlong forms, no
// surrogates, nothing above U+10FFFF, no truncated sequences).
bool isValidUtf8(std::string_view text) noexcept;

// `text` with every ill-formed sequence replaced by U+FFFD (one per maximal
// ill-formed subpart, as Unicode recommends); for messages and details.
std::string sanitizeUtf8(std::string_view text);

// UTF-16 <-> UTF-8. Unpaired surrogates and ill-formed UTF-8 become U+FFFD,
// so only well-formed input round-trips.
bool isValidUtf16(std::u16string_view text) noexcept;
std::string utf16ToUtf8(std::u16string_view text);
std::u16string utf8ToUtf16(std::string_view text);

#if defined(_WIN32)
// The same for wchar_t, which is UTF-16 on Windows (native path strings).
bool isValidUtf16(std::wstring_view text) noexcept;
std::string utf16ToUtf8(std::wstring_view text);
std::wstring utf8ToWide(std::string_view text);
#endif

} // namespace pmm::persistence::internal
