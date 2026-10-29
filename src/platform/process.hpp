#pragma once

// Process creation and inspection through native OS APIs. Arguments are always
// passed as a vector and quoted per platform rules (CreateProcessW command-line
// quoting on Windows, posix_spawn argv elsewhere); nothing is ever routed
// through a shell.

#include "core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace pmm::platform {

namespace fs = std::filesystem;

struct ProcessSpec {
    fs::path executable;  // absolute path; must exist and be a regular file
    fs::path workingDirectory;
    std::vector<std::string> arguments; // argv[1..], UTF-8
    std::vector<std::pair<std::string, std::string>> environment; // added to the inherited env
};

struct StartedProcess {
    std::int64_t pid = 0;
};

// Starts a detached process (does not wait, does not inherit handles/pipes).
// Fails with ProcessStartFailed including the OS error text in `detail`.
Result<StartedProcess> startDetached(const ProcessSpec& spec);

// Windows command-line quoting for one argument (CommandLineToArgvW rules).
// Exposed for tests.
std::wstring quoteWindowsArgument(const std::wstring& argument);

struct RunningProcess {
    std::int64_t pid = 0;
    std::string executableName; // file name only, e.g. "stellaris.exe"
    fs::path executablePath;    // empty if not accessible
};

// Snapshot of running processes visible to the current user.
Result<std::vector<RunningProcess>> listProcesses();

// Case-insensitive on Windows/macOS, case-sensitive on Linux.
bool executableNameMatches(const std::string& runningName, const std::string& wanted);

} // namespace pmm::platform
