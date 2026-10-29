#pragma once

// Log sink writing formatLogRecord() lines to one file per UTC day in the
// application's log folder ("pmm-app-2026-10-29.log"). A day's file that grows
// past maxBytes continues in "pmm-app-2026-10-29-2.log", and so on. When a new
// file is started, only the newest keepFiles log files of this prefix are kept.
// Thread-safe; write failures are ignored (logging must never break the app).

#include "core/log.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace pmm::app {

class FileLogSink {
public:
    static constexpr std::size_t kDefaultKeepFiles = 10;
    static constexpr std::uint64_t kDefaultMaxBytes = 8ull * 1024 * 1024;

    explicit FileLogSink(std::filesystem::path directory, std::string prefix = "pmm-app",
                         std::size_t keepFiles = kDefaultKeepFiles,
                         std::uint64_t maxBytes = kDefaultMaxBytes);

    void write(const LogRecord& record);
    std::filesystem::path currentFile() const;

private:
    void openFor(const std::string& day);
    void prune() const;
    std::filesystem::path fileName(const std::string& day, int part) const;

    std::filesystem::path m_directory;
    std::string m_prefix;
    std::size_t m_keepFiles;
    std::uint64_t m_maxBytes;
    mutable std::mutex m_mutex;
    std::ofstream m_stream;
    std::filesystem::path m_current;
    std::string m_day;
    int m_part = 1;
    std::uint64_t m_bytes = 0;
};

} // namespace pmm::app
