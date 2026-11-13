#pragma once

// UTF-8 checks used by the share-code decoder and manifest validation. Text from
// a share code is untrusted: it is validated explicitly instead of relying on
// whatever the JSON parser happens to accept.

#include "core/utf8.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace pmm::sharing {

// RFC 3629 validation from core/utf8.hpp: findInvalidUtf8() returns the byte
// offset of the first ill-formed sequence, or std::string_view::npos.
using pmm::findInvalidUtf8;
using pmm::isValidUtf8;

// True if well-formed UTF-8 `text` contains a C0 control (U+0000..U+001F),
// DEL (U+007F) or a C1 control (U+0080..U+009F).
bool containsControlCharacters(std::string_view text) noexcept;

// Length of the character at text[pos] if it is one that reorders or breaks
// the text displayed around it, else 0: the bidirectional marks, embeddings,
// overrides and isolates (U+061C, U+200E, U+200F, U+202A..U+202E,
// U+2066..U+2069) and the line and paragraph separators (U+2028, U+2029).
// Requires pos < text.size().
std::size_t layoutControlLength(std::string_view text, std::size_t pos) noexcept;

// Short, printable rendering of untrusted text for error messages: at most
// `maxBytes` bytes (cut on a character boundary, "..." appended), control
// characters replaced by '?', ill-formed UTF-8 replaced by U+FFFD.
std::string displaySnippet(std::string_view text, std::size_t maxBytes = 40);

} // namespace pmm::sharing
