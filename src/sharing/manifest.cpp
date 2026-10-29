#include "sharing/manifest.hpp"

#include "core/catalog.hpp"
#include "sharing/utf8.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>

namespace pmm::sharing {
namespace {

using Json = nlohmann::ordered_json;

// Structural limits applied while parsing, before the DOM grows: v1 manifests
// nest three levels deep and have at most six keys per object.
constexpr std::size_t kMaxDepth = 16;
constexpr std::size_t kMaxKeysPerObject = 64;

constexpr std::array<std::string_view, 5> kRootKeys{"schemaVersion", "game", "playsetName",
                                                    "gameVersionHint", "mods"};
constexpr std::array<std::string_view, 6> kEntryKeys{"source",  "id",          "nameHint",
                                                     "enabled", "versionHint", "fingerprint"};

Error invalid(const std::string& path, std::string_view problem, std::string detail = {}) {
    return makeError(ErrorCode::ValidationFailed, path + ": " + std::string(problem),
                     std::move(detail));
}

std::string quotedSnippet(std::string_view text) {
    return "\"" + displaySnippet(text) + "\"";
}

std::string pathLabel(const std::string& path) {
    return path.empty() ? std::string("manifest") : path;
}

// SAX consumer that builds an ordered DOM while rejecting what nlohmann's own
// DOM parser would silently accept: duplicate keys (last one wins there).
class StrictDomBuilder {
public:
    using number_integer_t = Json::number_integer_t;
    using number_unsigned_t = Json::number_unsigned_t;
    using number_float_t = Json::number_float_t;
    using string_t = Json::string_t;
    using binary_t = Json::binary_t;

    bool null() { return insert(Json(nullptr)) != nullptr; }
    bool boolean(bool value) { return insert(Json(value)) != nullptr; }
    bool number_integer(number_integer_t value) { return insert(Json(value)) != nullptr; }
    bool number_unsigned(number_unsigned_t value) { return insert(Json(value)) != nullptr; }
    bool number_float(number_float_t value, const string_t&) {
        return insert(Json(value)) != nullptr;
    }
    bool string(string_t& value) { return insert(Json(std::move(value))) != nullptr; }
    bool binary(binary_t&) {
        return fail(makeError(ErrorCode::ParseError, "The share code content is not JSON."));
    }

    bool start_object(std::size_t) { return push(Json::object()); }
    bool start_array(std::size_t) { return push(Json::array()); }
    bool end_object() { return pop(); }
    bool end_array() { return pop(); }

    bool key(string_t& name) {
        Frame& frame = m_stack.back();
        if (frame.value->contains(name))
            return fail(invalid(pathLabel(frame.path), "duplicate key " + quotedSnippet(name)));
        if (frame.value->size() >= kMaxKeysPerObject)
            return fail(invalid(pathLabel(frame.path), "too many keys"));
        frame.pendingKey = std::move(name);
        return true;
    }

    bool parse_error(std::size_t position, const std::string&, const Json::exception& ex) {
        return fail(makeError(ErrorCode::ParseError,
                              "The share code content is not valid JSON.",
                              std::string(ex.what()) + " (byte " + std::to_string(position) + ")"));
    }

    bool failed() const noexcept { return m_error.has_value(); }
    Error takeError() { return std::move(*m_error); }
    Json takeRoot() { return std::move(m_root); }

private:
    struct Frame {
        Json* value;
        std::string path;
        std::string pendingKey;
    };

    bool fail(Error error) {
        if (!m_error)
            m_error = std::move(error);
        return false;
    }

    std::string childPath() const {
        if (m_stack.empty())
            return {};
        const Frame& frame = m_stack.back();
        if (frame.value->is_object()) {
            const std::string key = displaySnippet(frame.pendingKey);
            return frame.path.empty() ? key : frame.path + "." + key;
        }
        return frame.path + "[" + std::to_string(frame.value->size()) + "]";
    }

    // Pointers into parent containers stay valid: only the innermost open
    // container is ever modified.
    Json* insert(Json&& value) {
        if (m_stack.empty()) {
            m_root = std::move(value);
            return &m_root;
        }
        Json& container = *m_stack.back().value;
        if (container.is_object()) {
            Json& slot = container[m_stack.back().pendingKey];
            slot = std::move(value);
            return &slot;
        }
        if (container.size() >= kMaxEntries) {
            fail(makeError(ErrorCode::LimitExceeded,
                           pathLabel(m_stack.back().path) + ": more than " +
                               std::to_string(kMaxEntries) + " entries"));
            return nullptr;
        }
        container.push_back(std::move(value));
        return &container.back();
    }

    bool push(Json&& container) {
        if (m_stack.size() >= kMaxDepth)
            return fail(invalid(pathLabel(childPath()), "nesting is too deep"));
        std::string path = childPath();
        Json* inserted = insert(std::move(container));
        if (!inserted)
            return false;
        m_stack.push_back(Frame{inserted, std::move(path), {}});
        return true;
    }

    bool pop() {
        m_stack.pop_back();
        return true;
    }

    Json m_root;
    std::vector<Frame> m_stack;
    std::optional<Error> m_error;
};

bool isLowerHex(std::string_view text) noexcept {
    for (const char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

Status checkText(const std::string& path, std::string_view value, std::size_t minBytes,
                 std::size_t maxBytes) {
    if (!isValidUtf8(value))
        return invalid(path, "is not valid UTF-8 text");
    if (value.size() < minBytes)
        return invalid(path, "must not be empty");
    if (value.size() > maxBytes)
        return invalid(path, "is too long (at most " + std::to_string(maxBytes) +
                                 " bytes of UTF-8, got " + std::to_string(value.size()) + ")");
    if (containsControlCharacters(value))
        return invalid(path, "must not contain control characters");
    return {};
}

Status checkGame(const std::string& game) {
    const GameCatalogEntry* entry = findGame(game);
    if (!entry)
        return invalid("game", quotedSnippet(game) +
                                   " is not a game this version of Paradox Mod Manager knows. If "
                                   "the code was created with a newer version, update the app.");
    if (!entry->adapterAvailable)
        return makeError(ErrorCode::UnsupportedGame,
                         "game: this share code is for " + std::string(entry->displayName) +
                             ", which this version of Paradox Mod Manager does not support yet.");
    return {};
}

Status checkWorkshopId(const std::string& path, std::string_view id) {
    constexpr std::string_view kUint64Max = "18446744073709551615";
    bool digitsOnly = !id.empty();
    for (const char c : id)
        digitsOnly = digitsOnly && c >= '0' && c <= '9';
    if (!digitsOnly || id.size() > kMaxWorkshopIdDigits || id.front() == '0' ||
        (id.size() == kUint64Max.size() && id > kUint64Max))
        return invalid(path,
                       "a Steam Workshop id must be a number of 1 to 20 digits without a "
                       "leading zero",
                       "got " + quotedSnippet(id));
    return {};
}

Status checkLocalId(const std::string& path, std::string_view id) {
    PMM_TRY(checkText(path, id, 1, kMaxLocalIdBytes));
    if (id == "." || id == ".." || id.find_first_of("/\\:") != std::string_view::npos)
        return invalid(path, "a local mod id must not contain '/', '\\' or ':' and must not be "
                             "\".\" or \"..\"",
                       "got " + quotedSnippet(id));
    return {};
}

Status checkEntry(const GameKey& game, std::size_t index, const ManifestEntry& entry) {
    const std::string base = "mods[" + std::to_string(index) + "]";
    if (!isSourceValidForGame(game, entry.source)) {
        const GameCatalogEntry* info = findGame(game);
        return invalid(base + ".source",
                       quotedSnippet(entry.source) + " is not a supported mod source for " +
                           std::string(info ? info->displayName : std::string_view(game)) +
                           ". If the code was created with a newer version, update the app.");
    }
    if (entry.source == source::SteamWorkshop)
        PMM_TRY(checkWorkshopId(base + ".id", entry.id));
    else
        PMM_TRY(checkLocalId(base + ".id", entry.id));
    PMM_TRY(checkText(base + ".nameHint", entry.nameHint, 0, kMaxNameHintBytes));
    PMM_TRY(checkText(base + ".versionHint", entry.versionHint, 0, kMaxVersionHintBytes));
    if (!entry.fingerprint.empty()) {
        constexpr std::string_view kPrefix = "sha256:";
        const std::string_view fp = entry.fingerprint;
        if (fp.size() != kPrefix.size() + 64 || fp.substr(0, kPrefix.size()) != kPrefix ||
            !isLowerHex(fp.substr(kPrefix.size())))
            return invalid(base + ".fingerprint",
                           "must be \"sha256:\" followed by 64 lowercase hexadecimal digits");
    }
    return {};
}

Status checkSchemaVersion(std::int64_t version) {
    if (version == kManifestSchemaVersion)
        return {};
    if (version > kManifestSchemaVersion)
        return makeError(ErrorCode::UnsupportedVersion,
                         "schemaVersion: this share code uses playset schema version " +
                             std::to_string(version) +
                             ", which was created by a newer version of Paradox Mod Manager. "
                             "Update the app to import it.");
    return makeError(ErrorCode::UnsupportedVersion,
                     "schemaVersion: playset schema version " + std::to_string(version) +
                         " is not supported (expected " +
                         std::to_string(kManifestSchemaVersion) + ").");
}

Status checkKnownKeys(const Json& object, const std::string& path,
                      std::span<const std::string_view> allowed) {
    for (auto it = object.begin(); it != object.end(); ++it) {
        const std::string& name = it.key();
        bool known = false;
        for (const auto candidate : allowed)
            known = known || candidate == name;
        if (!known)
            return invalid(pathLabel(path), "unknown field " + quotedSnippet(name) +
                                                " (not part of share-code schema version 1)");
    }
    return {};
}

// Reads a string field. Absent optional fields leave `out` untouched.
Status readString(const Json& object, std::string_view key, const std::string& path,
                  bool required, std::string& out) {
    const auto it = object.find(std::string(key));
    if (it == object.end()) {
        if (required)
            return invalid(path, "required field is missing");
        return {};
    }
    if (!it->is_string())
        return invalid(path, "must be a string");
    out = it->get_ref<const Json::string_t&>();
    return {};
}

std::string childPath(const std::string& parent, std::string_view key) {
    return parent.empty() ? std::string(key) : parent + "." + std::string(key);
}

Result<ManifestEntry> readEntry(const Json& value, std::size_t index) {
    const std::string path = "mods[" + std::to_string(index) + "]";
    if (!value.is_object())
        return invalid(path, "must be an object");
    PMM_TRY(checkKnownKeys(value, path, kEntryKeys));

    ManifestEntry entry;
    PMM_TRY(readString(value, "source", childPath(path, "source"), true, entry.source));
    PMM_TRY(readString(value, "id", childPath(path, "id"), true, entry.id));
    PMM_TRY(readString(value, "nameHint", childPath(path, "nameHint"), false, entry.nameHint));
    const auto enabled = value.find("enabled");
    if (enabled == value.end())
        return invalid(childPath(path, "enabled"), "required field is missing");
    if (!enabled->is_boolean())
        return invalid(childPath(path, "enabled"), "must be true or false");
    entry.enabled = enabled->get<bool>();
    PMM_TRY(readString(value, "versionHint", childPath(path, "versionHint"), false,
                       entry.versionHint));
    PMM_TRY(readString(value, "fingerprint", childPath(path, "fingerprint"), false,
                       entry.fingerprint));
    return entry;
}

Result<Manifest> readManifest(const Json& root) {
    if (!root.is_object())
        return invalid("manifest", "must be a JSON object");

    // schemaVersion and game come first: for a newer schema or an unsupported
    // game those are the actionable errors, not unknown fields.
    Manifest manifest;
    const auto version = root.find("schemaVersion");
    if (version == root.end())
        return invalid("schemaVersion", "required field is missing");
    if (!version->is_number_integer())
        return invalid("schemaVersion", "must be an integer");
    if (version->is_number_unsigned()) {
        const auto v = version->get<std::uint64_t>();
        PMM_TRY(checkSchemaVersion(
            v > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
                ? std::numeric_limits<std::int64_t>::max()
                : static_cast<std::int64_t>(v)));
    } else {
        PMM_TRY(checkSchemaVersion(version->get<std::int64_t>()));
    }
    manifest.schemaVersion = kManifestSchemaVersion;

    PMM_TRY(readString(root, "game", "game", true, manifest.game));
    PMM_TRY(checkGame(manifest.game));
    PMM_TRY(checkKnownKeys(root, {}, kRootKeys));
    PMM_TRY(readString(root, "playsetName", "playsetName", true, manifest.playsetName));
    PMM_TRY(readString(root, "gameVersionHint", "gameVersionHint", false,
                       manifest.gameVersionHint));

    const auto mods = root.find("mods");
    if (mods == root.end())
        return invalid("mods", "required field is missing");
    if (!mods->is_array())
        return invalid("mods", "must be an array");
    if (mods->size() > kMaxEntries)
        return makeError(ErrorCode::LimitExceeded,
                         "mods: more than " + std::to_string(kMaxEntries) + " entries");
    manifest.mods.reserve(mods->size());
    for (std::size_t i = 0; i < mods->size(); ++i) {
        auto entry = readEntry((*mods)[i], i);
        if (!entry)
            return std::move(entry).error();
        manifest.mods.push_back(std::move(entry).value());
    }

    PMM_TRY(validateManifest(manifest));
    return manifest;
}

} // namespace

Status validateManifest(const Manifest& manifest) {
    PMM_TRY(checkSchemaVersion(manifest.schemaVersion));
    PMM_TRY(checkGame(manifest.game));
    PMM_TRY(checkText("playsetName", manifest.playsetName, 1, kMaxPlaysetNameBytes));
    PMM_TRY(checkText("gameVersionHint", manifest.gameVersionHint, 0, kMaxVersionHintBytes));
    if (manifest.mods.size() > kMaxEntries)
        return makeError(ErrorCode::LimitExceeded,
                         "mods: more than " + std::to_string(kMaxEntries) + " entries");

    std::unordered_map<std::string, std::size_t> seen;
    seen.reserve(manifest.mods.size());
    for (std::size_t i = 0; i < manifest.mods.size(); ++i) {
        const ManifestEntry& entry = manifest.mods[i];
        PMM_TRY(checkEntry(manifest.game, i, entry));
        // '\n' cannot occur in a valid source or id, so the key is unambiguous.
        const auto [it, inserted] = seen.emplace(entry.source + '\n' + entry.id, i);
        if (!inserted)
            return invalid("mods[" + std::to_string(i) + "]",
                           "duplicate of mods[" + std::to_string(it->second) +
                               "] (same source and id " + quotedSnippet(entry.id) + ")");
    }
    return {};
}

Result<Manifest> parseManifestJson(std::string_view json) {
    if (json.substr(0, 3) == "\xEF\xBB\xBF")
        return makeError(ErrorCode::ParseError,
                         "The share code content is not valid: it starts with a byte order "
                         "mark.");
    if (const std::size_t bad = findInvalidUtf8(json); bad != std::string_view::npos)
        return makeError(ErrorCode::ParseError,
                         "The share code content is not valid UTF-8 text.",
                         "ill-formed UTF-8 at byte " + std::to_string(bad));

    StrictDomBuilder builder;
    bool parsed = false;
    try {
        parsed = Json::sax_parse(json.begin(), json.end(), &builder, Json::input_format_t::json,
                                 /*strict=*/true, /*ignore_comments=*/false);
    } catch (const nlohmann::json::exception& ex) {
        return makeError(ErrorCode::ParseError, "The share code content is not valid JSON.",
                         ex.what());
    } catch (const std::bad_alloc&) {
        return makeError(ErrorCode::Internal, "Out of memory while reading the share code.");
    }
    if (builder.failed())
        return builder.takeError();
    if (!parsed)
        return makeError(ErrorCode::ParseError, "The share code content is not valid JSON.");
    return readManifest(builder.takeRoot());
}

Result<std::string> serializeManifestJson(const Manifest& manifest) {
    PMM_TRY(validateManifest(manifest));
    try {
        Json root = Json::object();
        root["schemaVersion"] = manifest.schemaVersion;
        root["game"] = manifest.game;
        root["playsetName"] = manifest.playsetName;
        if (!manifest.gameVersionHint.empty())
            root["gameVersionHint"] = manifest.gameVersionHint;
        Json mods = Json::array();
        for (const ManifestEntry& entry : manifest.mods) {
            Json item = Json::object();
            item["source"] = entry.source;
            item["id"] = entry.id;
            if (!entry.nameHint.empty())
                item["nameHint"] = entry.nameHint;
            item["enabled"] = entry.enabled;
            if (!entry.versionHint.empty())
                item["versionHint"] = entry.versionHint;
            if (!entry.fingerprint.empty())
                item["fingerprint"] = entry.fingerprint;
            mods.push_back(std::move(item));
        }
        root["mods"] = std::move(mods);
        return root.dump(-1, ' ', /*ensure_ascii=*/false, Json::error_handler_t::strict);
    } catch (const nlohmann::json::exception& ex) {
        return makeError(ErrorCode::Internal, "Could not serialize the playset.", ex.what());
    }
}

} // namespace pmm::sharing
