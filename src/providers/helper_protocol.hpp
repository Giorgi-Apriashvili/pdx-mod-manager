#pragma once

// Wire protocol between the application and pmm-steam-helper: one JSON object
// per line (UTF-8, '\n' terminated) in each direction. Shared by both sides so
// the encoding is defined exactly once.
//
//   app -> helper  {"protocol":1,"cmd":"state","ids":["123"]}
//   helper -> app  {"event":"state","id":"123","flags":5,...}
//
// The helper prints a "ready" event first (it carries the protocol version),
// then answers commands. Commands only ever carry validated published-file
// ids; the protocol deliberately has no way to pass paths, URLs, or to
// unsubscribe/delete items. See docs/steam-workshop.md for the full contract.

#include "core/model.hpp"
#include "core/result.hpp"
#include "providers/workshop_item.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace pmm::providers {

inline constexpr int kHelperProtocolVersion = 1;
inline constexpr std::size_t kMaxHelperLineBytes = std::size_t{256} * 1024;
inline constexpr std::size_t kMaxIdsPerStateCommand = 1000;

// --- Commands (application -> helper) ----------------------------------------

struct PingCommand {
    bool operator==(const PingCommand&) const = default;
};
struct StateCommand {
    std::vector<std::string> ids;
    bool operator==(const StateCommand&) const = default;
};
struct SubscribeCommand {
    std::string id;
    bool operator==(const SubscribeCommand&) const = default;
};
struct DownloadCommand {
    std::string id;
    bool highPriority = true;
    bool operator==(const DownloadCommand&) const = default;
};
struct QuitCommand {
    bool operator==(const QuitCommand&) const = default;
};

using HelperCommand =
    std::variant<PingCommand, StateCommand, SubscribeCommand, DownloadCommand, QuitCommand>;

// One line of JSON without the trailing newline. Ids are not re-validated
// here; decodeCommand() is the gatekeeper on the helper side.
std::string encodeCommand(const HelperCommand& command);

// Strict: unknown commands, a different protocol version, malformed or
// non-canonical ids and oversized lines are rejected.
Result<HelperCommand> decodeCommand(std::string_view line);

// --- Events (helper -> application) ------------------------------------------

// Why the helper could not provide a Steam session. Names are wire values.
enum class HelperFailure {
    None,
    InvalidArguments,  // "invalid_arguments"
    LibraryLoadFailed, // "library_load_failed": file missing, wrong architecture, ...
    SymbolsMissing,    // "symbols_missing": library lacks required flat API exports
    SteamNotRunning,   // "steam_not_running"
    InitFailed,        // "init_failed": Steam refused (not owned, wrong app, ...)
    NotLoggedOn,       // "not_logged_on": no user session available
};
const char* toString(HelperFailure failure) noexcept;
HelperFailure helperFailureFromString(std::string_view text) noexcept; // unknown -> InitFailed

struct ReadyEvent {
    bool ok = false;
    int protocol = kHelperProtocolVersion;
    // Valid when ok:
    bool loggedOn = false;
    bool ownsApp = false;
    std::string appId;
    std::string ugcInterface;          // e.g. "STEAMUGC_INTERFACE_VERSION016"
    std::string initFunction;          // e.g. "SteamAPI_InitSafe"
    std::int64_t subscribedCount = -1; // -1 when unknown
    // Valid when !ok:
    HelperFailure error = HelperFailure::None;
    std::string message;

    bool operator==(const ReadyEvent&) const = default;
};

// "state": the boolean fields on the wire are derived from `flags`, which is
// authoritative when decoding.
struct ItemStateEvent {
    ItemStatus status;
    bool operator==(const ItemStateEvent&) const = default;
};

struct SubscribeResultEvent {
    std::string id;
    bool ok = false;
    int result = 0; // EResult; see classifySteamResult()
    bool operator==(const SubscribeResultEvent&) const = default;
};

struct DownloadRequestedEvent {
    std::string id;
    bool accepted = false;
    bool operator==(const DownloadRequestedEvent&) const = default;
};

struct PongEvent {
    bool operator==(const PongEvent&) const = default;
};

// A command was rejected (malformed JSON, invalid id, unknown command, ...).
struct ErrorEvent {
    std::string code; // "bad_request", "invalid_id", "unknown_command", "unsupported_protocol"
    std::string message;
    std::string cmd; // command name if it could be determined
    bool operator==(const ErrorEvent&) const = default;
};

using HelperEvent = std::variant<ReadyEvent, ItemStateEvent, SubscribeResultEvent,
                                 DownloadRequestedEvent, PongEvent, ErrorEvent>;

std::string encodeEvent(const HelperEvent& event);

// Lines that are not JSON objects with a known "event" fail with ParseError;
// callers should log and skip them (future helpers may add events). A ready
// event with another protocol version fails with UnsupportedVersion.
Result<HelperEvent> decodeEvent(std::string_view line);

// Error event for a command rejected by decodeCommand(): UnsupportedVersion ->
// "unsupported_protocol", ValidationFailed -> "unknown_command",
// InvalidArgument -> "invalid_id", anything else -> "bad_request".
ErrorEvent makeErrorEvent(const Error& error, std::string_view cmd);

// Best-effort "cmd" value of a (possibly invalid) command line, for error
// replies. Empty when the line is not a JSON object with a string "cmd".
std::string peekCommandName(std::string_view line);

// --- Process contract ----------------------------------------------------------

enum class HelperExitCode : int {
    Ok = 0,
    InvalidArguments = 2,
    LibraryLoadFailed = 3,
    SymbolsMissing = 4,
    SteamNotRunning = 5,
    InitFailed = 6,
    NotLoggedOn = 7,
    OutputFailed = 8,
};
HelperExitCode exitCodeFor(HelperFailure failure) noexcept;

// Steam API library file names the helper accepts on `platform`:
// Windows steam_api64.dll / steam_api.dll, Linux libsteam_api.so,
// macOS libsteam_api.dylib. The comparison is exact (case-sensitive).
bool isAcceptedSteamApiLibraryName(std::string_view fileName, OsPlatform platform) noexcept;

// Validates helper arguments: app id canonical digits; library file name
// accepted for the current platform; file exists and is a regular file.
Status validateHelperArguments(std::string_view appId, const std::filesystem::path& library);

// Splits a byte stream into lines ('\n', optional preceding '\r' removed).
// Lines longer than the limit are dropped and counted, never buffered whole.
class LineSplitter {
public:
    explicit LineSplitter(std::size_t maxLineBytes = kMaxHelperLineBytes)
        : m_maxLineBytes(maxLineBytes) {}

    // Appends bytes and returns every line completed by them.
    std::vector<std::string> push(std::string_view bytes);
    std::size_t droppedLines() const noexcept { return m_dropped; }

private:
    std::string m_partial;
    std::size_t m_maxLineBytes;
    std::size_t m_dropped = 0;
    bool m_discarding = false;
};

} // namespace pmm::providers
