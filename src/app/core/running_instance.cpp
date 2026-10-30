#include "app/core/running_instance.hpp"

#include <windows.h>

namespace pmm::app {

RunningInstanceSignal::RunningInstanceSignal(const QString& name) {
    // Several instances (other profiles) may share it: each holds a handle, and
    // the name exists until the last one is gone. Owning it is not the point.
    m_handle = CreateMutexW(nullptr, FALSE, reinterpret_cast<const wchar_t*>(name.utf16()));
}

RunningInstanceSignal::~RunningInstanceSignal() {
    if (m_handle)
        CloseHandle(m_handle);
}

} // namespace pmm::app
