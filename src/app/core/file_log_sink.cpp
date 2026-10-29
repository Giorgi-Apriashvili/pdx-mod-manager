#include "app/core/file_log_sink.hpp"

#include "platform/fs.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <system_error>
#include <vector>

namespace pmm::app {
namespace {

namespace fs = std::filesystem;

std::string utcDay(std::int64_t timestampMs) {
    using namespace std::chrono;
    const sys_days day = floor<days>(sys_time<milliseconds>(milliseconds(timestampMs)));
    const year_month_day ymd(day);
    char text[16];
    std::snprintf(text, sizeof text, "%04d-%02u-%02u", static_cast<int>(ymd.year()),
                  static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()));
    return text;
}

} // namespace

FileLogSink::FileLogSink(fs::path directory, std::string prefix, std::size_t keepFiles,
                         std::uint64_t maxBytes)
    : m_directory(std::move(directory)), m_prefix(std::move(prefix)),
      m_keepFiles(std::max<std::size_t>(keepFiles, 1)), m_maxBytes(maxBytes) {}

fs::path FileLogSink::fileName(const std::string& day, int part) const {
    std::string name = m_prefix + "-" + day;
    if (part > 1)
        name += "-" + std::to_string(part);
    return m_directory / platform::pathFromUtf8(name + ".log");
}

void FileLogSink::openFor(const std::string& day) {
    if (m_stream.is_open())
        m_stream.close();
    if (day != m_day) {
        m_day = day;
        m_part = 1;
    }
    // Continue an existing file of the day unless it is full.
    for (;; ++m_part) {
        const fs::path candidate = fileName(m_day, m_part);
        std::error_code ec;
        const auto size = fs::file_size(candidate, ec);
        if (ec || size < m_maxBytes) {
            m_current = candidate;
            m_bytes = ec ? 0 : size;
            break;
        }
    }
    std::error_code ec;
    fs::create_directories(m_directory, ec);
    m_stream.open(m_current, std::ios::out | std::ios::app | std::ios::binary);
    prune();
}

void FileLogSink::prune() const {
    std::vector<fs::directory_entry> logs;
    std::error_code ec;
    const std::string start = m_prefix + "-";
    for (fs::directory_iterator it(m_directory, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = platform::pathToUtf8(it->path().filename());
        if (name.size() > start.size() + 4 && name.compare(0, start.size(), start) == 0 &&
            name.compare(name.size() - 4, 4, ".log") == 0)
            logs.push_back(*it);
    }
    if (logs.size() <= m_keepFiles)
        return;
    std::sort(logs.begin(), logs.end(),
              [](const fs::directory_entry& a, const fs::directory_entry& b) {
                  std::error_code ea, eb;
                  return a.last_write_time(ea) > b.last_write_time(eb);
              });
    for (std::size_t i = m_keepFiles; i < logs.size(); ++i) {
        if (logs[i].path() == m_current)
            continue;
        std::error_code removeError;
        fs::remove(logs[i].path(), removeError);
    }
}

void FileLogSink::write(const LogRecord& record) {
    const std::string line = formatLogRecord(record) + "\n";
    std::lock_guard lock(m_mutex);
    const std::string day = utcDay(record.timestampMs != 0 ? record.timestampMs : nowUnixMs());
    if (!m_stream.is_open() || day != m_day) {
        openFor(day);
    } else if (m_bytes + line.size() > m_maxBytes) {
        ++m_part;
        openFor(day);
    }
    if (!m_stream.is_open())
        return;
    m_stream.write(line.data(), static_cast<std::streamsize>(line.size()));
    m_stream.flush();
    m_bytes += line.size();
}

fs::path FileLogSink::currentFile() const {
    std::lock_guard lock(m_mutex);
    return m_current;
}

} // namespace pmm::app
