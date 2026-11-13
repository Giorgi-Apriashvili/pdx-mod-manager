#include "sharing/utf8.hpp"

#include <cstdint>

namespace pmm::sharing {

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

std::size_t layoutControlLength(std::string_view text, std::size_t pos) noexcept {
    const std::size_t left = text.size() - pos;
    const auto byte = [text, pos](std::size_t i) {
        return static_cast<std::uint8_t>(text[pos + i]);
    };
    if (left >= 2 && byte(0) == 0xD8 && byte(1) == 0x9C) // U+061C
        return 2;
    if (left < 3 || byte(0) != 0xE2)
        return 0;
    const std::uint8_t last = byte(2);
    // U+200E, U+200F and U+2028..U+202E
    if (byte(1) == 0x80 && (last == 0x8E || last == 0x8F || (last >= 0xA8 && last <= 0xAE)))
        return 3;
    // U+2066..U+2069
    if (byte(1) == 0x81 && last >= 0xA6 && last <= 0xA9)
        return 3;
    return 0;
}

std::string displaySnippet(std::string_view text, std::size_t maxBytes) {
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t length = utf8SequenceLength(text, pos);
        std::string_view piece;
        if (length == 0) {
            piece = kUtf8ReplacementCharacter; // one per ill-formed byte
            length = 1;
        } else if ((length == 1 &&
                    (static_cast<std::uint8_t>(text[pos]) < 0x20 || text[pos] == 0x7F)) ||
                   (length == 2 && containsControlCharacters(text.substr(pos, 2)))) {
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
