#include "sharing/workshop_id_list.hpp"

#include "sharing/utf8.hpp"

#include <array>
#include <optional>
#include <unordered_map>
#include <utility>

namespace pmm::sharing {
namespace {

// Longer than the default: a quoted link should still show its id.
constexpr std::size_t kLinkSnippetBytes = 100;

bool isAsciiSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool isDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

char asciiLower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// `prefix` is lowercase ASCII.
bool startsWithIgnoringCase(std::string_view text, std::string_view prefix) noexcept {
    if (text.size() < prefix.size())
        return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (asciiLower(text[i]) != prefix[i])
            return false;
    }
    return true;
}

// Length of the separator at `pos`, or 0. The Unicode ones are U+00A0
// (no-break space), U+202F (narrow no-break space), U+200B (zero-width space)
// and U+FEFF (byte order mark, zero-width no-break space), which chats and
// editors insert.
std::size_t separatorLength(std::string_view text, std::size_t pos) noexcept {
    const char c = text[pos];
    if (c == '|' || c == ',' || isAsciiSpace(c))
        return 1;
    static constexpr std::array<std::string_view, 4> kUnicode{"\xC2\xA0", "\xE2\x80\xAF",
                                                              "\xE2\x80\x8B", "\xEF\xBB\xBF"};
    for (const std::string_view separator : kUnicode) {
        if (text.substr(pos, separator.size()) == separator)
            return separator.size();
    }
    return 0;
}

bool looksLikeLink(std::string_view token) noexcept {
    return token.find("://") != std::string_view::npos || startsWithIgnoringCase(token, "www.") ||
           startsWithIgnoringCase(token, "steamcommunity.com");
}

enum class LinkProblem { None, NotItemPage, NoId, TwoIds };

// The item id of a link to a Steam Workshop item page, or why there is none.
LinkProblem readItemLink(std::string_view link, std::string& id) {
    std::string_view rest = link;
    if (startsWithIgnoringCase(rest, "https://"))
        rest.remove_prefix(8);
    else if (startsWithIgnoringCase(rest, "http://"))
        rest.remove_prefix(7);
    if (startsWithIgnoringCase(rest, "www."))
        rest.remove_prefix(4);
    constexpr std::string_view kHost = "steamcommunity.com";
    if (!startsWithIgnoringCase(rest, kHost))
        return LinkProblem::NotItemPage;
    rest.remove_prefix(kHost.size());
    bool itemPage = false;
    for (const std::string_view path : {std::string_view("/sharedfiles/filedetails"),
                                        std::string_view("/workshop/filedetails")}) {
        if (startsWithIgnoringCase(rest, path)) {
            rest.remove_prefix(path.size());
            itemPage = true;
            break;
        }
    }
    if (!itemPage)
        return LinkProblem::NotItemPage;
    if (!rest.empty() && rest.front() == '/')
        rest.remove_prefix(1);
    if (rest.empty() || rest.front() == '#')
        return LinkProblem::NoId;
    if (rest.front() != '?')
        return LinkProblem::NotItemPage;
    rest.remove_prefix(1);
    rest = rest.substr(0, rest.find('#'));

    bool found = false;
    for (;;) {
        const std::size_t amp = rest.find('&');
        const std::string_view parameter = rest.substr(0, amp);
        if (parameter.substr(0, 3) == "id=") {
            if (found)
                return LinkProblem::TwoIds;
            found = true;
            id.assign(parameter.substr(3));
        }
        if (amp == std::string_view::npos)
            break;
        rest.remove_prefix(amp + 1);
    }
    return found && isValidWorkshopId(id) ? LinkProblem::None : LinkProblem::NoId;
}

// The id an entry stands for: a canonical id or an item link.
std::optional<std::string> readEntry(std::string_view token) {
    if (isValidWorkshopId(token))
        return std::string(token);
    std::string id;
    if (looksLikeLink(token) && readItemLink(token, id) == LinkProblem::None)
        return id;
    return std::nullopt;
}

std::string quotedSnippet(std::string_view token, std::size_t maxBytes = 40) {
    return "\"" + displaySnippet(token, maxBytes) + "\"";
}

std::string position(std::size_t entry, std::size_t offset) {
    return "entry " + std::to_string(entry) + " at byte offset " + std::to_string(offset);
}

// Why `token`, entry number `entry`, is not an id.
Error badEntry(std::string_view token, std::size_t entry, std::size_t offset) {
    const std::string where = position(entry, offset);
    const std::string label = "Entry " + std::to_string(entry);

    // A position suffix as Total War mod managers write it: "<id>;<n>".
    if (const std::size_t semicolon = token.find(';'); semicolon != std::string_view::npos) {
        const std::string_view suffix = token.substr(semicolon + 1);
        bool digits = !suffix.empty();
        for (const char c : suffix)
            digits = digits && isDigit(c);
        if (digits && readEntry(token.substr(0, semicolon)))
            return makeError(ErrorCode::ParseError,
                             label + ", " + quotedSnippet(token) + ", has a " +
                                 quotedSnippet(token.substr(semicolon)) +
                                 " suffix. Other mod managers use it to pin a load position; "
                                 "Paradox games load mods in list order, so remove the suffixes "
                                 "and put the IDs in the order the mods should load.",
                             where);
    }

    if (looksLikeLink(token)) {
        std::string id;
        switch (readItemLink(token, id)) {
        case LinkProblem::NoId:
            return makeError(ErrorCode::ParseError,
                             label + ", " + quotedSnippet(token, kLinkSnippetBytes) +
                                 ", is a Steam Workshop link without a valid item ID.",
                             where);
        case LinkProblem::TwoIds:
            return makeError(ErrorCode::ParseError,
                             label + ", " + quotedSnippet(token, kLinkSnippetBytes) +
                                 ", is a Steam Workshop link with more than one item ID.",
                             where);
        case LinkProblem::None: // not reached: readEntry() accepted it
        case LinkProblem::NotItemPage:
            break;
        }
        return makeError(ErrorCode::ParseError,
                         label + ", " + quotedSnippet(token, kLinkSnippetBytes) +
                             ", is a link, but not to a Steam Workshop item page.",
                         where);
    }

    if (entry == 1)
        return makeError(ErrorCode::ParseError,
                         "This text is neither a share code (it starts with \"PMM1.\") nor a list "
                         "of Workshop IDs: " +
                             quotedSnippet(token) +
                             " is not a Workshop ID or a Steam Workshop link.",
                         where);
    return makeError(ErrorCode::ParseError,
                     label + " of the Workshop ID list, " + quotedSnippet(token) +
                         ", is not a Workshop ID or a Steam Workshop link. Workshop IDs are "
                         "numbers such as 3818297502.",
                     where);
}

} // namespace

ImportTextFormat detectImportTextFormat(std::string_view input) noexcept {
    std::size_t start = 0;
    while (start < input.size() && isAsciiSpace(input[start]))
        ++start;
    return input.substr(start, 3) == "PMM" ? ImportTextFormat::ShareCode
                                           : ImportTextFormat::WorkshopIdList;
}

Result<std::vector<std::string>> parseWorkshopIdList(std::string_view input) {
    // The size limit applies to the raw input, before any other processing.
    if (input.size() > kMaxIdListInputBytes)
        return makeError(ErrorCode::LimitExceeded,
                         "This text is too large to be a share code or a Workshop ID list "
                         "(limit 2 MiB).",
                         std::to_string(input.size()) + " bytes");

    std::vector<std::string> ids;
    std::unordered_map<std::string, std::size_t> seen; // id -> entry number
    std::size_t entry = 0;
    std::size_t pos = 0;
    while (pos < input.size()) {
        if (const std::size_t separator = separatorLength(input, pos); separator > 0) {
            pos += separator;
            continue;
        }
        const std::size_t start = pos;
        while (pos < input.size() && separatorLength(input, pos) == 0)
            ++pos;
        const std::string_view token = input.substr(start, pos - start);
        ++entry;
        if (entry > kMaxEntries)
            return makeError(ErrorCode::LimitExceeded,
                             "The Workshop ID list has more than " + std::to_string(kMaxEntries) +
                                 " entries, more than a playset can hold.",
                             position(entry, start));
        std::optional<std::string> id = readEntry(token);
        if (!id)
            return badEntry(token, entry, start);
        const auto [it, inserted] = seen.emplace(*id, entry);
        if (!inserted)
            return makeError(ErrorCode::ValidationFailed,
                             "Workshop ID " + *id + " is listed twice, as entry " +
                                 std::to_string(it->second) + " and entry " +
                                 std::to_string(entry) +
                                 ". Remove one of them: a playset holds each mod once.",
                             position(entry, start));
        ids.push_back(std::move(*id));
    }
    if (ids.empty())
        return makeError(ErrorCode::ParseError,
                         "The text contains no Workshop IDs. Paste a share code (PMM1.…) or "
                         "IDs such as 3818297502|3149445431.");
    return ids;
}

std::string formatWorkshopIdList(std::span<const std::string> ids) {
    std::string text;
    std::size_t length = 0;
    for (const std::string& id : ids)
        length += id.size() + 1;
    text.reserve(length);
    for (const std::string& id : ids) {
        if (!text.empty())
            text += kIdListSeparator;
        text += id;
    }
    return text;
}

Result<Manifest> manifestFromWorkshopIds(const GameKey& game, std::span<const std::string> ids,
                                         std::string playsetName) {
    Manifest manifest;
    manifest.schemaVersion = kManifestSchemaVersion;
    manifest.game = game;
    manifest.playsetName = std::move(playsetName);
    manifest.mods.reserve(ids.size());
    for (const std::string& id : ids)
        manifest.mods.push_back(ManifestEntry{source::SteamWorkshop, id, {}, true, {}, {}});
    PMM_TRY(validateManifest(manifest));
    return manifest;
}

Result<WorkshopIdListExport> workshopIdListFromPlayset(const Playset& playset) {
    WorkshopIdListExport out;
    std::unordered_map<std::string, std::size_t> seen; // id -> entry number
    for (std::size_t i = 0; i < playset.entries.size(); ++i) {
        const PlaysetEntry& entry = playset.entries[i];
        const std::string number = "#" + std::to_string(i + 1);
        if (!entry.identity.game.empty() && entry.identity.game != playset.game)
            return makeError(ErrorCode::WrongGame,
                             "Entry " + number + " belongs to another game (" +
                                 displaySnippet(entry.identity.game) +
                                 ") and cannot be shared with this playset.",
                             toString(entry.identity));
        if (entry.identity.source != source::SteamWorkshop) {
            ++out.leftOutLocal;
            continue;
        }
        if (!entry.enabled) {
            ++out.leftOutDisabled;
            continue;
        }
        if (!isValidWorkshopId(entry.identity.id))
            return makeError(ErrorCode::ValidationFailed,
                             "Entry " + number + " has an invalid Steam Workshop ID (" +
                                 quotedSnippet(entry.identity.id) + ") and cannot be shared.",
                             toString(entry.identity));
        const auto [it, inserted] = seen.emplace(entry.identity.id, i + 1);
        if (!inserted)
            return makeError(ErrorCode::ValidationFailed,
                             "Entries #" + std::to_string(it->second) + " and " + number +
                                 " are the same Workshop mod (" + entry.identity.id +
                                 "), so the playset cannot be shared.",
                             toString(entry.identity));
        out.ids.push_back(entry.identity.id);
    }
    out.text = formatWorkshopIdList(out.ids);
    return out;
}

} // namespace pmm::sharing
