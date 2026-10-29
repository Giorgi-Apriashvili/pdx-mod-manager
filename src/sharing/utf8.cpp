#include "sharing/utf8.hpp"

#include <cstdint>

namespace pmm::sharing {
namespace {

// Length of the well-formed sequence starting at `pos`, or 0 if ill-formed.
std::size_t sequenceLength(std::string_view text, std::size_t pos) noexcept {
    const auto byte = [&](std::size_t i) { return static_cast<std::uint8_t>(text[i]); };
    const std::uint8_t lead = byte(pos);
    if (lead < 0x80)
        return 1;

    std::size_t length = 0;
    std::uint8_t lo = 0x80; // allowed range of the second byte (excludes
    std::uint8_t hi = 0xBF; // overlongs, surrogates and > U+10FFFF)
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        if (lead == 0xE0)
            lo = 0xA0;
        else if (lead == 0xED)
            hi = 0x9F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        if (lead == 0xF0)
            lo = 0x90;
        else if (lead == 0xF4)
            hi = 0x8F;
    } else {
        return 0;
    }

    if (text.size() - pos < length)
        return 0;
    if (byte(pos + 1) < lo || byte(pos + 1) > hi)
        return 0;
    for (std::size_t i = 2; i < length; ++i) {
        if ((byte(pos + i) & 0xC0) != 0x80)
            return 0;
    }
    return length;
}

} // namespace

std::size_t findInvalidUtf8(std::string_view text) noexcept {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t length = sequenceLength(text, pos);
        if (length == 0)
            return pos;
        pos += length;
    }
    return std::string_view::npos;
}

bool containsControlCharacters(std::string_view text) noexcept {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<std::uint8_t>(text[i]);
        if (c < 0x20 || c == 0x7F)
            return true;
        // C1 controls U+0080..U+009F are encoded as C2 80..C2 9F.
        if (c == 0xC2 && i + 1 < text.size()) {
            const auto next = static_cast<std::uint8_t>(text[i + 1]);
            if (next >= 0x80 && next <= 0x9F)
                return true;
        }
    }
    return false;
}

std::string displaySnippet(std::string_view text, std::size_t maxBytes) {
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t length = sequenceLength(text, pos);
        std::string_view piece;
        if (length == 0) {
            piece = "\xEF\xBF\xBD"; // U+FFFD
            length = 1;
        } else if (length == 1 && (static_cast<std::uint8_t>(text[pos]) < 0x20 || text[pos] == 0x7F)) {
            piece = "?";
        } else if (length == 2 && containsControlCharacters(text.substr(pos, 2))) {
            piece = "?";
        } else {
            piece = text.substr(pos, length);
        }
        if (out.size() + piece.size() > maxBytes) {
            out += "...";
            return out;
        }
        out += piece;
        pos += length;
    }
    return out;
}

} // namespace pmm::sharing
