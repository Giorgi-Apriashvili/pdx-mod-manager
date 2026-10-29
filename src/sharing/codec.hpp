#pragma once

// Byte-level building blocks of the share-code envelope: strict unpadded
// Base64URL (RFC 4648 section 5) and zlib (RFC 1950) compression with a hard
// output cap. Byte buffers are carried in std::string. See
// docs/share-code-protocol.md for how share_code.cpp combines them.

#include "core/result.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace pmm::sharing {

// Unpadded Base64URL ('A'-'Z', 'a'-'z', '0'-'9', '-', '_').
std::string base64UrlEncode(std::string_view bytes);

// Strict canonical decoding: only the Base64URL alphabet, no '=' padding, no
// whitespace, length % 4 != 1, and the unused low bits of the final character
// must be zero, so every byte string has exactly one accepted encoding.
// Fails with ParseError.
Result<std::string> base64UrlDecode(std::string_view text);

// zlib stream (RFC 1950), compression level 9, default window and memory
// settings. Deterministic for a given zlib version (pinned in cmake/).
Result<std::string> zlibCompress(std::string_view data);

// Inflates a single zlib stream. Aborts with LimitExceeded as soon as the
// output would exceed `maxOutputBytes` (the cap is enforced while inflating,
// never after). Requires a complete stream with no trailing bytes and no preset
// dictionary; violations fail with ParseError.
Result<std::string> zlibInflate(std::string_view compressed, std::size_t maxOutputBytes);

} // namespace pmm::sharing
