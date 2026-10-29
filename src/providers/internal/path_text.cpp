#include "providers/internal/path_text.hpp"

#include "core/utf8.hpp"

#include <string_view>

namespace pmm::providers::internal {

std::optional<std::string> pathToUtf8(const std::filesystem::path& path, bool generic) {
#if defined(_WIN32)
    const std::wstring native = generic ? path.generic_wstring() : path.native();
    if (!isValidUtf16(std::wstring_view(native)))
        return std::nullopt;
    return utf16ToUtf8(std::wstring_view(native));
#else
    return generic ? path.generic_string() : path.native();
#endif
}

std::string displayPath(const std::filesystem::path& path) {
#if defined(_WIN32)
    return utf16ToUtf8(std::wstring_view(path.native()));
#else
    return sanitizeUtf8(path.native());
#endif
}

} // namespace pmm::providers::internal
