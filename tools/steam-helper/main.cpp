// pmm-steam-helper --app-id <digits> --steam-api <path to the game's steam_api library>
//
// Runs one Steam API session in the game's app context and answers JSON-lines
// commands on stdin (protocol: providers/helper_protocol.hpp, documentation:
// docs/steam-workshop.md). The main thread pumps SteamAPI_RunCallbacks every
// ~50 ms; a reader thread turns stdin into a queue of lines so the pump never
// blocks on input. Exits after "quit" or end of input, calling SteamAPI_Shutdown.
//
// Safety: commands carry only validated published-file ids. There is no
// command to unsubscribe, delete, or touch arbitrary paths or URLs.

#include "providers/helper_protocol.hpp"
#include "providers/workshop_item.hpp"
#include "steam_api_loader.hpp"

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#endif

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

using namespace pmm;
using namespace pmm::providers;
using pmm::steamhelper::CallStatus;
using pmm::steamhelper::SteamApi;
using pmm::steamhelper::SteamApiCall;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr auto kPumpInterval = std::chrono::milliseconds(50);
constexpr auto kSubscribeTimeout = std::chrono::seconds(120);
constexpr int kEResultFail = 2;
constexpr int kEResultTimeout = 16;

constexpr const char* kUsage =
    "usage: pmm-steam-helper --app-id <digits> --steam-api <path to steam_api64.dll | "
    "steam_api.dll | libsteam_api.so | libsteam_api.dylib>";

std::string utf8(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

void diag(const std::string& text) {
    std::fprintf(stderr, "pmm-steam-helper: %s\n", text.c_str());
    std::fflush(stderr);
}

// Protocol lines go to a private duplicate of the original stdout. The
// process-wide stdout is then pointed at stderr, so anything the Steam library
// prints (it does log to stdout) cannot corrupt the JSON-lines stream.
class ProtocolOutput {
public:
    bool open() {
#if defined(_WIN32)
        const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out == nullptr || out == INVALID_HANDLE_VALUE)
            return false;
        if (!DuplicateHandle(GetCurrentProcess(), out, GetCurrentProcess(), &m_handle, 0, FALSE,
                             DUPLICATE_SAME_ACCESS))
            return false;
        std::fflush(stdout);
        const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
        if (err != nullptr && err != INVALID_HANDLE_VALUE) {
            // Libraries with their own C runtime pick up the std handle at load time.
            SetStdHandle(STD_OUTPUT_HANDLE, err);
            if (_fileno(stderr) >= 0 && _fileno(stdout) >= 0)
                _dup2(_fileno(stderr), _fileno(stdout));
        } else {
            FILE* sink = nullptr;
            freopen_s(&sink, "NUL", "w", stdout);
        }
#else
        m_fd = dup(STDOUT_FILENO);
        if (m_fd < 0)
            return false;
        fcntl(m_fd, F_SETFD, FD_CLOEXEC);
        std::fflush(stdout);
        dup2(STDERR_FILENO, STDOUT_FILENO);
        std::signal(SIGPIPE, SIG_IGN);
#endif
        m_ok = true;
        return true;
    }

    bool write(const std::string& line) {
        if (!m_ok)
            return false;
        std::string data = line;
        data += '\n';
        const char* p = data.data();
        std::size_t left = data.size();
        while (left > 0) {
#if defined(_WIN32)
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(left, 1u << 20));
            if (!WriteFile(m_handle, p, chunk, &written, nullptr) || written == 0) {
                m_ok = false;
                return false;
            }
#else
            const ssize_t written = ::write(m_fd, p, left);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) {
                m_ok = false;
                return false;
            }
#endif
            p += written;
            left -= static_cast<std::size_t>(written);
        }
        return true;
    }

    bool ok() const { return m_ok; }

private:
#if defined(_WIN32)
    HANDLE m_handle = nullptr;
#else
    int m_fd = -1;
#endif
    bool m_ok = false;
};

// Lines read from stdin by the reader thread. Shared ownership keeps it alive
// for the detached reader after main() returns.
struct InputQueue {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::string> lines;
    bool eof = false;
};

void readInput(std::shared_ptr<InputQueue> queue) {
    // Raw OS reads: no C runtime stream lock is held while blocked, so process
    // exit never waits for this thread.
    LineSplitter splitter;
    char buf[4096];
#if defined(_WIN32)
    const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
#endif
    for (;;) {
#if defined(_WIN32)
        DWORD got = 0;
        if (in == nullptr || in == INVALID_HANDLE_VALUE ||
            !ReadFile(in, buf, sizeof buf, &got, nullptr) || got == 0)
            break;
#else
        const ssize_t got = ::read(STDIN_FILENO, buf, sizeof buf);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
#endif
        auto lines = splitter.push(std::string_view(buf, static_cast<std::size_t>(got)));
        if (lines.empty())
            continue;
        std::lock_guard lock(queue->mutex);
        for (auto& line : lines)
            queue->lines.push_back(std::move(line));
        queue->ready.notify_one();
    }
    std::lock_guard lock(queue->mutex);
    queue->eof = true;
    queue->ready.notify_one();
}

class Session {
public:
    Session(SteamApi& api, ProtocolOutput& out) : m_api(api), m_out(out) {}

    // Returns false when the helper should exit.
    bool handle(const std::string& line) {
        auto decoded = decodeCommand(line);
        if (!decoded) {
            send(makeErrorEvent(decoded.error(), peekCommandName(line)));
            return m_out.ok();
        }
        bool keepRunning = true;
        std::visit(
            [this, &keepRunning](const auto& c) {
                using T = std::decay_t<decltype(c)>;
                if constexpr (std::is_same_v<T, PingCommand>) {
                    send(PongEvent{});
                } else if constexpr (std::is_same_v<T, StateCommand>) {
                    for (const auto& id : c.ids)
                        send(ItemStateEvent{status(id)});
                } else if constexpr (std::is_same_v<T, SubscribeCommand>) {
                    subscribe(c.id);
                } else if constexpr (std::is_same_v<T, DownloadCommand>) {
                    const bool accepted =
                        m_api.download(*parsePublishedFileId(c.id), c.highPriority);
                    send(DownloadRequestedEvent{c.id, accepted});
                } else {
                    static_assert(std::is_same_v<T, QuitCommand>);
                    keepRunning = false;
                }
            },
            *decoded);
        return keepRunning && m_out.ok();
    }

    // Delivers finished (or timed-out) subscribe call results.
    void poll() {
        const auto now = Clock::now();
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            int eresult = 0;
            const CallStatus call = m_api.pollSubscribeResult(it->call, eresult);
            if (call == CallStatus::Pending && now - it->started < kSubscribeTimeout) {
                ++it;
                continue;
            }
            if (call == CallStatus::Completed) {
                send(SubscribeResultEvent{it->id, eresult == 1, eresult});
            } else if (call == CallStatus::Failed) {
                diag("subscribe call failed for item " + it->id);
                send(SubscribeResultEvent{it->id, false, kEResultFail});
            } else {
                diag("subscribe call timed out for item " + it->id);
                send(SubscribeResultEvent{it->id, false, kEResultTimeout});
            }
            it = m_pending.erase(it);
        }
    }

private:
    struct PendingSubscribe {
        std::string id;
        SteamApiCall call = steamhelper::kInvalidApiCall;
        Clock::time_point started;
    };

    void send(const HelperEvent& event) { m_out.write(encodeEvent(event)); }

    ItemStatus status(const std::string& id) {
        const std::uint64_t pid = *parsePublishedFileId(id); // validated by decodeCommand
        ItemStatus s;
        s.id = id;
        s.flags = m_api.itemState(pid);
        if (const auto info = m_api.installInfo(pid)) {
            s.installPath = info->folder;
            s.sizeOnDisk = info->sizeOnDisk;
            s.timestamp = info->timestamp;
        }
        if (!m_api.downloadInfo(pid, s.bytesDownloaded, s.bytesTotal)) {
            s.bytesDownloaded = 0;
            s.bytesTotal = 0;
        }
        return s;
    }

    void subscribe(const std::string& id) {
        const SteamApiCall call = m_api.subscribe(*parsePublishedFileId(id));
        if (call == steamhelper::kInvalidApiCall) {
            diag("SubscribeItem returned an invalid call handle for item " + id);
            send(SubscribeResultEvent{id, false, kEResultFail});
            return;
        }
        m_pending.push_back(PendingSubscribe{id, call, Clock::now()});
    }

    SteamApi& m_api;
    ProtocolOutput& m_out;
    std::vector<PendingSubscribe> m_pending;
};

void setEnvironment(const std::string& name, const std::string& value) {
#if defined(_WIN32)
    const std::wstring wname(name.begin(), name.end()); // ASCII only
    const std::wstring wvalue(value.begin(), value.end());
    _wputenv_s(wname.c_str(), wvalue.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

int run(const std::vector<fs::path>& args) {
#if defined(_WIN32)
    // A background helper must never block on a modal system dialog (e.g. the
    // "bad image" box LoadLibrary can raise, or the crash reporter).
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    ProtocolOutput out;
    if (!out.open())
        return static_cast<int>(HelperExitCode::OutputFailed);

    auto fail = [&out](HelperFailure kind, const std::string& message) {
        ReadyEvent ready;
        ready.ok = false;
        ready.error = kind;
        ready.message = message;
        out.write(encodeEvent(ready));
        diag(std::string(toString(kind)) + ": " + message);
        return static_cast<int>(exitCodeFor(kind));
    };

    std::string appId;
    fs::path library;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string arg = utf8(args[i]);
        if (arg == "--app-id" && i + 1 < args.size()) {
            appId = utf8(args[++i]);
        } else if (arg == "--steam-api" && i + 1 < args.size()) {
            library = args[++i];
        } else {
            return fail(HelperFailure::InvalidArguments,
                        "Unexpected argument \"" + arg.substr(0, 64) + "\". " + kUsage);
        }
    }
    if (appId.empty() || library.empty())
        return fail(HelperFailure::InvalidArguments, kUsage);
    if (auto valid = validateHelperArguments(appId, library); !valid) {
        const Error& e = valid.error();
        const HelperFailure kind = e.code == ErrorCode::NotFound ? HelperFailure::LibraryLoadFailed
                                                                 : HelperFailure::InvalidArguments;
        return fail(kind, e.detail.empty() ? e.message : e.message + " (" + e.detail + ")");
    }
    const std::uint32_t appIdNumber = *parseSteamAppId(appId);

    // The Steam API reads the app context from these variables at init.
    setEnvironment("SteamAppId", appId);
    setEnvironment("SteamGameId", appId);

    SteamApi api;
    if (auto f = api.load(library))
        return fail(f->kind, f->message);
    if (auto f = api.resolveSymbols())
        return fail(f->kind, f->message);
    if (auto f = api.init(appIdNumber))
        return fail(f->kind, f->message);

    ReadyEvent ready;
    ready.ok = true;
    ready.loggedOn = api.loggedOn();
    ready.ownsApp = api.ownsApp(appIdNumber);
    ready.appId = appId;
    ready.ugcInterface = api.ugcInterface();
    ready.initFunction = api.initFunction();
    ready.subscribedCount = api.subscribedCount();
    if (!out.write(encodeEvent(ready)))
        return static_cast<int>(HelperExitCode::OutputFailed);
    diag("ready: app " + appId + ", " + ready.ugcInterface + " via " + ready.initFunction);

    auto queue = std::make_shared<InputQueue>();
    std::thread(readInput, queue).detach();

    Session session(api, out);
    bool running = true;
    while (running) {
        std::deque<std::string> batch;
        bool eof = false;
        {
            std::unique_lock lock(queue->mutex);
            queue->ready.wait_for(lock, kPumpInterval,
                                  [&queue] { return !queue->lines.empty() || queue->eof; });
            batch.swap(queue->lines);
            eof = queue->eof;
        }
        for (const auto& line : batch) {
            if (!session.handle(line)) {
                running = false;
                break;
            }
        }
        if (eof)
            running = false;
        api.runCallbacks();
        session.poll();
        if (!out.ok())
            running = false;
    }
    api.shutdown();
    diag("exiting");
    return out.ok() ? static_cast<int>(HelperExitCode::Ok)
                    : static_cast<int>(HelperExitCode::OutputFailed);
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    std::vector<fs::path> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    return run(args);
}
#else
int main(int argc, char** argv) {
    std::vector<fs::path> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    return run(args);
}
#endif
