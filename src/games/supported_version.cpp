#include "games/supported_version.hpp"

#include <charconv>
#include <system_error>

namespace pmm::games {
namespace {

constexpr std::size_t kMaxComponents = 8;

bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

std::string_view trimmed(std::string_view text) noexcept {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

std::optional<std::uint32_t> toNumber(std::string_view digits) noexcept {
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (error != std::errc() || end != digits.data() + digits.size())
        return std::nullopt;
    return value;
}

} // namespace

std::optional<std::vector<std::uint32_t>> parseGameVersion(std::string_view text) {
    // First run of "digits(.digits)+", so labels like "Cygnus v4.5.2 (9776)" work.
    for (std::size_t start = 0; start < text.size(); ++start) {
        if (!isDigit(text[start]) || (start > 0 && isDigit(text[start - 1])))
            continue;
        std::vector<std::uint32_t> parts;
        std::size_t pos = start;
        for (;;) {
            const std::size_t begin = pos;
            while (pos < text.size() && isDigit(text[pos]))
                ++pos;
            const auto number = toNumber(text.substr(begin, pos - begin));
            if (!number)
                break;
            parts.push_back(*number);
            if (pos + 1 < text.size() && text[pos] == '.' && isDigit(text[pos + 1]) &&
                parts.size() < kMaxComponents)
                ++pos;
            else
                break;
        }
        if (parts.size() >= 2)
            return parts;
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> parseVersionPattern(std::string_view pattern) {
    pattern = trimmed(pattern);
    if (!pattern.empty() && (pattern.front() == 'v' || pattern.front() == 'V'))
        pattern.remove_prefix(1);
    if (pattern.empty())
        return std::nullopt;
    std::vector<std::string> parts;
    for (;;) {
        const std::size_t dot = pattern.find('.');
        const std::string_view part = pattern.substr(0, dot);
        const bool numeric = !part.empty() && part.size() <= 9 &&
                             part.find_first_not_of("0123456789") == std::string_view::npos;
        if (part != "*" && !numeric)
            return std::nullopt;
        parts.emplace_back(part);
        if (parts.size() > kMaxComponents)
            return std::nullopt;
        if (dot == std::string_view::npos)
            return parts;
        pattern.remove_prefix(dot + 1);
    }
}

SupportedVersionCheck checkSupportedVersion(std::string_view pattern, std::string_view gameVersion) {
    SupportedVersionCheck check;
    if (trimmed(pattern).empty())
        return check;
    const auto parts = parseVersionPattern(pattern);
    if (!parts) {
        check.match = VersionMatch::Unparsable;
        return check;
    }
    for (std::size_t i = 0; i < parts->size() && i < 2; ++i)
        check.wildcardBeforePatch = check.wildcardBeforePatch || (*parts)[i] == "*";

    const auto version = parseGameVersion(gameVersion);
    if (!version) {
        check.match = VersionMatch::UnknownGameVersion;
        return check;
    }
    check.match = VersionMatch::Matches;
    for (std::size_t i = 0; i < parts->size(); ++i) {
        if ((*parts)[i] == "*")
            continue;
        const std::uint32_t installed = i < version->size() ? (*version)[i] : 0;
        if (toNumber((*parts)[i]) != installed) {
            check.match = VersionMatch::Mismatch;
            break;
        }
    }
    return check;
}

} // namespace pmm::games
