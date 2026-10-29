#pragma once

// Filesystem primitives with explicit failure reporting. All functions accept
// paths containing spaces and non-ASCII characters on every OS (std::filesystem
// paths are converted with the native wide APIs on Windows).

#include "core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace pmm::platform {

namespace fs = std::filesystem;

// Reads a whole file. Fails with LimitExceeded when larger than maxBytes.
Result<std::string> readFile(const fs::path& path, std::uint64_t maxBytes = 64ull * 1024 * 1024);

// Atomically replaces `path` with `contents`: writes a sibling temporary file,
// flushes it to stable storage, then renames over the target (MoveFileExW with
// MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH on Windows, rename(2)
// elsewhere). Creates missing parent directories. On failure the original file
// is untouched and the temporary file is removed.
Status writeFileAtomic(const fs::path& path, std::string_view contents);

// Copies `source` to `destination` (overwriting) and flushes it.
Status copyFileDurable(const fs::path& source, const fs::path& destination);

// True if a file can be created in `directory` (probes with a temporary file).
Result<bool> isDirectoryWritable(const fs::path& directory);

// Converts between std::filesystem::path and UTF-8 strings without relying on
// the process code page.
std::string pathToUtf8(const fs::path& path);
fs::path pathFromUtf8(std::string_view utf8);

// Path with forward slashes, as written into Paradox descriptor/config files.
std::string toGenericUtf8(const fs::path& path);

// Last-write time as unix seconds; 0 if unavailable.
std::int64_t lastWriteUnixSeconds(const fs::path& path) noexcept;

// True if `child` is lexically inside `parent` after normalisation (both made
// absolute and lexically normal; does not follow symlinks).
bool isWithin(const fs::path& child, const fs::path& parent);

} // namespace pmm::platform
