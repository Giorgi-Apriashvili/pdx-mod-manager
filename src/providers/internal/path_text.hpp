#pragma once

// Path -> UTF-8 text for the provider sources. path::u8string() throws on
// Windows for names with unpaired UTF-16 surrogates; these functions never
// throw (except std::bad_alloc). Not part of the public providers API.

#include <filesystem>
#include <optional>
#include <string>

namespace pmm::providers::internal {

// Exact UTF-8 form of `path` ('/'-separated when `generic`), or nullopt when it
// has none: a Windows name with unpaired surrogates. POSIX paths are byte
// strings and come back unchanged, as path::u8string() returns them there.
std::optional<std::string> pathToUtf8(const std::filesystem::path& path, bool generic = false);

// For messages and error details: always valid UTF-8, anything without a UTF-8
// form replaced by U+FFFD.
std::string displayPath(const std::filesystem::path& path);

} // namespace pmm::providers::internal
