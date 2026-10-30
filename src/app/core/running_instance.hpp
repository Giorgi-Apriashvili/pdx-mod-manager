#pragma once

// The signal an update run waits for (docs/updates.md): on Windows the process
// holds a named mutex for its whole lifetime, and the installer waits until
// nobody holds it any more. The operating system releases it if the process
// dies. Other platforms have no installer that needs it: nothing is held.

#include <QString>

namespace pmm::app {

inline constexpr const char* kRunningInstanceMutexName = "Local\\ParadoxModManagerRunning";

class RunningInstanceSignal {
public:
    explicit RunningInstanceSignal(const QString& name = QLatin1String(kRunningInstanceMutexName));
    ~RunningInstanceSignal();
    RunningInstanceSignal(const RunningInstanceSignal&) = delete;
    RunningInstanceSignal& operator=(const RunningInstanceSignal&) = delete;

    bool held() const noexcept { return m_handle != nullptr; }

private:
    void* m_handle = nullptr;
};

} // namespace pmm::app
