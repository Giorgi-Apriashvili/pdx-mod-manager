#pragma once

// Content fingerprint for local (non-Workshop) mods, carried in share codes as
// an informational hint so recipients can see whether their copy matches.
//
// Definition (stable across OSes and part of the share-code contract):
//   1. Collect every regular file below the root (directory symlinks are not
//      followed), skipping OS/VCS junk: any path component ".git" or
//      "__MACOSX", and files named ".DS_Store", "Thumbs.db", "desktop.ini"
//      (ASCII case-insensitive) or starting with "._" (AppleDouble).
//   2. Key each file by its path relative to the root, '/'-separated, UTF-8.
//   3. Sort by that key bytewise (unsigned).
//   4. SHA-256 over the concatenation, per file, of
//        key "\0" decimal-size "\0" lowercase-hex-sha256(file bytes) "\n"
//   5. Result: "sha256:" + lowercase hex.
// Directory entries themselves (including empty directories) do not
// contribute. File names are not Unicode-normalised: a copy whose names were
// converted to NFD (older macOS filesystems) fingerprints differently.

#include "core/result.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace pmm::providers {

struct FingerprintOptions {
    // Polled between files and between read chunks; set to true to abort with
    // ErrorCode::Cancelled. May be null.
    const std::atomic<bool>* cancel = nullptr;
    // Fails with LimitExceeded before hashing when the files add up to more.
    std::uint64_t maxTotalBytes = 16ull * 1024 * 1024 * 1024;
    std::uint64_t maxFiles = 500'000;
};

Result<std::string> fingerprintDirectory(const std::filesystem::path& root,
                                         const FingerprintOptions& options = {});

// True when `genericRelativePath` ('/'-separated, relative to the mod root)
// is excluded from fingerprints by the junk rules above.
bool isFingerprintJunk(std::string_view genericRelativePath) noexcept;

} // namespace pmm::providers
