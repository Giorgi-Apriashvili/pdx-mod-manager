#include "persistence/internal/utf8.hpp"

#include <cstddef>
#include <cstdint>

namespace pmm::persistence::internal {

namespace {

constexpr char32_t kReplacement = 0xFFFD;

struct Decoded {
    char32_t codePoint = kReplacement;
    std::size_t length = 1; // bytes consumed, at least 1
    bool valid = false;
};

// Decodes the sequence at `pos`. An ill-formed sequence yields U+FFFD and
// consumes its maximal well-formed prefix.
Decoded decodeUtf8(std::string_view text, std::size_t pos) noexcept {
    const auto byte = [&](std::size_t i) { return static_cast<std::uint8_t>(text[i]); };
    const std::uint8_t lead = byte(pos);
    if (lead < 0x80)
        return {lead, 1, true};

    std::size_t length = 0;
    char32_t codePoint = 0;
    std::uint8_t lo = 0x80; // allowed range of the second byte (excludes
    std::uint8_t hi = 0xBF; // overlongs, surrogates and > U+10FFFF)
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        codePoint = static_cast<char32_t>(lead & 0x1Fu);
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        codePoint = static_cast<char32_t>(lead & 0x0Fu);
        if (lead == 0xE0)
            lo = 0xA0;
        else if (lead == 0xED)
            hi = 0x9F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        codePoint = static_cast<char32_t>(lead & 0x07u);
        if (lead == 0xF0)
            lo = 0x90;
        else if (lead == 0xF4)
            hi = 0x8F;
    } else {
        return {};
    }

    for (std::size_t i = 1; i < length; ++i) {
        if (pos + i >= text.size() || byte(pos + i) < lo || byte(pos + i) > hi)
            return {kReplacement, i, false};
        codePoint = (codePoint << 6) | static_cast<char32_t>(byte(pos + i) & 0x3Fu);
        lo = 0x80;
        hi = 0xBF;
    }
    return {codePoint, length, true};
}

void appendUtf8(std::string& out, char32_t c) {
    const auto put = [&](char32_t bits) {
        out += static_cast<char>(static_cast<std::uint8_t>(bits));
    };
    if (c < 0x80) {
        put(c);
    } else if (c < 0x800) {
        put(0xC0 | (c >> 6));
        put(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        put(0xE0 | (c >> 12));
        put(0x80 | ((c >> 6) & 0x3F));
        put(0x80 | (c & 0x3F));
    } else {
        put(0xF0 | (c >> 18));
        put(0x80 | ((c >> 12) & 0x3F));
        put(0x80 | ((c >> 6) & 0x3F));
        put(0x80 | (c & 0x3F));
    }
}

constexpr bool isHighSurrogate(char32_t unit) noexcept {
    return unit >= 0xD800 && unit <= 0xDBFF;
}
constexpr bool isLowSurrogate(char32_t unit) noexcept {
    return unit >= 0xDC00 && unit <= 0xDFFF;
}

// Code point starting at `i`; advances `i` past a surrogate pair. Unpaired
// surrogates yield U+FFFD with `valid` cleared.
template <class Char>
char32_t nextUtf16(std::basic_string_view<Char> text, std::size_t& i, bool& valid) noexcept {
    static_assert(sizeof(Char) == 2, "UTF-16 code units");
    const auto unit = [&](std::size_t at) { return static_cast<char32_t>(text[at]); };
    const char32_t first = unit(i);
    if (isHighSurrogate(first) && i + 1 < text.size() && isLowSurrogate(unit(i + 1))) {
        const char32_t second = unit(++i);
        return 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
    }
    if (isHighSurrogate(first) || isLowSurrogate(first)) {
        valid = false;
        return kReplacement;
    }
    return first;
}

template <class Char> bool validUtf16(std::basic_string_view<Char> text) noexcept {
    bool valid = true;
    for (std::size_t i = 0; i < text.size() && valid; ++i)
        nextUtf16(text, i, valid);
    return valid;
}

template <class Char> std::string encodeUtf16(std::basic_string_view<Char> text) {
    std::string out;
    out.reserve(text.size());
    bool valid = true;
    for (std::size_t i = 0; i < text.size(); ++i)
        appendUtf8(out, nextUtf16(text, i, valid));
    return out;
}

template <class String> String decodeToUtf16(std::string_view text) {
    using Char = typename String::value_type;
    String out;
    out.reserve(text.size());
    for (std::size_t pos = 0; pos < text.size();) {
        const Decoded decoded = decodeUtf8(text, pos);
        pos += decoded.length;
        if (decoded.codePoint >= 0x10000) {
            const char32_t offset = decoded.codePoint - 0x10000;
            out += static_cast<Char>(0xD800 + (offset >> 10));
            out += static_cast<Char>(0xDC00 + (offset & 0x3FF));
        } else {
            out += static_cast<Char>(decoded.codePoint);
        }
    }
    return out;
}

} // namespace

bool isValidUtf8(std::string_view text) noexcept {
    for (std::size_t pos = 0; pos < text.size();) {
        const Decoded decoded = decodeUtf8(text, pos);
        if (!decoded.valid)
            return false;
        pos += decoded.length;
    }
    return true;
}

std::string sanitizeUtf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t pos = 0; pos < text.size();) {
        const Decoded decoded = decodeUtf8(text, pos);
        if (decoded.valid)
            out.append(text.substr(pos, decoded.length));
        else
            appendUtf8(out, kReplacement);
        pos += decoded.length;
    }
    return out;
}

bool isValidUtf16(std::u16string_view text) noexcept {
    return validUtf16(text);
}

std::string utf16ToUtf8(std::u16string_view text) {
    return encodeUtf16(text);
}

std::u16string utf8ToUtf16(std::string_view text) {
    return decodeToUtf16<std::u16string>(text);
}

#if defined(_WIN32)
bool isValidUtf16(std::wstring_view text) noexcept {
    return validUtf16(text);
}

std::string utf16ToUtf8(std::wstring_view text) {
    return encodeUtf16(text);
}

std::wstring utf8ToWide(std::string_view text) {
    return decodeToUtf16<std::wstring>(text);
}
#endif

} // namespace pmm::persistence::internal
