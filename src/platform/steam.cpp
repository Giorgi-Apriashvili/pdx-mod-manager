#include "platform/steam.hpp"

#include "platform/detail/os_util.hpp"
#include "platform/fs.hpp"

#include <windows.h>
#include <winreg.h>

#include <algorithm>
#include <charconv>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace pmm::platform::steam {
namespace {

using detail::quotedName;

constexpr std::size_t kMaxNestingDepth = 256;
constexpr std::uint64_t kMaxSteamFileBytes = 64ull * 1024 * 1024;

char asciiLower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool isDecimal(std::string_view text, std::size_t maxDigits = 20) noexcept {
    if (text.empty() || text.size() > maxDigits)
        return false;
    return std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

template <class Int> Int parseNumber(std::string_view text) noexcept {
    Int value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc() && end == text.data() + text.size() ? value : Int{};
}

Error keyValuesError(int line, std::string_view reason) {
    std::string where = "line " + std::to_string(line) + ": " + std::string(reason);
    return Error(ErrorCode::ParseError, "The Steam data is malformed (" + where + ").", where);
}

enum class TokenKind { String, OpenBrace, CloseBrace, Condition, End };

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    int line = 0;
};

class Lexer {
public:
    explicit Lexer(std::string_view text) : m_text(text) {
        if (m_text.substr(0, 3) == "\xEF\xBB\xBF")
            m_text.remove_prefix(3);
    }

    Result<Token> next() {
        skipSpaceAndComments();
        Token token;
        token.line = m_line;
        if (m_pos >= m_text.size())
            return token;
        const char c = m_text[m_pos];
        if (c == '{' || c == '}') {
            ++m_pos;
            token.kind = c == '{' ? TokenKind::OpenBrace : TokenKind::CloseBrace;
            return token;
        }
        if (c == '"')
            return quoted(std::move(token));
        if (c == '[') {
            const std::size_t close = m_text.find(']', m_pos);
            const std::size_t newline = m_text.find('\n', m_pos);
            if (close == std::string_view::npos || newline < close)
                return keyValuesError(m_line, "unterminated [condition]");
            token.kind = TokenKind::Condition;
            token.text = std::string(m_text.substr(m_pos, close + 1 - m_pos));
            m_pos = close + 1;
            return token;
        }
        token.kind = TokenKind::String;
        const std::size_t start = m_pos;
        while (m_pos < m_text.size() && !isSpace(m_text[m_pos]) && m_text[m_pos] != '"' &&
               m_text[m_pos] != '{' && m_text[m_pos] != '}')
            ++m_pos;
        token.text = std::string(m_text.substr(start, m_pos - start));
        return token;
    }

private:
    static bool isSpace(char c) noexcept {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
    }

    void skipSpaceAndComments() noexcept {
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos];
            if (c == '\n') {
                ++m_line;
                ++m_pos;
            } else if (isSpace(c)) {
                ++m_pos;
            } else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
                while (m_pos < m_text.size() && m_text[m_pos] != '\n')
                    ++m_pos;
            } else {
                break;
            }
        }
    }

    Result<Token> quoted(Token token) {
        token.kind = TokenKind::String;
        ++m_pos; // opening quote
        for (;;) {
            if (m_pos >= m_text.size())
                return keyValuesError(token.line, "unterminated quoted string");
            const char c = m_text[m_pos++];
            if (c == '"')
                return token;
            if (c == '\\' && m_pos < m_text.size()) {
                const char escaped = m_text[m_pos];
                const char decoded = escaped == '\\'  ? '\\'
                                     : escaped == '"' ? '"'
                                     : escaped == 'n' ? '\n'
                                     : escaped == 't' ? '\t'
                                                      : '\0';
                if (decoded != '\0') {
                    token.text += decoded;
                    ++m_pos;
                    continue;
                }
            }
            if (c == '\r' && m_pos < m_text.size() && m_text[m_pos] == '\n')
                continue; // CRLF inside a quoted string reads as LF
            if (c == '\n')
                ++m_line;
            token.text += c;
        }
    }

    std::string_view m_text;
    std::size_t m_pos = 0;
    int m_line = 1;
};

std::optional<std::wstring> readRegistryString(HKEY root, const wchar_t* subKey,
                                               const wchar_t* valueName) {
    DWORD size = 0;
    if (RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, nullptr, nullptr, &size) !=
            ERROR_SUCCESS ||
        size == 0)
        return std::nullopt;
    std::wstring buffer(size / sizeof(wchar_t) + 1, L'\0');
    size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
    if (RegGetValueW(root, subKey, valueName, RRF_RT_REG_SZ, nullptr, buffer.data(), &size) !=
        ERROR_SUCCESS)
        return std::nullopt;
    buffer.resize(size / sizeof(wchar_t));
    while (!buffer.empty() && buffer.back() == L'\0')
        buffer.pop_back();
    if (buffer.empty())
        return std::nullopt;
    return buffer;
}

std::optional<std::uint32_t> readRegistryDword(HKEY root, const wchar_t* subKey,
                                               const wchar_t* valueName) {
    DWORD value = 0;
    DWORD size = sizeof value;
    if (RegGetValueW(root, subKey, valueName, RRF_RT_REG_DWORD, nullptr, &value, &size) !=
        ERROR_SUCCESS)
        return std::nullopt;
    return static_cast<std::uint32_t>(value);
}

// Comparison key for library paths: resolved where possible, no trailing
// separator, case-folded on Windows.
std::string libraryKey(const fs::path& path) {
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(path, ec);
    if (ec || resolved.empty())
        resolved = path.lexically_normal();
    std::string key = toGenericUtf8(resolved);
    while (key.size() > 1 && key.back() == '/')
        key.pop_back();
    std::transform(key.begin(), key.end(), key.begin(), asciiLower);
    return key;
}

} // namespace

// ---- KeyValues --------------------------------------------------------------

bool keyEquals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (asciiLower(a[i]) != asciiLower(b[i]))
            return false;
    }
    return true;
}

const KeyValue* KeyValue::child(std::string_view name) const noexcept {
    for (const KeyValue& node : children) {
        if (keyEquals(node.key, name))
            return &node;
    }
    return nullptr;
}

const KeyValue* KeyValue::find(std::initializer_list<std::string_view> path) const noexcept {
    const KeyValue* node = this;
    for (const std::string_view name : path) {
        node = node->child(name);
        if (node == nullptr)
            return nullptr;
    }
    return node;
}

std::optional<std::string> KeyValue::childValue(std::string_view name) const {
    const KeyValue* node = child(name);
    if (node == nullptr || node->isBlock)
        return std::nullopt;
    return node->value;
}

std::string KeyValue::childValueOr(std::string_view name, std::string_view fallback) const {
    const KeyValue* node = child(name);
    if (node == nullptr || node->isBlock)
        return std::string(fallback);
    return node->value;
}

Result<KeyValue> parseKeyValues(std::string_view text) {
    Lexer lexer(text);
    KeyValue root;
    root.isBlock = true;
    // Iterative so hostile nesting cannot exhaust the stack. Pointers stay
    // valid because only the innermost block's children vector grows.
    std::vector<KeyValue*> stack{&root};
    for (;;) {
        Result<Token> next = lexer.next();
        if (!next)
            return std::move(next).error();
        Token key = std::move(next).value();
        switch (key.kind) {
        case TokenKind::End:
            if (stack.size() > 1)
                return keyValuesError(key.line, "missing '}' for \"" + stack.back()->key +
                                                    "\" opened on line " +
                                                    std::to_string(stack.back()->line));
            return root;
        case TokenKind::CloseBrace:
            if (stack.size() == 1)
                return keyValuesError(key.line, "unexpected '}'");
            stack.pop_back();
            continue;
        case TokenKind::OpenBrace:
            return keyValuesError(key.line, "'{' without a key");
        case TokenKind::Condition:
            continue; // trailing [$CONDITION] after a value
        case TokenKind::String:
            break;
        }

        Result<Token> afterKey = lexer.next();
        if (!afterKey)
            return std::move(afterKey).error();
        Token value = std::move(afterKey).value();
        if (value.kind == TokenKind::Condition) { // "key" [$COND] { ... }
            afterKey = lexer.next();
            if (!afterKey)
                return std::move(afterKey).error();
            value = std::move(afterKey).value();
        }

        KeyValue node;
        node.key = std::move(key.text);
        node.line = key.line;
        if (value.kind == TokenKind::String) {
            node.value = std::move(value.text);
            stack.back()->children.push_back(std::move(node));
        } else if (value.kind == TokenKind::OpenBrace) {
            if (stack.size() > kMaxNestingDepth)
                return keyValuesError(value.line, "blocks are nested too deeply");
            node.isBlock = true;
            stack.back()->children.push_back(std::move(node));
            stack.push_back(&stack.back()->children.back());
        } else {
            return keyValuesError(node.line, "\"" + node.key + "\" has no value");
        }
    }
}

Result<KeyValue> readKeyValuesFile(const fs::path& path) {
    Result<std::string> text = readFile(path, kMaxSteamFileBytes);
    if (!text)
        return std::move(text).error();
    Result<KeyValue> parsed = parseKeyValues(*text);
    if (!parsed) {
        Error error = std::move(parsed).error();
        return Error(error.code,
                     "The Steam file " + quotedName(path) + " is malformed (" + error.detail + ").",
                     pathToUtf8(path) + ": " + error.detail);
    }
    return parsed;
}

// ---- Steam root and libraries -----------------------------------------------

std::vector<fs::path> defaultSteamRootCandidates() {
    std::vector<fs::path> candidates;
    if (auto path = readRegistryString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"))
        candidates.push_back(fs::path(*path).make_preferred());
    if (auto path = readRegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
                                       L"InstallPath"))
        candidates.push_back(fs::path(*path).make_preferred());
    candidates.emplace_back(L"C:\\Program Files (x86)\\Steam");
    return candidates;
}

bool looksLikeSteamRoot(const fs::path& directory) {
    std::error_code ec;
    return !directory.empty() && fs::is_directory(directory / "steamapps", ec);
}

Result<fs::path> findSteamRoot(const std::vector<fs::path>& candidates) {
    std::string checked;
    for (const fs::path& candidate : candidates) {
        if (candidate.empty())
            continue;
        if (looksLikeSteamRoot(candidate)) {
            std::error_code ec;
            fs::path resolved = fs::weakly_canonical(candidate, ec);
            return ec || resolved.empty() ? candidate.lexically_normal() : resolved;
        }
        if (!checked.empty())
            checked += "; ";
        checked += pathToUtf8(candidate);
    }
    return makeError(ErrorCode::NotFound,
                     "Steam was not found. Install Steam, or choose the game folder manually.",
                     "checked: " + checked);
}

Result<fs::path> findSteamRoot() {
    return findSteamRoot(defaultSteamRootCandidates());
}

Result<std::vector<LibraryFolder>> readLibraryFolders(const fs::path& steamRoot) {
    const fs::path file = steamRoot / "steamapps" / "libraryfolders.vdf";
    std::error_code ec;
    if (!fs::exists(file, ec))
        return std::vector<LibraryFolder>{LibraryFolder{steamRoot, {}, {}}};
    Result<KeyValue> document = readKeyValuesFile(file);
    if (!document)
        return std::move(document).error();
    return parseLibraryFolders(*document, steamRoot);
}

Result<std::vector<LibraryFolder>> parseLibraryFolders(const KeyValue& document,
                                                       const fs::path& steamRoot) {
    const KeyValue* top = document.child("libraryfolders");
    if (top == nullptr || !top->isBlock)
        return makeError(ErrorCode::ParseError,
                         "The Steam library list (libraryfolders.vdf) has an unexpected format.",
                         "missing \"libraryfolders\" block");

    std::vector<LibraryFolder> libraries;
    std::vector<std::string> keys;
    libraries.push_back(LibraryFolder{steamRoot, {}, {}});
    keys.push_back(libraryKey(steamRoot));
    for (const KeyValue& entry : top->children) {
        // Library entries have numeric keys; skip "TimeNextStatsReport" etc.
        if (!isDecimal(entry.key, 9))
            continue;
        LibraryFolder library;
        if (entry.isBlock) {
            const std::string path = entry.childValueOr("path");
            if (path.empty())
                continue;
            library.path = pathFromUtf8(path);
            library.label = entry.childValueOr("label");
            if (const KeyValue* apps = entry.child("apps"); apps != nullptr && apps->isBlock) {
                for (const KeyValue& app : apps->children) {
                    if (isValidAppId(app.key))
                        library.appIds.push_back(app.key);
                }
            }
        } else {
            if (entry.value.empty())
                continue;
            library.path = pathFromUtf8(entry.value);
        }

        const std::string key = libraryKey(library.path);
        const auto duplicate = std::find(keys.begin(), keys.end(), key);
        if (duplicate != keys.end()) {
            LibraryFolder& existing = libraries[static_cast<std::size_t>(duplicate - keys.begin())];
            if (existing.appIds.empty())
                existing.appIds = std::move(library.appIds);
            if (existing.label.empty())
                existing.label = std::move(library.label);
            continue;
        }
        keys.push_back(key);
        libraries.push_back(std::move(library));
    }
    return libraries;
}

// ---- App manifests ----------------------------------------------------------

fs::path AppManifest::installPath() const {
    return libraryRoot / "steamapps" / "common" / pathFromUtf8(installDir);
}

bool isValidAppId(std::string_view appId) noexcept {
    return isDecimal(appId);
}

Result<AppManifest> readAppManifest(const fs::path& acfPath) {
    Result<KeyValue> document = readKeyValuesFile(acfPath);
    if (!document)
        return std::move(document).error();
    const KeyValue* state = document->child("AppState");
    if (state == nullptr || !state->isBlock)
        return makeError(ErrorCode::ParseError,
                         quotedName(acfPath) + " is not a Steam app manifest.",
                         pathToUtf8(acfPath));

    AppManifest manifest;
    manifest.appId = state->childValueOr("appid");
    if (!isValidAppId(manifest.appId))
        return makeError(ErrorCode::ParseError,
                         quotedName(acfPath) + " does not contain a valid app id.",
                         pathToUtf8(acfPath));
    manifest.name = state->childValueOr("name");
    manifest.installDir = state->childValueOr("installdir");
    manifest.buildId = state->childValueOr("buildid");
    manifest.stateFlags = parseNumber<std::uint32_t>(state->childValueOr("StateFlags"));
    manifest.lastUpdated = parseNumber<std::int64_t>(state->childValueOr("LastUpdated"));
    manifest.sizeOnDisk = parseNumber<std::uint64_t>(state->childValueOr("SizeOnDisk"));
    manifest.manifestPath = acfPath;
    manifest.libraryRoot = acfPath.parent_path().parent_path();

    // installdir must be a single folder name below steamapps/common.
    const std::string& dir = manifest.installDir;
    if (dir.empty() || dir == "." || dir == ".." ||
        dir.find_first_of(std::string_view("/\\:\0", 4)) != std::string::npos)
        return makeError(ErrorCode::ValidationFailed,
                         quotedName(acfPath) + " has an invalid install folder.",
                         pathToUtf8(acfPath) + ": installdir=\"" + dir + "\"");
    return manifest;
}

Result<AppManifest> findInstalledApp(const std::vector<LibraryFolder>& libraries,
                                     std::string_view appId) {
    if (!isValidAppId(appId))
        return makeError(ErrorCode::InvalidArgument, "Invalid Steam app id.", std::string(appId));
    const std::string fileName = "appmanifest_" + std::string(appId) + ".acf";

    std::vector<AppManifest> found;
    std::optional<Error> firstError;
    for (const LibraryFolder& library : libraries) {
        const fs::path acf = library.path / "steamapps" / fileName;
        std::error_code ec;
        if (!fs::is_regular_file(acf, ec))
            continue;
        Result<AppManifest> manifest = readAppManifest(acf);
        if (!manifest) {
            if (!firstError)
                firstError = manifest.error();
            continue;
        }
        if (manifest->appId == appId)
            found.push_back(std::move(manifest).value());
    }
    if (found.empty()) {
        if (firstError)
            return *firstError;
        return makeError(ErrorCode::NotFound, "Steam app " + std::string(appId) +
                                                  " is not installed in any Steam library.");
    }

    const auto score = [](const AppManifest& m) {
        std::error_code ec;
        if (!m.fullyInstalled())
            return 0;
        return fs::is_directory(m.installPath(), ec) ? 2 : 1;
    };
    std::size_t best = 0;
    int bestScore = score(found[0]);
    for (std::size_t i = 1; i < found.size(); ++i) {
        const int s = score(found[i]);
        if (s > bestScore) {
            best = i;
            bestScore = s;
        }
    }
    return std::move(found[best]);
}

fs::path workshopContentDir(const fs::path& libraryRoot, std::string_view appId) {
    return libraryRoot / "steamapps" / "workshop" / "content" / std::string(appId);
}

fs::path appWorkshopManifestPath(const fs::path& libraryRoot, std::string_view appId) {
    return libraryRoot / "steamapps" / "workshop" / ("appworkshop_" + std::string(appId) + ".acf");
}

// ---- Workshop manifests -----------------------------------------------------

const WorkshopItemState* AppWorkshopManifest::findItem(std::string_view id) const noexcept {
    for (const WorkshopItemState& item : items) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

Result<AppWorkshopManifest> parseAppWorkshopManifest(const KeyValue& document) {
    const KeyValue* root = document.child("AppWorkshop");
    if (root == nullptr || !root->isBlock)
        return makeError(ErrorCode::ParseError, "This is not a Steam Workshop manifest.",
                         "missing \"AppWorkshop\" block");

    AppWorkshopManifest manifest;
    manifest.appId = root->childValueOr("appid");
    manifest.sizeOnDisk = parseNumber<std::uint64_t>(root->childValueOr("SizeOnDisk"));
    manifest.needsUpdate = parseNumber<std::uint64_t>(root->childValueOr("NeedsUpdate")) != 0;
    manifest.needsDownload = parseNumber<std::uint64_t>(root->childValueOr("NeedsDownload")) != 0;
    manifest.timeLastUpdated = parseNumber<std::int64_t>(root->childValueOr("TimeLastUpdated"));
    manifest.lastBuildId = root->childValueOr("LastBuildID");

    std::unordered_map<std::string, std::size_t> index;
    if (const KeyValue* installed = root->child("WorkshopItemsInstalled");
        installed != nullptr && installed->isBlock) {
        for (const KeyValue& entry : installed->children) {
            if (!entry.isBlock || !isDecimal(entry.key) || index.count(entry.key) != 0)
                continue;
            WorkshopItemState item;
            item.id = entry.key;
            item.installed = true;
            item.size = parseNumber<std::uint64_t>(entry.childValueOr("size"));
            item.timeUpdated = parseNumber<std::int64_t>(entry.childValueOr("timeupdated"));
            item.manifest = entry.childValueOr("manifest");
            index.emplace(item.id, manifest.items.size());
            manifest.items.push_back(std::move(item));
        }
    }
    if (const KeyValue* details = root->child("WorkshopItemDetails");
        details != nullptr && details->isBlock) {
        for (const KeyValue& entry : details->children) {
            if (!entry.isBlock || !isDecimal(entry.key))
                continue;
            auto [it, inserted] = index.emplace(entry.key, manifest.items.size());
            if (inserted) {
                WorkshopItemState item;
                item.id = entry.key;
                manifest.items.push_back(std::move(item));
            }
            WorkshopItemState& item = manifest.items[it->second];
            if (item.manifest.empty())
                item.manifest = entry.childValueOr("manifest");
            if (item.timeUpdated == 0)
                item.timeUpdated = parseNumber<std::int64_t>(entry.childValueOr("timeupdated"));
            item.timeTouched = parseNumber<std::int64_t>(entry.childValueOr("timetouched"));
            item.subscribedBy = entry.childValueOr("subscribedby");
            item.latestTimeUpdated =
                parseNumber<std::int64_t>(entry.childValueOr("latest_timeupdated"));
            item.latestManifest = entry.childValueOr("latest_manifest");
        }
    }
    return manifest;
}

Result<AppWorkshopManifest> readAppWorkshopManifest(const fs::path& acfPath) {
    Result<KeyValue> document = readKeyValuesFile(acfPath);
    if (!document)
        return std::move(document).error();
    Result<AppWorkshopManifest> manifest = parseAppWorkshopManifest(*document);
    if (!manifest)
        return Error(ErrorCode::ParseError,
                     quotedName(acfPath) + " is not a Steam Workshop manifest.",
                     pathToUtf8(acfPath));
    return manifest;
}

// ---- Steam client process and runtime ---------------------------------------

bool isSteamRunning(const std::vector<RunningProcess>& processes, std::int64_t registeredPid) {
    for (const RunningProcess& process : processes) {
        if (keyEquals(process.executableName, "steam.exe"))
            return true;
        // A stale registry pid may have been reused by an unrelated program, so
        // it only counts when the name is unknown.
        if (registeredPid != 0 && process.pid == registeredPid && process.executableName.empty())
            return true;
    }
    return false;
}

bool isSteamRunning() {
    std::int64_t registeredPid = 0;
    if (auto pid =
            readRegistryDword(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid"))
        registeredPid = static_cast<std::int64_t>(*pid);
    Result<std::vector<RunningProcess>> processes = listProcesses();
    if (!processes)
        return registeredPid != 0;
    return isSteamRunning(*processes, registeredPid);
}

const char* steamApiLibraryName() noexcept {
    return "steam_api64.dll";
}

std::optional<fs::path> findSteamApiLibrary(const fs::path& gameInstallDir) {
    if (gameInstallDir.empty())
        return std::nullopt;
    const std::vector<fs::path> directories = {gameInstallDir, gameInstallDir / "bin",
                                               gameInstallDir / "binaries",
                                               gameInstallDir / "win64"};

    const std::string name = steamApiLibraryName();
    for (const fs::path& directory : directories) {
        fs::path candidate = directory / name;
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec))
            return candidate;
    }
    return std::nullopt;
}

} // namespace pmm::platform::steam
