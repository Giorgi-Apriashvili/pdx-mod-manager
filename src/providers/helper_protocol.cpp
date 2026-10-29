#include "providers/helper_protocol.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <limits>
#include <system_error>
#include <type_traits>

namespace pmm::providers {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::array<std::pair<HelperFailure, std::string_view>, 7> kFailureNames{{
    {HelperFailure::None, "none"},
    {HelperFailure::InvalidArguments, "invalid_arguments"},
    {HelperFailure::LibraryLoadFailed, "library_load_failed"},
    {HelperFailure::SymbolsMissing, "symbols_missing"},
    {HelperFailure::SteamNotRunning, "steam_not_running"},
    {HelperFailure::InitFailed, "init_failed"},
    {HelperFailure::NotLoggedOn, "not_logged_on"},
}};

std::string dumpLine(const Json& j) {
    // Replace invalid UTF-8 (e.g. a mis-encoded path from Steam) instead of
    // throwing; the protocol is always valid UTF-8.
    return j.dump(-1, ' ', false, Json::error_handler_t::replace);
}

Result<Json> parseObject(std::string_view line, std::string_view what) {
    if (line.size() > kMaxHelperLineBytes) {
        return makeError(ErrorCode::LimitExceeded,
                         std::string(what) + " line exceeds the protocol size limit.",
                         std::to_string(line.size()) + " bytes");
    }
    Json j;
    try {
        j = Json::parse(line.begin(), line.end(), nullptr, /*allow_exceptions=*/false);
    } catch (const std::exception& e) {
        return makeError(ErrorCode::ParseError, std::string(what) + " line is not valid JSON.",
                         e.what());
    }
    if (j.is_discarded() || !j.is_object())
        return makeError(ErrorCode::ParseError, std::string(what) + " line is not a JSON object.");
    return j;
}

const Json* findField(const Json& obj, const char* key) {
    const auto it = obj.find(key);
    return it == obj.end() ? nullptr : &*it;
}

Error fieldError(const char* key, const char* expected) {
    return makeError(ErrorCode::ParseError,
                     std::string("Field \"") + key + "\" is missing or not " + expected + ".");
}

Result<std::string> requireString(const Json& obj, const char* key) {
    const Json* f = findField(obj, key);
    if (!f || !f->is_string())
        return fieldError(key, "a string");
    return f->get<std::string>();
}

std::string optionalString(const Json& obj, const char* key) {
    const Json* f = findField(obj, key);
    return f && f->is_string() ? f->get<std::string>() : std::string();
}

Result<bool> requireBool(const Json& obj, const char* key) {
    const Json* f = findField(obj, key);
    if (!f || !f->is_boolean())
        return fieldError(key, "a boolean");
    return f->get<bool>();
}

Result<bool> optionalBool(const Json& obj, const char* key, bool fallback) {
    const Json* f = findField(obj, key);
    if (!f)
        return fallback;
    if (!f->is_boolean())
        return fieldError(key, "a boolean");
    return f->get<bool>();
}

template <class T> Result<T> optionalUnsigned(const Json& obj, const char* key, T fallback) {
    static_assert(std::is_unsigned_v<T>);
    const Json* f = findField(obj, key);
    if (!f)
        return fallback;
    if (!f->is_number_unsigned())
        return fieldError(key, "an unsigned integer in range");
    const auto v = f->get<std::uint64_t>();
    if constexpr (sizeof(T) < sizeof(std::uint64_t)) {
        if (v > std::numeric_limits<T>::max())
            return fieldError(key, "an unsigned integer in range");
    }
    return static_cast<T>(v);
}

Result<std::string> requireItemId(const Json& obj, const char* key) {
    auto id = requireString(obj, key);
    if (!id)
        return id;
    if (!isValidPublishedFileId(*id)) {
        return makeError(ErrorCode::InvalidArgument,
                         "Workshop item id must be a canonical decimal number.",
                         "id=\"" + id->substr(0, 64) + "\"");
    }
    return id;
}

std::string utf8(const std::filesystem::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

Json encodeStatus(const ItemStatus& s) {
    const ItemFlags f = decodeItemFlags(s.flags);
    Json j;
    j["event"] = "state";
    j["id"] = s.id;
    j["flags"] = s.flags;
    j["subscribed"] = f.subscribed;
    j["installed"] = f.installed;
    j["downloading"] = f.downloading;
    j["downloadPending"] = f.downloadPending;
    j["needsUpdate"] = f.needsUpdate;
    j["legacy"] = f.legacy;
    j["bytesDownloaded"] = s.bytesDownloaded;
    j["bytesTotal"] = s.bytesTotal;
    j["installPath"] = s.installPath;
    j["sizeOnDisk"] = s.sizeOnDisk;
    j["timestamp"] = s.timestamp;
    return j;
}

Result<HelperEvent> decodeReady(const Json& j) {
    ReadyEvent e;
    if (const Json* p = findField(j, "protocol")) {
        if (!p->is_number_integer())
            return fieldError("protocol", "an integer");
        e.protocol = p->get<int>();
    }
    if (e.protocol != kHelperProtocolVersion) {
        return makeError(ErrorCode::UnsupportedVersion,
                         "The Steam helper speaks a different protocol version; reinstall the "
                         "application so the helper and application match.",
                         "helper protocol " + std::to_string(e.protocol) + ", expected " +
                             std::to_string(kHelperProtocolVersion));
    }
    auto ok = requireBool(j, "ok");
    if (!ok)
        return std::move(ok).error();
    e.ok = *ok;
    if (e.ok) {
        auto loggedOn = requireBool(j, "loggedOn");
        auto ownsApp = requireBool(j, "ownsApp");
        auto appId = requireString(j, "appId");
        if (!loggedOn)
            return std::move(loggedOn).error();
        if (!ownsApp)
            return std::move(ownsApp).error();
        if (!appId)
            return std::move(appId).error();
        e.loggedOn = *loggedOn;
        e.ownsApp = *ownsApp;
        e.appId = std::move(*appId);
        e.ugcInterface = optionalString(j, "ugcInterface");
        e.initFunction = optionalString(j, "init");
        if (const Json* c = findField(j, "subscribedCount"); c && c->is_number_integer())
            e.subscribedCount = c->get<std::int64_t>();
    } else {
        e.error = helperFailureFromString(optionalString(j, "error"));
        e.message = optionalString(j, "message");
    }
    return HelperEvent{std::move(e)};
}

Result<HelperEvent> decodeState(const Json& j) {
    ItemStateEvent e;
    auto id = requireItemId(j, "id");
    if (!id)
        return std::move(id).error();
    e.status.id = std::move(*id);
    const Json* flags = findField(j, "flags");
    if (!flags)
        return fieldError("flags", "an unsigned integer");
    auto f = optionalUnsigned<std::uint32_t>(j, "flags", 0);
    auto downloaded = optionalUnsigned<std::uint64_t>(j, "bytesDownloaded", 0);
    auto total = optionalUnsigned<std::uint64_t>(j, "bytesTotal", 0);
    auto size = optionalUnsigned<std::uint64_t>(j, "sizeOnDisk", 0);
    auto ts = optionalUnsigned<std::uint32_t>(j, "timestamp", 0);
    for (const Error* err :
         {f.ok() ? nullptr : &f.error(), downloaded.ok() ? nullptr : &downloaded.error(),
          total.ok() ? nullptr : &total.error(), size.ok() ? nullptr : &size.error(),
          ts.ok() ? nullptr : &ts.error()}) {
        if (err)
            return *err;
    }
    e.status.flags = *f;
    e.status.bytesDownloaded = *downloaded;
    e.status.bytesTotal = *total;
    e.status.sizeOnDisk = *size;
    e.status.timestamp = *ts;
    e.status.installPath = optionalString(j, "installPath");
    return HelperEvent{std::move(e)};
}

Result<HelperEvent> decodeSubscribeResult(const Json& j) {
    SubscribeResultEvent e;
    auto id = requireItemId(j, "id");
    if (!id)
        return std::move(id).error();
    auto ok = requireBool(j, "ok");
    if (!ok)
        return std::move(ok).error();
    const Json* r = findField(j, "result");
    if (!r || !r->is_number_integer() || r->get<std::int64_t>() < 0 ||
        r->get<std::int64_t>() > std::numeric_limits<int>::max())
        return fieldError("result", "an EResult integer");
    e.id = std::move(*id);
    e.ok = *ok;
    e.result = r->get<int>();
    return HelperEvent{std::move(e)};
}

Result<HelperEvent> decodeDownloadRequested(const Json& j) {
    DownloadRequestedEvent e;
    auto id = requireItemId(j, "id");
    if (!id)
        return std::move(id).error();
    auto accepted = requireBool(j, "accepted");
    if (!accepted)
        return std::move(accepted).error();
    e.id = std::move(*id);
    e.accepted = *accepted;
    return HelperEvent{std::move(e)};
}

} // namespace

const char* toString(HelperFailure failure) noexcept {
    for (const auto& [f, name] : kFailureNames) {
        if (f == failure)
            return name.data();
    }
    return "init_failed";
}

HelperFailure helperFailureFromString(std::string_view text) noexcept {
    for (const auto& [f, name] : kFailureNames) {
        if (name == text && f != HelperFailure::None)
            return f;
    }
    return HelperFailure::InitFailed;
}

std::string encodeCommand(const HelperCommand& command) {
    Json j;
    j["protocol"] = kHelperProtocolVersion;
    std::visit(
        [&j](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, PingCommand>) {
                j["cmd"] = "ping";
            } else if constexpr (std::is_same_v<T, StateCommand>) {
                j["cmd"] = "state";
                j["ids"] = c.ids;
            } else if constexpr (std::is_same_v<T, SubscribeCommand>) {
                j["cmd"] = "subscribe";
                j["id"] = c.id;
            } else if constexpr (std::is_same_v<T, DownloadCommand>) {
                j["cmd"] = "download";
                j["id"] = c.id;
                j["highPriority"] = c.highPriority;
            } else {
                static_assert(std::is_same_v<T, QuitCommand>);
                j["cmd"] = "quit";
            }
        },
        command);
    return dumpLine(j);
}

Result<HelperCommand> decodeCommand(std::string_view line) {
    auto parsed = parseObject(line, "Command");
    if (!parsed)
        return std::move(parsed).error();
    const Json& j = *parsed;

    if (const Json* p = findField(j, "protocol")) {
        if (!p->is_number_integer() || p->get<std::int64_t>() != kHelperProtocolVersion) {
            return makeError(ErrorCode::UnsupportedVersion, "Unsupported helper protocol version.",
                             "expected " + std::to_string(kHelperProtocolVersion));
        }
    }
    auto cmd = requireString(j, "cmd");
    if (!cmd)
        return std::move(cmd).error();

    if (*cmd == "ping")
        return HelperCommand{PingCommand{}};
    if (*cmd == "quit")
        return HelperCommand{QuitCommand{}};
    if (*cmd == "state") {
        const Json* ids = findField(j, "ids");
        if (!ids || !ids->is_array())
            return fieldError("ids", "an array of strings");
        if (ids->empty() || ids->size() > kMaxIdsPerStateCommand) {
            return makeError(ErrorCode::InvalidArgument,
                             "A state command needs between 1 and " +
                                 std::to_string(kMaxIdsPerStateCommand) + " ids.");
        }
        StateCommand c;
        c.ids.reserve(ids->size());
        for (const auto& id : *ids) {
            if (!id.is_string() || !isValidPublishedFileId(id.get<std::string>())) {
                return makeError(ErrorCode::InvalidArgument,
                                 "Workshop item id must be a canonical decimal number.");
            }
            c.ids.push_back(id.get<std::string>());
        }
        return HelperCommand{std::move(c)};
    }
    if (*cmd == "subscribe") {
        auto id = requireItemId(j, "id");
        if (!id)
            return std::move(id).error();
        return HelperCommand{SubscribeCommand{std::move(*id)}};
    }
    if (*cmd == "download") {
        auto id = requireItemId(j, "id");
        if (!id)
            return std::move(id).error();
        auto high = optionalBool(j, "highPriority", true);
        if (!high)
            return std::move(high).error();
        return HelperCommand{DownloadCommand{std::move(*id), *high}};
    }
    return makeError(ErrorCode::ValidationFailed, "Unknown helper command.",
                     "cmd=\"" + cmd->substr(0, 32) + "\"");
}

std::string encodeEvent(const HelperEvent& event) {
    Json j;
    std::visit(
        [&j](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, ReadyEvent>) {
                j["event"] = "ready";
                j["protocol"] = e.protocol;
                j["ok"] = e.ok;
                if (e.ok) {
                    j["loggedOn"] = e.loggedOn;
                    j["ownsApp"] = e.ownsApp;
                    j["appId"] = e.appId;
                    j["ugcInterface"] = e.ugcInterface;
                    j["init"] = e.initFunction;
                    if (e.subscribedCount >= 0)
                        j["subscribedCount"] = e.subscribedCount;
                } else {
                    j["error"] = toString(e.error);
                    j["message"] = e.message;
                }
            } else if constexpr (std::is_same_v<T, ItemStateEvent>) {
                j = encodeStatus(e.status);
            } else if constexpr (std::is_same_v<T, SubscribeResultEvent>) {
                j["event"] = "subscribe_result";
                j["id"] = e.id;
                j["ok"] = e.ok;
                j["result"] = e.result;
            } else if constexpr (std::is_same_v<T, DownloadRequestedEvent>) {
                j["event"] = "download_requested";
                j["id"] = e.id;
                j["accepted"] = e.accepted;
            } else if constexpr (std::is_same_v<T, PongEvent>) {
                j["event"] = "pong";
            } else {
                static_assert(std::is_same_v<T, ErrorEvent>);
                j["event"] = "error";
                j["code"] = e.code;
                j["message"] = e.message;
                if (!e.cmd.empty())
                    j["cmd"] = e.cmd;
            }
        },
        event);
    return dumpLine(j);
}

Result<HelperEvent> decodeEvent(std::string_view line) {
    auto parsed = parseObject(line, "Helper event");
    if (!parsed)
        return std::move(parsed).error();
    const Json& j = *parsed;
    auto name = requireString(j, "event");
    if (!name)
        return std::move(name).error();

    if (*name == "ready")
        return decodeReady(j);
    if (*name == "state")
        return decodeState(j);
    if (*name == "subscribe_result")
        return decodeSubscribeResult(j);
    if (*name == "download_requested")
        return decodeDownloadRequested(j);
    if (*name == "pong")
        return HelperEvent{PongEvent{}};
    if (*name == "error") {
        ErrorEvent e;
        auto code = requireString(j, "code");
        if (!code)
            return std::move(code).error();
        e.code = std::move(*code);
        e.message = optionalString(j, "message");
        e.cmd = optionalString(j, "cmd");
        return HelperEvent{std::move(e)};
    }
    return makeError(ErrorCode::ParseError, "Unknown helper event.",
                     "event=\"" + name->substr(0, 32) + "\"");
}

ErrorEvent makeErrorEvent(const Error& error, std::string_view cmd) {
    ErrorEvent e;
    switch (error.code) {
    case ErrorCode::UnsupportedVersion:
        e.code = "unsupported_protocol";
        break;
    case ErrorCode::ValidationFailed:
        e.code = "unknown_command";
        break;
    case ErrorCode::InvalidArgument:
        e.code = "invalid_id";
        break;
    default:
        e.code = "bad_request";
        break;
    }
    e.message = error.message;
    e.cmd = std::string(cmd.substr(0, 32));
    return e;
}

std::string peekCommandName(std::string_view line) {
    auto parsed = parseObject(line, "Command");
    if (!parsed)
        return {};
    return optionalString(*parsed, "cmd").substr(0, 32);
}

HelperExitCode exitCodeFor(HelperFailure failure) noexcept {
    switch (failure) {
    case HelperFailure::None:
        return HelperExitCode::Ok;
    case HelperFailure::InvalidArguments:
        return HelperExitCode::InvalidArguments;
    case HelperFailure::LibraryLoadFailed:
        return HelperExitCode::LibraryLoadFailed;
    case HelperFailure::SymbolsMissing:
        return HelperExitCode::SymbolsMissing;
    case HelperFailure::SteamNotRunning:
        return HelperExitCode::SteamNotRunning;
    case HelperFailure::InitFailed:
        return HelperExitCode::InitFailed;
    case HelperFailure::NotLoggedOn:
        return HelperExitCode::NotLoggedOn;
    }
    return HelperExitCode::InitFailed;
}

bool isAcceptedSteamApiLibraryName(std::string_view fileName, OsPlatform platform) noexcept {
    switch (platform) {
    case OsPlatform::Windows:
        return fileName == "steam_api64.dll" || fileName == "steam_api.dll";
    case OsPlatform::Linux:
        return fileName == "libsteam_api.so";
    case OsPlatform::MacOS:
        return fileName == "libsteam_api.dylib";
    }
    return false;
}

Status validateHelperArguments(std::string_view appId, const std::filesystem::path& library) {
    if (!isValidSteamAppId(appId)) {
        return makeError(ErrorCode::InvalidArgument,
                         "--app-id must be a Steam AppId (decimal digits only).",
                         "app-id=\"" + std::string(appId.substr(0, 32)) + "\"");
    }
    const std::string name = utf8(library.filename());
    if (!isAcceptedSteamApiLibraryName(name, currentOsPlatform())) {
        return makeError(ErrorCode::InvalidArgument,
                         "--steam-api must point at the game's own Steam API library "
                         "(steam_api64.dll, steam_api.dll, libsteam_api.so or libsteam_api.dylib "
                         "depending on the platform).",
                         "file name \"" + name + "\"");
    }
    std::error_code ec;
    const auto status = std::filesystem::status(library, ec);
    if (ec || !std::filesystem::exists(status)) {
        return makeError(ErrorCode::NotFound,
                         "The game's Steam API library was not found; verify the game files in "
                         "Steam.",
                         utf8(library) + (ec ? ": " + ec.message() : std::string()));
    }
    if (!std::filesystem::is_regular_file(status)) {
        return makeError(ErrorCode::InvalidArgument, "--steam-api must point at a regular file.",
                         utf8(library));
    }
    return {};
}

std::vector<std::string> LineSplitter::push(std::string_view bytes) {
    std::vector<std::string> lines;
    while (!bytes.empty()) {
        const std::size_t nl = bytes.find('\n');
        const std::string_view piece = bytes.substr(0, nl);
        if (!m_discarding) {
            if (m_partial.size() + piece.size() > m_maxLineBytes) {
                m_discarding = true;
                ++m_dropped;
                m_partial.clear();
                m_partial.shrink_to_fit();
            } else {
                m_partial.append(piece);
            }
        }
        if (nl == std::string_view::npos)
            break;
        bytes.remove_prefix(nl + 1);
        if (!m_discarding) {
            if (!m_partial.empty() && m_partial.back() == '\r')
                m_partial.pop_back();
            if (!m_partial.empty())
                lines.push_back(std::move(m_partial));
            m_partial.clear();
        }
        m_discarding = false;
    }
    return lines;
}

} // namespace pmm::providers
