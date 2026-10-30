// End-to-end tests of pmm-steam-helper against fake_steam_api.cpp: the real
// helper binary is spawned with pipes, loads the fake library from a Unicode
// path, and is driven through the JSON-lines protocol.

#if defined(PMM_STEAM_HELPER_PATH) && defined(PMM_FAKE_STEAM_API_PATH)

#include "providers/helper_protocol.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using namespace pmm;
using namespace pmm::providers;
using pmm::test::TempDir;
using pmm::test::u8path;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

void setEnv(const char* name, const char* value) {
    _putenv_s(name, value ? value : "");
}

// Collects '\n'-terminated lines from a pipe on a background thread.
class LineCollector {
public:
    void push(std::string_view bytes) {
        auto lines = m_splitter.push(bytes);
        std::lock_guard lock(m_mutex);
        for (auto& l : lines)
            m_lines.push_back(std::move(l));
        m_ready.notify_all();
    }
    void close() {
        std::lock_guard lock(m_mutex);
        m_closed = true;
        m_ready.notify_all();
    }
    std::optional<std::string> next(std::chrono::milliseconds timeout) {
        std::unique_lock lock(m_mutex);
        m_ready.wait_for(lock, timeout, [this] { return !m_lines.empty() || m_closed; });
        if (m_lines.empty())
            return std::nullopt;
        std::string line = std::move(m_lines.front());
        m_lines.pop_front();
        return line;
    }
    std::vector<std::string> drain(std::chrono::milliseconds timeout) {
        std::unique_lock lock(m_mutex);
        m_ready.wait_for(lock, timeout, [this] { return m_closed; });
        std::vector<std::string> out(m_lines.begin(), m_lines.end());
        m_lines.clear();
        return out;
    }

private:
    LineSplitter m_splitter;
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<std::string> m_lines;
    bool m_closed = false;
};

class HelperProcess {
public:
    HelperProcess(const std::string& appId, const fs::path& library) {
        start({"--app-id", appId, "--steam-api"}, library);
    }
    explicit HelperProcess(const std::vector<std::string>& args) { start(args, {}); }

    ~HelperProcess() {
        closeStdin();
        if (!m_exited)
            waitExit(5s);
        if (!m_exited && m_process)
            TerminateProcess(m_process, 1);
        for (auto& t : m_readers)
            if (t.joinable())
                t.join();
        if (m_process)
            CloseHandle(m_process);
    }

    void send(const std::string& line) {
        const std::string data = line + "\n";
        DWORD written = 0;
        REQUIRE(
            WriteFile(m_stdin, data.data(), static_cast<DWORD>(data.size()), &written, nullptr));
    }

    void closeStdin() {
        if (m_stdin) {
            CloseHandle(m_stdin);
            m_stdin = nullptr;
        }
    }

    HelperEvent nextEvent(std::chrono::milliseconds timeout = 10s) {
        auto line = m_out.next(timeout);
        REQUIRE(line.has_value());
        INFO("line: " << *line);
        auto event = decodeEvent(*line);
        REQUIRE(event.ok());
        return std::move(*event);
    }

    template <class T> T next(std::chrono::milliseconds timeout = 10s) {
        HelperEvent e = nextEvent(timeout);
        REQUIRE(std::holds_alternative<T>(e));
        return std::get<T>(std::move(e));
    }

    std::optional<int> waitExit(std::chrono::milliseconds timeout) {
        if (m_exited)
            return m_exitCode;
        if (WaitForSingleObject(m_process, static_cast<DWORD>(timeout.count())) != WAIT_OBJECT_0)
            return std::nullopt;
        DWORD code = 0;
        GetExitCodeProcess(m_process, &code);
        m_exitCode = static_cast<int>(code);
        m_exited = true;
        return m_exitCode;
    }

    std::vector<std::string> remainingStdout() { return m_out.drain(5s); }
    std::vector<std::string> stderrLines() { return m_err.drain(5s); }

private:
    void start(const std::vector<std::string>& args, const fs::path& library) {
        const fs::path exe = u8path(PMM_STEAM_HELPER_PATH);
        SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr,
               errRead = nullptr, errWrite = nullptr;
        REQUIRE(CreatePipe(&inRead, &inWrite, &sa, 0));
        REQUIRE(CreatePipe(&outRead, &outWrite, &sa, 0));
        REQUIRE(CreatePipe(&errRead, &errWrite, &sa, 0));
        SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

        std::wstring cmd = quote(exe.wstring());
        for (const auto& a : args)
            cmd += L" " + quote(u8path(a).wstring());
        if (!library.empty())
            cmd += L" " + quote(library.wstring());

        STARTUPINFOW si{};
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = inRead;
        si.hStdOutput = outWrite;
        si.hStdError = errWrite;
        PROCESS_INFORMATION pi{};
        const BOOL created = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(inRead);
        CloseHandle(outWrite);
        CloseHandle(errWrite);
        REQUIRE(created);
        CloseHandle(pi.hThread);
        m_process = pi.hProcess;
        m_stdin = inWrite;
        m_readers.emplace_back([outRead, this] { pump(outRead, m_out); });
        m_readers.emplace_back([errRead, this] { pump(errRead, m_err); });
    }

    static std::wstring quote(const std::wstring& arg) {
        std::wstring out = L"\"";
        std::size_t backslashes = 0;
        for (wchar_t c : arg) {
            if (c == L'\\') {
                ++backslashes;
                continue;
            }
            out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
            backslashes = 0;
            out.push_back(c);
        }
        out.append(backslashes * 2, L'\\');
        out.push_back(L'"');
        return out;
    }

    static void pump(HANDLE pipe, LineCollector& collector) {
        char buf[4096];
        DWORD got = 0;
        while (ReadFile(pipe, buf, sizeof buf, &got, nullptr) && got > 0)
            collector.push(std::string_view(buf, got));
        CloseHandle(pipe);
        collector.close();
    }

    HANDLE m_process = nullptr;
    HANDLE m_stdin = nullptr;
    LineCollector m_out;
    LineCollector m_err;
    std::vector<std::thread> m_readers;
    bool m_exited = false;
    int m_exitCode = -1;
};

// Copies the fake library into "<temp>/Ünïcødé dir/日本語/<accepted name>".
struct FakeSteamInstall {
    TempDir dir{"helper"};
    fs::path library;

    explicit FakeSteamInstall(const char* mode = "ok") {
        const fs::path folder = dir.path() / u8path("Ünïcødé dir") / u8path("日本語");
        fs::create_directories(folder);
        library = folder / "steam_api64.dll";
        fs::copy_file(u8path(PMM_FAKE_STEAM_API_PATH), library);
        setEnv("PMM_FAKE_STEAM_MODE", mode);
        setEnv("SteamAppId", nullptr); // the helper must set these itself
        setEnv("SteamGameId", nullptr);
    }
};

int exitCode(HelperExitCode code) {
    return static_cast<int>(code);
}

} // namespace

TEST_CASE("Helper serves state, subscribe and download against a Steam API library",
          "[providers][helper]") {
    FakeSteamInstall steam;
    HelperProcess helper("281990", steam.library);

    const auto ready = helper.next<ReadyEvent>();
    CHECK(ready.ok);
    CHECK(ready.protocol == kHelperProtocolVersion);
    CHECK(ready.loggedOn);
    CHECK(ready.ownsApp);
    CHECK(ready.appId == "281990");
    CHECK(ready.ugcInterface == "STEAMUGC_INTERFACE_VERSION017"); // highest exported accessor
    CHECK(ready.initFunction == "SteamAPI_InitFlat");
    CHECK(ready.subscribedCount == 1);

    helper.send(encodeCommand(PingCommand{}));
    helper.next<PongEvent>();

    helper.send(encodeCommand(StateCommand{{"111111111", "222222222"}}));
    const auto installed = helper.next<ItemStateEvent>().status;
    CHECK(installed.id == "111111111");
    CHECK(installed.flags == 5);
    CHECK(installed.installPath == "/fake/\xC3\x9Cn\xC3\xAF"
                                   "c\xC3\xB8"
                                   "d\xC3\xA9 library/steamapps/workshop/content/281990/111111111");
    CHECK(installed.sizeOnDisk == 4096);
    CHECK(installed.timestamp == 1700000000);
    const auto unknown = helper.next<ItemStateEvent>().status;
    CHECK(unknown.id == "222222222");
    CHECK(unknown.flags == 0);
    CHECK(unknown.installPath.empty());

    // Subscribe results come from polled call results.
    helper.send(encodeCommand(SubscribeCommand{"222222222"}));
    const auto subscribed = helper.next<SubscribeResultEvent>();
    CHECK(subscribed.id == "222222222");
    CHECK(subscribed.ok);
    CHECK(subscribed.result == 1);

    helper.send(encodeCommand(SubscribeCommand{"333333333"}));
    const auto notFound = helper.next<SubscribeResultEvent>();
    CHECK_FALSE(notFound.ok);
    CHECK(notFound.result == 9);

    helper.send(encodeCommand(SubscribeCommand{"444444444"}));
    const auto invalidCall = helper.next<SubscribeResultEvent>();
    CHECK_FALSE(invalidCall.ok);
    CHECK(invalidCall.result == 2);

    helper.send(encodeCommand(DownloadCommand{"222222222", true}));
    const auto requested = helper.next<DownloadRequestedEvent>();
    CHECK(requested.id == "222222222");
    CHECK(requested.accepted);

    // DownloadItem refuses items the account is not subscribed to (fake rule).
    helper.send(encodeCommand(DownloadCommand{"333333333", false}));
    CHECK_FALSE(helper.next<DownloadRequestedEvent>().accepted);

    // The fake advances the download on every state query.
    std::uint32_t lastFlags = 0;
    bool sawDownloading = false;
    for (int i = 0; i < 6 && (lastFlags & 4) == 0; ++i) {
        helper.send(encodeCommand(StateCommand{{"222222222"}}));
        const auto s = helper.next<ItemStateEvent>().status;
        lastFlags = s.flags;
        sawDownloading = sawDownloading || (s.flags & 16) != 0;
    }
    CHECK(sawDownloading);
    CHECK(lastFlags == 5);

    // Malformed and unsafe commands get error replies; the helper keeps going.
    helper.send("this is not json");
    CHECK(helper.next<ErrorEvent>().code == "bad_request");
    helper.send(R"({"cmd":"subscribe","id":"12;rm -rf"})");
    const auto badId = helper.next<ErrorEvent>();
    CHECK(badId.code == "invalid_id");
    CHECK(badId.cmd == "subscribe");
    helper.send(R"({"cmd":"unsubscribe","id":"111111111"})");
    CHECK(helper.next<ErrorEvent>().code == "unknown_command");
    helper.send(R"({"protocol":2,"cmd":"ping"})");
    CHECK(helper.next<ErrorEvent>().code == "unsupported_protocol");

    helper.send(encodeCommand(PingCommand{}));
    helper.next<PongEvent>();
    helper.send(encodeCommand(QuitCommand{}));
    CHECK(helper.waitExit(10s) == exitCode(HelperExitCode::Ok));

    // Every stdout line is protocol; the library's own logging went to stderr.
    for (const auto& line : helper.remainingStdout()) {
        INFO(line);
        CHECK(decodeEvent(line).ok());
    }
    bool libraryNoiseOnStderr = false;
    for (const auto& line : helper.stderrLines())
        libraryNoiseOnStderr = libraryNoiseOnStderr || line.find("Setting breakpad") == 0;
    CHECK(libraryNoiseOnStderr);
}

TEST_CASE("Helper exits cleanly at end of input", "[providers][helper]") {
    FakeSteamInstall steam;
    HelperProcess helper("281990", steam.library);
    CHECK(helper.next<ReadyEvent>().ok);
    helper.send(encodeCommand(StateCommand{{"111111111"}}));
    CHECK(helper.next<ItemStateEvent>().status.flags == 5);
    helper.closeStdin();
    CHECK(helper.waitExit(10s) == exitCode(HelperExitCode::Ok));
}

TEST_CASE("Helper reports session problems in its ready event", "[providers][helper]") {
    struct Row {
        const char* mode;
        bool ok;
        HelperFailure error;
        int exit;
    };
    const Row rows[] = {
        {"no_client", false, HelperFailure::SteamNotRunning,
         exitCode(HelperExitCode::SteamNotRunning)},
        {"init_fail", false, HelperFailure::InitFailed, exitCode(HelperExitCode::InitFailed)},
        {"no_user", false, HelperFailure::NotLoggedOn, exitCode(HelperExitCode::NotLoggedOn)},
    };
    for (const Row& row : rows) {
        INFO(row.mode);
        FakeSteamInstall steam(row.mode);
        HelperProcess helper("281990", steam.library);
        const auto ready = helper.next<ReadyEvent>();
        CHECK(ready.ok == row.ok);
        CHECK(ready.error == row.error);
        CHECK_FALSE(ready.message.empty());
        CHECK(helper.waitExit(10s) == row.exit);
    }

    SECTION("offline mode is a usable session without network operations") {
        FakeSteamInstall steam("offline");
        HelperProcess helper("281990", steam.library);
        const auto ready = helper.next<ReadyEvent>();
        CHECK(ready.ok);
        CHECK_FALSE(ready.loggedOn);
        CHECK(ready.ownsApp);
    }
    SECTION("an account without the game is reported") {
        FakeSteamInstall steam("not_owned");
        HelperProcess helper("281990", steam.library);
        const auto ready = helper.next<ReadyEvent>();
        CHECK(ready.ok);
        CHECK(ready.loggedOn);
        CHECK_FALSE(ready.ownsApp);
    }
}

TEST_CASE("Helper rejects bad arguments and unloadable libraries", "[providers][helper]") {
    FakeSteamInstall steam;

    SECTION("non-numeric app id") {
        HelperProcess helper("281990x", steam.library);
        const auto ready = helper.next<ReadyEvent>();
        CHECK(ready.error == HelperFailure::InvalidArguments);
        CHECK(helper.waitExit(10s) == exitCode(HelperExitCode::InvalidArguments));
    }
    SECTION("library with an unexpected file name") {
        const fs::path renamed = steam.library.parent_path() / "steam_api_proxy.dll";
        fs::copy_file(steam.library, renamed);
        HelperProcess helper("281990", renamed);
        CHECK(helper.next<ReadyEvent>().error == HelperFailure::InvalidArguments);
        CHECK(helper.waitExit(10s) == exitCode(HelperExitCode::InvalidArguments));
    }
    SECTION("missing library") {
        HelperProcess helper("281990", steam.dir.path() / "steam_api64.dll");
        CHECK(helper.next<ReadyEvent>().error == HelperFailure::LibraryLoadFailed);
        CHECK(helper.waitExit(10s) == exitCode(HelperExitCode::LibraryLoadFailed));
    }
    SECTION("file that is not a loadable library") {
        const fs::path junkDir = steam.dir.path() / "junk";
        fs::create_directories(junkDir);
        pmm::test::writeFile(junkDir / "steam_api64.dll", "definitely not a library");
        HelperProcess helper("281990", junkDir / "steam_api64.dll");
        const auto ready = helper.next<ReadyEvent>();
        CHECK(ready.error == HelperFailure::LibraryLoadFailed);
        CHECK(helper.waitExit(10s) == exitCode(HelperExitCode::LibraryLoadFailed));
    }
    SECTION("unknown argument") {
        HelperProcess helper(
            std::vector<std::string>{"--app-id", "281990", "--url", "https://example.invalid"});
        CHECK(helper.next<ReadyEvent>().error == HelperFailure::InvalidArguments);
        CHECK(helper.waitExit(10s) == exitCode(HelperExitCode::InvalidArguments));
    }
}

#endif // PMM_STEAM_HELPER_PATH && PMM_FAKE_STEAM_API_PATH
