#pragma once

// Small structured logger. Records carry a level, a component ("games.stellaris",
// "providers.workshop", ...) and key/value fields. The application installs a
// file sink in the per-user log directory; tests install a capturing sink.
//
// Never log secrets. Paths may be logged locally, but anything exported for
// diagnostics must go through redact().

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pmm {

enum class LogLevel { Debug, Info, Warning, Error };
const char* toString(LogLevel level) noexcept;

struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string component;
    std::string message;
    std::vector<std::pair<std::string, std::string>> fields;
    std::int64_t timestampMs = 0;
};

using LogSink = std::function<void(const LogRecord&)>;

// Replaces all sinks. Thread-safe. Pass an empty function to silence logging.
void setLogSink(LogSink sink);
void setMinimumLogLevel(LogLevel level) noexcept;

void log(LogLevel level, std::string_view component, std::string_view message,
         std::initializer_list<std::pair<std::string_view, std::string_view>> fields = {});

inline void logDebug(std::string_view c, std::string_view m,
                     std::initializer_list<std::pair<std::string_view, std::string_view>> f = {}) {
    log(LogLevel::Debug, c, m, f);
}
inline void logInfo(std::string_view c, std::string_view m,
                    std::initializer_list<std::pair<std::string_view, std::string_view>> f = {}) {
    log(LogLevel::Info, c, m, f);
}
inline void logWarning(std::string_view c, std::string_view m,
                       std::initializer_list<std::pair<std::string_view, std::string_view>> f = {}) {
    log(LogLevel::Warning, c, m, f);
}
inline void logError(std::string_view c, std::string_view m,
                     std::initializer_list<std::pair<std::string_view, std::string_view>> f = {}) {
    log(LogLevel::Error, c, m, f);
}

// One line, e.g. `2026-10-29T11:22:33.123Z INFO games.stellaris scan finished count=27`.
std::string formatLogRecord(const LogRecord& record);

// Redaction for exported diagnostics: replaces the user's home directory with
// "~", the user name with "<user>", and strips anything that looks like a
// credential (key=..., token=..., password=...). Home/user default to the
// current process environment when empty.
struct RedactionContext {
    std::string homeDirectory;
    std::string userName;
};
RedactionContext currentRedactionContext();
std::string redact(std::string_view text, const RedactionContext& context);
std::string redact(std::string_view text);

std::int64_t nowUnixMs() noexcept;

} // namespace pmm
