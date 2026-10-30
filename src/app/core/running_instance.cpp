#include "app/core/running_instance.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace pmm::app {

#if defined(_WIN32)

RunningInstanceSignal::RunningInstanceSignal(const QString& name) {
    // Several instances (other profiles) may share it: each holds a handle, and
    // the name exists until the last one is gone. Owning it is not the point.
    m_handle = CreateMutexW(nullptr, FALSE, reinterpret_cast<const wchar_t*>(name.utf16()));
}

RunningInstanceSignal::~RunningInstanceSignal() {
    if (m_handle)
        CloseHandle(m_handle);
}

#else

RunningInstanceSignal::RunningInstanceSignal(const QString&) {}

RunningInstanceSignal::~RunningInstanceSignal() = default;

#endif

} // namespace pmm::app
