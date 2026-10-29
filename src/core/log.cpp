#include "core/log.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <regex>

namespace pmm {
namespace {

std::mutex& sinkMutex() {
    static std::mutex m;
    return m;
}

LogSink& sinkRef() {
    static LogSink sink;
    return sink;
}

std::atomic<int> g_minLevel{static_cast<int>(LogLevel::Info)};

std::string envOrEmpty(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr)
        return {};
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

std::string asciiLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return s;
}

void replaceAll(std::string& text, std::string_view needle, std::string_view replacement,
                bool caseInsensitive) {
    if (needle.empty())
        return;
    const std::string hay = caseInsensitive ? asciiLower(text) : text;
    const std::string pat = caseInsensitive ? asciiLower(std::string(needle)) : std::string(needle);
    std::string out;
    out.reserve(text.size());
    std::size_t pos = 0;
    while (true) {
        const std::size_t hit = hay.find(pat, pos);
        if (hit == std::string::npos)
            break;
        out.append(text, pos, hit - pos);
        out.append(replacement);
        pos = hit + pat.size();
    }
    out.append(text, pos, std::string::npos);
    text = std::move(out);
}

} // namespace

const char* toString(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "INFO";
}

void setLogSink(LogSink sink) {
    std::lock_guard lock(sinkMutex());
    sinkRef() = std::move(sink);
}

void setMinimumLogLevel(LogLevel level) noexcept {
    g_minLevel.store(static_cast<int>(level));
}

void log(LogLevel level, std::string_view component, std::string_view message,
         std::initializer_list<std::pair<std::string_view, std::string_view>> fields) {
    if (static_cast<int>(level) < g_minLevel.load())
        return;
    LogRecord record;
    record.level = level;
    record.component = std::string(component);
    record.message = std::string(message);
    record.timestampMs = nowUnixMs();
    record.fields.reserve(fields.size());
    for (const auto& [k, v] : fields)
        record.fields.emplace_back(std::string(k), std::string(v));
    std::lock_guard lock(sinkMutex());
    if (sinkRef())
        sinkRef()(record);
}

std::string formatLogRecord(const LogRecord& record) {
    const auto seconds = static_cast<std::time_t>(record.timestampMs / 1000);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", &utc);
    std::string line = stamp;
    char millis[8];
    std::snprintf(millis, sizeof millis, ".%03dZ", static_cast<int>(record.timestampMs % 1000));
    line += millis;
    line += ' ';
    line += toString(record.level);
    line += ' ';
    line += record.component;
    line += ' ';
    line += record.message;
    for (const auto& [k, v] : record.fields) {
        line += ' ';
        line += k;
        line += '=';
        const bool quote = v.empty() || v.find_first_of(" \t\"") != std::string::npos;
        if (!quote) {
            line += v;
            continue;
        }
        line += '"';
        for (char c : v) {
            if (c == '"' || c == '\\')
                line += '\\';
            line += c;
        }
        line += '"';
    }
    return line;
}

RedactionContext currentRedactionContext() {
    RedactionContext ctx;
#if defined(_WIN32)
    ctx.homeDirectory = envOrEmpty("USERPROFILE");
    ctx.userName = envOrEmpty("USERNAME");
#else
    ctx.homeDirectory = envOrEmpty("HOME");
    ctx.userName = envOrEmpty("USER");
#endif
    return ctx;
}

std::string redact(std::string_view text, const RedactionContext& context) {
    std::string out(text);
    // Credentials first, so a secret that contains the user name is still removed.
    static const std::regex kSecret(
        R"((password|passwd|pwd|token|secret|api[_-]?key|auth|session)(\s*[=:]\s*)("[^"]*"|[^\s&;,]+))",
        std::regex::icase);
    out = std::regex_replace(out, kSecret, "$1$2<redacted>");

    if (!context.homeDirectory.empty()) {
#if defined(_WIN32)
        constexpr bool caseInsensitive = true;
#else
        constexpr bool caseInsensitive = false;
#endif
        std::string home = context.homeDirectory;
        replaceAll(out, home, "~", caseInsensitive);
        std::replace(home.begin(), home.end(), '\\', '/');
        replaceAll(out, home, "~", caseInsensitive);
        std::replace(home.begin(), home.end(), '/', '\\');
        replaceAll(out, home, "~", caseInsensitive);
    }
    // Very short user names would redact unrelated words.
    if (context.userName.size() >= 3)
        replaceAll(out, context.userName, "<user>", true);
    return out;
}

std::string redact(std::string_view text) {
    return redact(text, currentRedactionContext());
}

std::int64_t nowUnixMs() noexcept {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace pmm
