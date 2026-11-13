#pragma once

// Workshop ID lists (docs/share-code-protocol.md section 8): a plain list of
// Steam Workshop item ids, such as "3818297502|3149445431|3366734814", as other
// mod managers share them. Unlike a share code a list carries no game, no
// playset name, no local mods, no disabled entries and no checksum, and it is
// not versioned. Readers accept more than writers produce: ids separated by
// '|', commas, spaces, tabs, line breaks and the Unicode spaces chats insert,
// and links to Steam Workshop item pages. Links are only read, never fetched.
//
// Text that starts with "PMM" is a share code (sharing/share_code.hpp), and
// anything else a list; share codes and their fixtures are unaffected.

#include "core/model.hpp"
#include "core/result.hpp"
#include "sharing/manifest.hpp"
#include "sharing/share_code.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::sharing {

enum class ImportTextFormat { ShareCode, WorkshopIdList };

// After leading ASCII whitespace (space, tab, CR, LF): text starting with
// "PMM" is a share code, anything else (empty text too) a Workshop ID list.
ImportTextFormat detectImportTextFormat(std::string_view input) noexcept;

// Raw input, checked before anything else: the share-code limit.
inline constexpr std::size_t kMaxIdListInputBytes = kMaxEncodedBytes;
// Name of a playset imported from a list, which carries none.
inline constexpr std::string_view kIdListPlaysetName = "Imported mod list";
// Separator of an exported list.
inline constexpr char kIdListSeparator = '|';

// The canonical ids of the list, in list order, in one linear pass. Entries
// are separated by runs of '|', ',', space, tab, CR, LF, U+00A0, U+202F,
// U+200B and U+FEFF (';' is not a separator). Each entry is a canonical id
// (isValidWorkshopId) or a link to a Steam Workshop item page
// ([http[s]://][www.]steamcommunity.com/{sharedfiles|workshop}/filedetails[/]?...id=<id>...;
// scheme, host and path ignore ASCII case; other parameters and a fragment
// are ignored, a second id= is not). Error messages name the 1-based entry
// and quote it as displaySnippet() shows it; byte offsets go into the detail.
// Error codes: LimitExceeded (more than kMaxIdListInputBytes, or more than
// kMaxEntries ids), ParseError (an entry that is neither an id nor an item
// link, a ";N" position suffix, no entry at all) and ValidationFailed (an
// item listed twice, also as an id and a link).
Result<std::vector<std::string>> parseWorkshopIdList(std::string_view input);

// The export form: the ids joined by '|', nothing else.
std::string formatWorkshopIdList(std::span<const std::string> ids);

// Manifest (schema 1) for importing `ids` into `game`: every entry a Steam
// Workshop entry, enabled, without hints, in list order, and no game version
// hint. Validated with validateManifest(), so it fails like a share code for
// an unknown or unsupported game.
Result<Manifest> manifestFromWorkshopIds(const GameKey& game, std::span<const std::string> ids,
                                         std::string playsetName = std::string(kIdListPlaysetName));

struct WorkshopIdListExport {
    std::string text;                // formatWorkshopIdList(ids); empty without ids
    std::vector<std::string> ids;    // the enabled Workshop entries, in playset order
    std::size_t leftOutLocal = 0;    // local entries, enabled or not
    std::size_t leftOutDisabled = 0; // disabled Workshop entries
};

// The list for `playset`: its enabled Steam Workshop entries in playset
// order, and how many entries a list cannot carry. Fails with WrongGame for
// an entry of another game and ValidationFailed for an id that is not
// canonical or listed twice, as manifestFromPlayset() and the share code would.
Result<WorkshopIdListExport> workshopIdListFromPlayset(const Playset& playset);

} // namespace pmm::sharing
