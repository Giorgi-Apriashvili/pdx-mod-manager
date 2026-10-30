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
