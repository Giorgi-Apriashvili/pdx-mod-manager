#pragma once

// UTF-8 checks used by the share-code decoder and manifest validation. Text from
// a share code is untrusted: it is validated explicitly instead of relying on
// whatever the JSON parser happens to accept.

#include <cstddef>
#include <string>
#include <string_view>

namespace pmm::sharing {

// Byte offset of the first ill-formed sequence, or std::string_view::npos when
// `text` is well-formed UTF-8 (RFC 3629: no overlong forms, no surrogates, no
// code points above U+10FFFF, no truncated sequences).
std::size_t findInvalidUtf8(std::string_view text) noexcept;

inline bool isValidUtf8(std::string_view text) noexcept {
    return findInvalidUtf8(text) == std::string_view::npos;
}

// True if well-formed UTF-8 `text` contains a C0 control (U+0000..U+001F),
// DEL (U+007F) or a C1 control (U+0080..U+009F).
bool containsControlCharacters(std::string_view text) noexcept;

// Short, printable rendering of untrusted text for error messages: at most
// `maxBytes` bytes (cut on a character boundary, "..." appended), control
// characters replaced by '?', ill-formed UTF-8 replaced by U+FFFD.
std::string displaySnippet(std::string_view text, std::size_t maxBytes = 40);

} // namespace pmm::sharing
