#include "games/mod_metadata.hpp"

#include "core/utf8.hpp"
#include "platform/fs.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <utility>

namespace pmm::games {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::uint64_t kMaxMetadataBytes = std::uint64_t{1024} * 1024;

// The keys mapped onto named ModDescriptor fields (or not kept at all).
constexpr std::array<std::string_view, 7> kMappedKeys{
    "name", "version",       "supported_game_version", "picture",
    "tags", "relationships", "game_custom_data"};

// Finds where a document that the DOM parser rejected goes wrong, without
// building anything.
class SyntaxErrorLocator final : public nlohmann::json_sax<Json> {
public:
    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool string(string_t&) override { return true; }
    bool binary(binary_t&) override { return true; }
    bool start_object(std::size_t) override { return true; }
    bool key(string_t&) override { return true; }
    bool end_object() override { return true; }
    bool start_array(std::size_t) override { return true; }
    bool end_array() override { return true; }
    bool parse_error(std::size_t, const std::string&, const Json::exception& error) override {
        m_what = error.what();
        return false;
    }

    // "line 3, column 1: syntax error while parsing object - ..." from
    // nlohmann's "[json.exception.parse_error.101] parse error at line 3, ...".
    std::string where() const {
        std::string_view text = m_what;
        if (const std::size_t bracket = text.find("] "); bracket != std::string_view::npos)
            text.remove_prefix(bracket + 2);
        constexpr std::string_view prefix = "parse error at ";
        if (text.substr(0, prefix.size()) == prefix)
            text.remove_prefix(prefix.size());
        return text.empty() ? std::string("JSON syntax error") : sanitizeUtf8(text);
    }

private:
    std::string m_what;
};

const std::string* stringAt(const Json& object, std::string_view key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get_ptr<const std::string*>() : nullptr;
}

void readString(const Json& object, std::string_view key, std::string& out) {
    if (const std::string* value = stringAt(object, key))
        out = *value;
}

// rel_type "dependency" objects (by display_name, else id) and the names of
// the older plain list.
std::vector<std::string> dependenciesOf(const Json& relationships) {
    std::vector<std::string> out;
    if (!relationships.is_array())
        return out;
    for (const Json& relation : relationships) {
        if (relation.is_string()) {
            if (!relation.get_ref<const std::string&>().empty())
                out.push_back(relation.get<std::string>());
            continue;
        }
        if (!relation.is_object())
            continue;
        const std::string* type = stringAt(relation, "rel_type");
        if (type == nullptr || *type != "dependency")
            continue;
        // A dependency on something other than a mod (the launcher writes
        // "resource_type": "mod") is not a mod to look for.
        const std::string* resource = stringAt(relation, "resource_type");
        if (resource != nullptr && *resource != "mod")
            continue;
        const std::string* name = stringAt(relation, "display_name");
        if (name == nullptr || name->empty())
            name = stringAt(relation, "id");
        if (name != nullptr && !name->empty())
            out.push_back(*name);
    }
    return out;
}

bool isMappedKey(std::string_view key) {
    for (const std::string_view mapped : kMappedKeys) {
        if (key == mapped)
            return true;
    }
    return false;
}

// `subject` names the metadata in messages, `where` prefixes the details.
Result<ModMetadata> parseMetadata(std::string_view text, const std::string& subject,
                                  const std::string& where) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix(3);
    const std::string clean = sanitizeUtf8(text);
    const Json document = Json::parse(clean, nullptr, /*allow_exceptions=*/false,
                                      /*ignore_comments=*/false);
    if (document.is_discarded()) {
        SyntaxErrorLocator locator;
        Json::sax_parse(clean, &locator, Json::input_format_t::json, /*strict=*/true,
                        /*ignore_comments=*/false);
        const std::string error = locator.where();
        return makeError(ErrorCode::ParseError, subject + " is not valid JSON (" + error + ").",
                         where + error);
    }
    if (!document.is_object())
        return makeError(ErrorCode::ParseError, subject + " is not a JSON object.",
                         where + "the document is a JSON " + std::string(document.type_name()));

    ModMetadata metadata;
    ModDescriptor& d = metadata.descriptor;
    readString(document, "name", d.name);
    readString(document, "version", d.version);
    readString(document, "supported_game_version", d.supportedVersion);
    readString(document, "picture", d.picture);
    if (const auto tags = document.find("tags"); tags != document.end() && tags->is_array()) {
        for (const Json& tag : *tags) {
            if (tag.is_string())
                d.tags.push_back(tag.get<std::string>());
        }
    }
    if (const auto relationships = document.find("relationships"); relationships != document.end())
        d.dependencies = dependenciesOf(*relationships);
    const auto custom = document.find("game_custom_data");
    metadata.hasGameCustomData = custom != document.end() && custom->is_object();

    for (const auto& [key, value] : document.items()) {
        if (isMappedKey(key))
            continue;
        if (value.is_string())
            d.extra.emplace_back(key, value.get<std::string>());
        else if (value.is_number() || value.is_boolean())
            d.extra.emplace_back(key, value.dump());
    }
    return metadata;
}

} // namespace

fs::path modMetadataPath(const fs::path& modFolder) {
    return modFolder / ".metadata" / "metadata.json";
}

Result<ModMetadata> parseModMetadata(std::string_view text) {
    return parseMetadata(text, "The mod metadata", {});
}

Result<ModMetadata> readModMetadata(const fs::path& file) {
    Result<std::string> text = platform::readFile(file, kMaxMetadataBytes);
    if (!text)
        return std::move(text).error();
    return parseMetadata(*text, "The mod metadata \"" + std::string(kModMetadataFile) + "\"",
                         platform::pathToUtf8(file) + ": ");
}

} // namespace pmm::games
