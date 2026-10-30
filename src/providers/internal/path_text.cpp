#include "providers/internal/path_text.hpp"

#include "core/utf8.hpp"

#include <string_view>

namespace pmm::providers::internal {

std::optional<std::string> pathToUtf8(const std::filesystem::path& path, bool generic) {
    const std::wstring native = generic ? path.generic_wstring() : path.native();
    if (!isValidUtf16(std::wstring_view(native)))
        return std::nullopt;
    return utf16ToUtf8(std::wstring_view(native));
}

std::string displayPath(const std::filesystem::path& path) {
    return utf16ToUtf8(std::wstring_view(path.native()));
}

} // namespace pmm::providers::internal
