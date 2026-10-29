#include "platform/fs.hpp"
#include "platform/process.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
// Needs <windows.h> first.
#include <shellapi.h>
#endif

using namespace pmm;
using namespace pmm::platform;
using pmm::test::TempDir;

namespace {

fs::path echoExecutable() {
    return pathFromUtf8(PMM_ECHO_EXE);
}

// Waits until the echo child has published its report.
std::optional<nlohmann::json> waitForReport(const fs::path& file) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        std::error_code ec;
        if (fs::exists(file, ec)) {
            if (auto text = readFile(file); text.ok()) {
                auto parsed = nlohmann::json::parse(*text, nullptr, false);
                if (!parsed.is_discarded())
                    return parsed;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return std::nullopt;
}

// Arguments that exercise every quoting rule.
const std::vector<std::string>& trickyArguments() {
    static const std::vector<std::string> arguments = {
        "plain",
        "with space",
        "",
        "quote\"inside",
        "\"fully quoted\"",
        "trailing backslash\\",
        "trailing\\\\",
        "back\\\\\"slash quote",
        "C:\\Program Files\\Game\\",
        "tab\there",
        "-flag=value",
        "Ünïcødé 日本語 \xF0\x9F\x98\x80",
        "a\\b\\c",
    };
    return arguments;
}

#if defined(_WIN32)
std::vector<std::wstring> splitWithCommandLineToArgvW(const std::wstring& commandLine) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(commandLine.c_str(), &argc);
    REQUIRE(argv != nullptr);
    std::vector<std::wstring> result(argv, argv + argc);
    LocalFree(argv);
    return result;
}
#endif

} // namespace

TEST_CASE("quoteWindowsArgument follows CommandLineToArgvW rules", "[platform][process]") {
    struct Case {
        std::wstring input;
        std::wstring expected;
    };
    const std::vector<Case> cases = {
        {L"plain", L"plain"},
        {L"", L"\"\""},
        {L"with space", L"\"with space\""},
        {L"tab\there", L"\"tab\there\""},
        {L"quote\"inside", L"\"quote\\\"inside\""},
        {L"back\\slash", L"back\\slash"},                 // no quoting needed: literal
        {L"trailing\\", L"trailing\\"},                   // unquoted: backslash stays single
        {L"trailing space\\", L"\"trailing space\\\\\""}, // doubled before the closing quote
        {L"two\\\\\" q", L"\"two\\\\\\\\\\\" q\""},       // 2 backslashes + quote -> 5 + quote
        {L"\\\\server\\share dir", L"\"\\\\server\\share dir\""},
        {L"\u00DCn\u00EFc\u00F8d\u00E9 \u65E5\u672C",
         L"\"\u00DCn\u00EFc\u00F8d\u00E9 \u65E5\u672C\""},
    };
    for (const Case& c : cases) {
        CAPTURE(pathToUtf8(fs::path(c.input)));
        CHECK(quoteWindowsArgument(c.input) == c.expected);
    }
}

#if defined(_WIN32)
TEST_CASE("Windows command lines round-trip through CommandLineToArgvW", "[platform][process]") {
    const fs::path exe = L"C:\\Program Files\\Example Game\\game.exe";
    const auto& arguments = trickyArguments();
    const std::wstring line = buildWindowsCommandLine(exe, arguments);
    const auto parsed = splitWithCommandLineToArgvW(line);
    REQUIRE(parsed.size() == arguments.size() + 1);
    CHECK(parsed[0] == exe.native());
    for (std::size_t i = 0; i < arguments.size(); ++i)
        CHECK(pathToUtf8(fs::path(parsed[i + 1])) == arguments[i]);
}

TEST_CASE("Random arguments round-trip through CommandLineToArgvW", "[platform][process]") {
    std::mt19937 rng(12345);
    const std::wstring alphabet = L"ab \\\"\t\u00FC\u65E5";
    std::uniform_int_distribution<std::size_t> pick(0, alphabet.size() - 1);
    std::uniform_int_distribution<int> length(0, 12);
    for (int round = 0; round < 500; ++round) {
        std::vector<std::wstring> arguments(4);
        std::wstring line = L"\"C:\\x\\prog.exe\"";
        for (auto& argument : arguments) {
            const int n = length(rng);
            for (int i = 0; i < n; ++i)
                argument += alphabet[pick(rng)];
            line += L" " + quoteWindowsArgument(argument);
        }
        const auto parsed = splitWithCommandLineToArgvW(line);
        REQUIRE(parsed.size() == arguments.size() + 1);
        for (std::size_t i = 0; i < arguments.size(); ++i)
            REQUIRE(parsed[i + 1] == arguments[i]);
    }
}
#endif

TEST_CASE("startDetached passes arguments, working directory and environment",
          "[platform][process]") {
    TempDir temp;
    const fs::path workingDirectory = temp.path() / pathFromUtf8("wörk dir 日本語");
    fs::create_directories(workingDirectory);
    const fs::path report = temp.path() / test::unicodeSubdir() / "report file.json";

    ProcessSpec spec;
    spec.executable = echoExecutable();
    spec.workingDirectory = workingDirectory;
    spec.arguments = trickyArguments();
    spec.environment = {{"PMM_ECHO_OUTPUT", pathToUtf8(report)},
                        {"PMM_ECHO_VAR", "välue = with spaces; 日本語"}};

    const auto started = startDetached(spec);
    REQUIRE(started.ok());
    CHECK(started->pid > 0);

    const auto result = waitForReport(report);
    REQUIRE(result.has_value());
    CHECK((*result)["args"].get<std::vector<std::string>>() == trickyArguments());
    CHECK((*result)["env"].get<std::string>() == "välue = with spaces; 日本語");
    const fs::path reportedCwd = pathFromUtf8((*result)["cwd"].get<std::string>());
    CHECK(fs::equivalent(reportedCwd, workingDirectory));
}

TEST_CASE("startDetached defaults the working directory to the executable folder",
          "[platform][process]") {
    TempDir temp;
    const fs::path report = temp / "report.json";
    ProcessSpec spec;
    spec.executable = echoExecutable();
    spec.environment = {{"PMM_ECHO_OUTPUT", pathToUtf8(report)}};

    REQUIRE(startDetached(spec).ok());
    const auto result = waitForReport(report);
    REQUIRE(result.has_value());
    CHECK((*result)["args"].empty());
    CHECK((*result)["env"].is_null());
    CHECK(fs::equivalent(pathFromUtf8((*result)["cwd"].get<std::string>()),
                         echoExecutable().parent_path()));
}

TEST_CASE("startDetached validates the spec before spawning", "[platform][process]") {
    TempDir temp;
    ProcessSpec valid;
    valid.executable = echoExecutable();
    valid.workingDirectory = temp.path();

    SECTION("relative executable") {
        ProcessSpec spec = valid;
        spec.executable = "relative/game";
        const auto result = startDetached(spec);
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
    }
    SECTION("missing executable") {
        ProcessSpec spec = valid;
        spec.executable = temp.path() / "missing game.exe";
        const auto result = startDetached(spec);
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().code == ErrorCode::ProcessStartFailed);
        CHECK(result.error().message.find("missing game.exe") != std::string::npos);
    }
    SECTION("executable is a folder") {
        ProcessSpec spec = valid;
        spec.executable = temp.path();
        const auto result = startDetached(spec);
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().code == ErrorCode::ProcessStartFailed);
    }
    SECTION("missing working directory") {
        ProcessSpec spec = valid;
        spec.workingDirectory = temp.path() / "missing dir";
        const auto result = startDetached(spec);
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().code == ErrorCode::ProcessStartFailed);
    }
    SECTION("NUL inside an argument") {
        ProcessSpec spec = valid;
        spec.arguments = {std::string("a\0b", 3)};
        const auto result = startDetached(spec);
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
    }
    SECTION("invalid environment names") {
        const auto name = GENERATE(std::string(), std::string("A=B"), std::string("A\0B", 3));
        ProcessSpec spec = valid;
        spec.environment = {{name, "value"}};
        const auto result = startDetached(spec);
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().code == ErrorCode::InvalidArgument);
    }
    SECTION("not a program") {
        const fs::path fake = temp / "not a program.exe";
        test::writeText(fake, "this is text, not an executable");
        ProcessSpec spec = valid;
        spec.executable = fake;
        const auto result = startDetached(spec);
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().code == ErrorCode::ProcessStartFailed);
        CHECK_FALSE(result.error().detail.empty());
    }
    SECTION("corrupt program image fails fast without a system dialog") {
        // Passes the MZ check on Windows, so CreateProcessW itself rejects it.
        const fs::path fake = temp / "corrupt image.exe";
        test::writeText(fake, std::string("MZ") + std::string(512, '\x7f'));
        ProcessSpec spec = valid;
        spec.executable = fake;
        const auto before = std::chrono::steady_clock::now();
        const auto result = startDetached(spec);
        REQUIRE_FALSE(result.ok());
        CHECK(result.error().code == ErrorCode::ProcessStartFailed);
        CHECK_FALSE(result.error().detail.empty());
        CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(10));
    }
}

TEST_CASE("listProcesses includes the current process", "[platform][process]") {
    const auto processes = listProcesses();
    REQUIRE(processes.ok());
    CHECK(processes->size() > 1);
    const std::int64_t self = currentProcessId();
    const auto it = std::find_if(processes->begin(), processes->end(),
                                 [&](const RunningProcess& p) { return p.pid == self; });
    REQUIRE(it != processes->end());
    CHECK(it->executableName.find("pmm_platform_tests") == 0);
    CHECK(executableNameMatches(it->executableName, it->executableName));
    if (!it->executablePath.empty())
        CHECK(it->executablePath.filename() == pathFromUtf8(it->executableName));
}

TEST_CASE("executableNameMatches follows the OS case rules", "[platform][process]") {
    CHECK(executableNameMatches("stellaris.exe", "stellaris.exe"));
    CHECK_FALSE(executableNameMatches("stellaris.exe", "stellaris"));
    CHECK_FALSE(executableNameMatches("eu4", ""));
#if defined(_WIN32) || defined(__APPLE__)
    CHECK(executableNameMatches("Stellaris.EXE", "stellaris.exe"));
#else
    CHECK_FALSE(executableNameMatches("Stellaris", "stellaris"));
#endif
}
