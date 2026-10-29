#include "games/descriptor.hpp"

#include "platform/fs.hpp"
#include "platform/steam.hpp"

#include <cstdint>
#include <utility>

namespace pmm::games {
namespace {

using platform::steam::keyEquals;

constexpr std::uint64_t kMaxDescriptorBytes = 1024 * 1024;
constexpr std::string_view kReplacementCharacter = "\xEF\xBF\xBD";

enum class TokenKind { Text, Equals, Open, Close, End };

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    int line = 0;
};

Error parseError(int line, std::string_view reason) {
    std::string where = "line " + std::to_string(line) + ": " + std::string(reason);
    return Error(ErrorCode::ParseError, "The mod descriptor is malformed (" + where + ").", where);
}

bool isSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

bool endsBareToken(char c) noexcept {
    return isSpace(c) || c == '=' || c == '{' || c == '}' || c == '"' || c == '#';
}

// Splits the whole text into tokens; descriptors are small (<= 1 MiB).
Result<std::vector<Token>> tokenize(std::string_view text) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix(3);
    std::vector<Token> tokens;
    std::size_t pos = 0;
    int line = 1;
    while (pos < text.size()) {
        const char c = text[pos];
        if (c == '\n') {
            ++line;
            ++pos;
        } else if (isSpace(c)) {
            ++pos;
        } else if (c == '#') {
            while (pos < text.size() && text[pos] != '\n')
                ++pos;
        } else if (c == '=' || c == '{' || c == '}') {
            const TokenKind kind = c == '='   ? TokenKind::Equals
                                   : c == '{' ? TokenKind::Open
                                              : TokenKind::Close;
            tokens.push_back(Token{kind, {}, line});
            ++pos;
        } else if (c == '"') {
            Token token{TokenKind::Text, {}, line};
            ++pos;
            for (;;) {
                if (pos >= text.size())
                    return parseError(token.line, "unterminated quoted string");
                const char ch = text[pos++];
                if (ch == '"')
                    break;
                if (ch == '\\' && pos < text.size() && (text[pos] == '"' || text[pos] == '\\')) {
                    token.text += text[pos++];
                    continue;
                }
                if (ch == '\r' && pos < text.size() && text[pos] == '\n')
                    continue; // CRLF inside a quoted string reads as LF
                if (ch == '\n')
                    ++line;
                token.text += ch;
            }
            tokens.push_back(std::move(token));
        } else {
            const std::size_t start = pos;
            while (pos < text.size() && !endsBareToken(text[pos]))
                ++pos;
            tokens.push_back(
                Token{TokenKind::Text, std::string(text.substr(start, pos - start)), line});
        }
    }
    tokens.push_back(Token{TokenKind::End, {}, line});
    return tokens;
}

// Parses the block after "key = {"; `index` points behind the '{'.
Status parseBlock(const std::vector<Token>& tokens, std::size_t& index, const Token& key,
                  DescriptorDocument& document) {
    DescriptorEntry entry{sanitizeUtf8(key.text), {}, true, key.line};
    bool nested = false;
    int depth = 1;
    for (;;) {
        const Token& token = tokens[index];
        switch (token.kind) {
        case TokenKind::End:
            return parseError(token.line, "missing '}' for \"" + entry.key + "\" opened on line " +
                                              std::to_string(key.line));
        case TokenKind::Close:
            ++index;
            if (--depth == 0) {
                if (nested)
                    document.warnings.push_back("line " + std::to_string(key.line) + ": \"" +
                                                entry.key +
                                                "\" holds nested data, which is ignored");
                else
                    document.entries.push_back(std::move(entry));
                return {};
            }
            break;
        case TokenKind::Open:
            nested = true;
            ++depth;
            ++index;
            break;
        case TokenKind::Equals:
            nested = true;
            ++index;
            break;
        case TokenKind::Text:
            if (depth == 1)
                entry.values.push_back(sanitizeUtf8(token.text));
            ++index;
            break;
        }
    }
}

// Minimal escaping that our tokenizer (and the games) read back verbatim: a
// backslash is doubled only where it would otherwise start an escape.
std::string quotedValue(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    out += '"';
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '"') {
            out += "\\\"";
        } else if (c == '\\') {
            const bool escapeNeeded =
                i + 1 == value.size() || value[i + 1] == '"' || value[i + 1] == '\\';
            out += escapeNeeded ? "\\\\" : "\\";
        } else {
            out += c;
        }
    }
    out += '"';
    return out;
}

std::string writtenKey(std::string_view key) {
    bool bare = !key.empty();
    for (const char c : key)
        bare = bare && !endsBareToken(c);
    return bare ? std::string(key) : quotedValue(key);
}

DescriptorEntry scalar(std::string key, std::string value) {
    return DescriptorEntry{std::move(key), {std::move(value)}, false, 0};
}

DescriptorEntry list(std::string key, std::vector<std::string> values) {
    return DescriptorEntry{std::move(key), std::move(values), true, 0};
}

std::string* knownScalar(ModDescriptor& d, std::string_view key) {
    if (keyEquals(key, "name"))
        return &d.name;
    if (keyEquals(key, "version"))
        return &d.version;
    if (keyEquals(key, "supported_version"))
        return &d.supportedVersion;
    if (keyEquals(key, "picture"))
        return &d.picture;
    if (keyEquals(key, "path"))
        return &d.path;
    if (keyEquals(key, "archive"))
        return &d.archive;
    if (keyEquals(key, "remote_file_id"))
        return &d.remoteFileId;
    return nullptr;
}

std::vector<std::string>* knownList(ModDescriptor& d, std::string_view key) {
    if (keyEquals(key, "tags"))
        return &d.tags;
    if (keyEquals(key, "dependencies"))
        return &d.dependencies;
    return nullptr;
}

// Length of the well-formed UTF-8 sequence starting at text[pos], or 0.
std::size_t utf8SequenceLength(std::string_view text, std::size_t pos) noexcept {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(pos);
    if (lead < 0x80)
        return 1;
    std::size_t length = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        if (lead == 0xE0)
            low = 0xA0; // no overlong forms
        if (lead == 0xED)
            high = 0x9F; // no surrogates
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        if (lead == 0xF0)
            low = 0x90;
        if (lead == 0xF4)
            high = 0x8F; // <= U+10FFFF
    } else {
        return 0;
    }
    if (pos + length > text.size())
        return 0;
    if (byte(pos + 1) < low || byte(pos + 1) > high)
        return 0;
    for (std::size_t i = 2; i < length; ++i) {
        if (byte(pos + i) < 0x80 || byte(pos + i) > 0xBF)
            return 0;
    }
    return length;
}

} // namespace

std::string sanitizeUtf8(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t length = utf8SequenceLength(text, pos);
        if (length == 0) {
            out += kReplacementCharacter;
            ++pos;
        } else {
            out.append(text.substr(pos, length));
            pos += length;
        }
    }
    return out;
}

bool isValidUtf8(std::string_view text) noexcept {
    for (std::size_t pos = 0; pos < text.size();) {
        const std::size_t length = utf8SequenceLength(text, pos);
        if (length == 0)
            return false;
        pos += length;
    }
    return true;
}

Result<DescriptorDocument> parseDescriptorDocument(std::string_view text) {
    Result<std::vector<Token>> tokenized = tokenize(text);
    if (!tokenized)
        return std::move(tokenized).error();
    const std::vector<Token>& tokens = *tokenized;

    DescriptorDocument document;
    std::size_t index = 0;
    for (;;) {
        const Token& key = tokens[index];
        switch (key.kind) {
        case TokenKind::End:
            return document;
        case TokenKind::Close:
            return parseError(key.line, "unexpected '}'");
        case TokenKind::Open:
            return parseError(key.line, "'{' without a key");
        case TokenKind::Equals:
            return parseError(key.line, "'=' without a key");
        case TokenKind::Text:
            break;
        }
        ++index;
        if (tokens[index].kind == TokenKind::Open) { // tolerated: key { ... }
            ++index;
            PMM_TRY(parseBlock(tokens, index, key, document));
            continue;
        }
        if (tokens[index].kind != TokenKind::Equals)
            return parseError(key.line, "expected '=' after \"" + sanitizeUtf8(key.text) + "\"");
        ++index;
        const Token& value = tokens[index];
        if (value.kind == TokenKind::Text) {
            document.entries.push_back(DescriptorEntry{
                sanitizeUtf8(key.text), {sanitizeUtf8(value.text)}, false, key.line});
            ++index;
        } else if (value.kind == TokenKind::Open) {
            ++index;
            PMM_TRY(parseBlock(tokens, index, key, document));
        } else {
            return parseError(key.line, "\"" + sanitizeUtf8(key.text) + "\" has no value");
        }
    }
}

Result<ModDescriptor> parseDescriptor(std::string_view text) {
    Result<DescriptorDocument> document = parseDescriptorDocument(text);
    if (!document)
        return std::move(document).error();
    return toModDescriptor(*document);
}

Result<DescriptorDocument> readDescriptorDocument(const fs::path& file) {
    Result<std::string> text = platform::readFile(file, kMaxDescriptorBytes);
    if (!text)
        return std::move(text).error();
    Result<DescriptorDocument> document = parseDescriptorDocument(*text);
    if (!document) {
        const Error& error = document.error();
        const std::string name = platform::pathToUtf8(file.filename());
        return Error(ErrorCode::ParseError,
                     "The mod descriptor \"" + name + "\" is malformed (" + error.detail + ").",
                     platform::pathToUtf8(file) + ": " + error.detail);
    }
    return document;
}

Result<ModDescriptor> readDescriptor(const fs::path& file) {
    Result<DescriptorDocument> document = readDescriptorDocument(file);
    if (!document)
        return std::move(document).error();
    return toModDescriptor(*document);
}

ModDescriptor toModDescriptor(const DescriptorDocument& document) {
    ModDescriptor d;
    for (const DescriptorEntry& entry : document.entries) {
        if (std::vector<std::string>* items = knownList(d, entry.key)) {
            items->insert(items->end(), entry.values.begin(), entry.values.end());
            continue;
        }
        if (!entry.isList) {
            if (std::string* field = knownScalar(d, entry.key))
                *field = entry.values.front();
            else
                d.extra.emplace_back(entry.key, entry.values.front());
            continue;
        }
        d.extra.emplace_back(entry.key + std::string(kListKeySuffix),
                             encodeListValue(entry.values));
    }
    return d;
}

DescriptorDocument toDocument(const ModDescriptor& d) {
    DescriptorDocument document;
    auto& entries = document.entries;
    const auto addScalar = [&](const char* key, const std::string& value) {
        if (!value.empty())
            entries.push_back(scalar(key, value));
    };
    const auto addList = [&](const char* key, const std::vector<std::string>& values) {
        if (!values.empty())
            entries.push_back(list(key, values));
    };
    addScalar("version", d.version);
    addList("tags", d.tags);
    addScalar("name", d.name);
    addList("dependencies", d.dependencies);
    addScalar("picture", d.picture);
    addScalar("supported_version", d.supportedVersion);
    for (const auto& [key, value] : d.extra) {
        if (isListExtraKey(key))
            entries.push_back(
                list(key.substr(0, key.size() - kListKeySuffix.size()), decodeListValue(value)));
        else
            entries.push_back(scalar(key, value));
    }
    addScalar("path", d.path);
    addScalar("archive", d.archive);
    addScalar("remote_file_id", d.remoteFileId);
    return document;
}

std::string writeDescriptor(const DescriptorDocument& document) {
    std::string out;
    for (const DescriptorEntry& entry : document.entries) {
        if (!out.empty())
            out += '\n';
        out += writtenKey(entry.key);
        out += '=';
        if (!entry.isList) {
            out += quotedValue(entry.values.empty() ? std::string_view() : entry.values.front());
            continue;
        }
        out += '{';
        for (const std::string& value : entry.values) {
            out += "\n\t";
            out += quotedValue(value);
        }
        out += "\n}";
    }
    return out;
}

std::string writeDescriptor(const ModDescriptor& descriptor) {
    return writeDescriptor(toDocument(descriptor));
}

DescriptorDocument relocateDescriptor(DescriptorDocument document,
                                      const DescriptorLocation& location) {
    std::erase_if(document.entries, [](const DescriptorEntry& entry) {
        return keyEquals(entry.key, "path") || keyEquals(entry.key, "archive") ||
               keyEquals(entry.key, "remote_file_id");
    });
    if (!location.path.empty())
        document.entries.push_back(scalar("path", location.path));
    if (!location.archive.empty())
        document.entries.push_back(scalar("archive", location.archive));
    if (!location.remoteFileId.empty())
        document.entries.push_back(scalar("remote_file_id", location.remoteFileId));
    return document;
}

bool isListExtraKey(std::string_view key) noexcept {
    return key.size() > kListKeySuffix.size() &&
           key.substr(key.size() - kListKeySuffix.size()) == kListKeySuffix;
}

std::string encodeListValue(std::span<const std::string> items) {
    std::string out;
    for (const std::string& item : items) {
        if (!out.empty())
            out += ' ';
        out += quotedValue(item);
    }
    return out;
}

std::vector<std::string> decodeListValue(std::string_view encoded) {
    std::vector<std::string> items;
    Result<std::vector<Token>> tokens = tokenize(encoded);
    if (!tokens)
        return items;
    for (Token& token : *tokens) {
        if (token.kind == TokenKind::Text)
            items.push_back(std::move(token.text));
    }
    return items;
}

} // namespace pmm::games
