#pragma once

// Clausewitz mod descriptors: the .mod files in the game's user mod folder and
// descriptor.mod inside mod content. Parses them into core ModDescriptor and
// regenerates them deterministically.
//
// Accepted syntax (what EU4 and Stellaris descriptors use):
//   key="quoted value"   key=bare_value   key={ "item" item ... }
//   # comments to the end of the line, optional UTF-8 BOM, LF or CRLF.
// In quoted strings only \" and \\ are escapes; any other backslash is kept
// literally (Windows paths). Quoted strings may span lines. A missing '='
// before '{' is tolerated. A block that holds nested data (key={ a=b } or
// key={ { } }) is skipped with a warning: descriptors never need it, and the
// game ignores keys it does not know.
//
// Mapping onto ModDescriptor (key names compared ASCII case-insensitively):
// name, version, supported_version, picture, path, archive and remote_file_id
// are scalars (the last occurrence wins); tags and dependencies are lists
// (several blocks append; a scalar counts as a one-item list). Every other key
// goes to ModDescriptor::extra in file order, duplicates included (e.g.
// repeated replace_path=). Unknown LIST keys are stored there too, as
// {"<key>{}", "<items>"}: the "{}" suffix cannot occur in a real key, and the
// items are written as quoted, space-separated strings (encodeListValue()).
//
// All text is made valid UTF-8 (invalid bytes become U+FFFD) because the
// inventory is persisted as JSON.

#include "core/model.hpp"
#include "core/result.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pmm::games {

namespace fs = std::filesystem;

// Suffix marking an unknown list key in ModDescriptor::extra.
inline constexpr std::string_view kListKeySuffix = "{}";

struct DescriptorEntry {
    std::string key;                 // as written
    std::vector<std::string> values; // scalars: exactly one value
    bool isList = false;
    int line = 0; // 1-based line of the key
};

// Every key/value of a descriptor in file order. Used where regenerated files
// must keep the author's layout.
struct DescriptorDocument {
    std::vector<DescriptorEntry> entries;
    std::vector<std::string> warnings; // tolerated problems, "line N: ..."
};

// ParseError with the line number for malformed input (unterminated string,
// unbalanced braces, key without value, ...).
Result<DescriptorDocument> parseDescriptorDocument(std::string_view text);
Result<ModDescriptor> parseDescriptor(std::string_view text);

// Reads at most 1 MiB. Errors name the file.
Result<DescriptorDocument> readDescriptorDocument(const fs::path& file);
Result<ModDescriptor> readDescriptor(const fs::path& file);

ModDescriptor toModDescriptor(const DescriptorDocument& document);

// Canonical document for a ModDescriptor: version, tags, name, dependencies,
// picture, supported_version, extra (file order), path, archive,
// remote_file_id. Empty values and lists are omitted.
DescriptorDocument toDocument(const ModDescriptor& descriptor);

// Deterministic text in the style the Paradox launcher writes: one key per
// line, values quoted, lists as key={\n\t"item"\n}, LF line ends, no trailing
// newline.
std::string writeDescriptor(const DescriptorDocument& document);
std::string writeDescriptor(const ModDescriptor& descriptor);

struct DescriptorLocation {
    std::string path;    // generic absolute content folder, or empty
    std::string archive; // generic absolute archive, or empty
    std::string remoteFileId;
};

// Drops path, archive and remote_file_id and appends the new values at the
// end (path/archive first, then remote_file_id), which is how the Paradox
// launcher registers Workshop items. Everything else keeps its order.
DescriptorDocument relocateDescriptor(DescriptorDocument document,
                                      const DescriptorLocation& location);

// Unknown list values in ModDescriptor::extra.
bool isListExtraKey(std::string_view key) noexcept;
std::string encodeListValue(std::span<const std::string> items);
std::vector<std::string> decodeListValue(std::string_view encoded);

// Replaces every ill-formed UTF-8 sequence with U+FFFD.
std::string sanitizeUtf8(std::string_view text);
bool isValidUtf8(std::string_view text) noexcept;

} // namespace pmm::games
