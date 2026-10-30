#pragma once

// Unicode helpers for the repositories, re-exported from core/utf8.hpp so the
// repository sources keep their short names. Nothing here throws except
// std::bad_alloc.

#include "core/utf8.hpp"

namespace pmm::persistence::internal {

using pmm::isValidUtf8;
using pmm::sanitizeUtf8;

using pmm::isValidUtf16;
using pmm::utf16ToUtf8;
using pmm::utf8ToUtf16;
using pmm::utf8ToWide;

} // namespace pmm::persistence::internal
