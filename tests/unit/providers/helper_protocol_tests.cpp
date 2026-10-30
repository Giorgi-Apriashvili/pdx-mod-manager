#include "providers/helper_protocol.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <variant>

using namespace pmm;
using namespace pmm::providers;
namespace fs = std::filesystem;

namespace {

std::string readFixture(const char* relative) {
    std::ifstream in(fs::path(PMM_FIXTURES_DIR) / relative, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

template <class T> const T& as(const HelperEvent& e) {
    REQUIRE(std::holds_alternative<T>(e));
    return std::get<T>(e);
}

} // namespace

TEST_CASE("Commands round-trip through the wire format", "[providers][protocol]") {
    const HelperCommand commands[] = {
        PingCommand{},
        StateCommand{{"703156866", "1121692237"}},
        SubscribeCommand{"1121692237"},
        DownloadCommand{"1121692237", true},
        DownloadCommand{"1121692237", false},
        QuitCommand{},
    };
    for (const auto& command : commands) {
        const std::string line = encodeCommand(command);
        INFO(line);
        CHECK(line.find('\n') == std::string::npos);
        CHECK(line.find("\"protocol\":1") != std::string::npos);
        auto decoded = decodeCommand(line);
        REQUIRE(decoded.ok());
        CHECK(*decoded == command);
    }
    CHECK(encodeCommand(StateCommand{{"5"}}) == R"({"protocol":1,"cmd":"state","ids":["5"]})");
}

TEST_CASE("Command decoding accepts the documented minimal forms", "[providers][protocol]") {
    auto ping = decodeCommand(R"({"cmd":"ping"})");
    REQUIRE(ping.ok());
    CHECK(std::holds_alternative<PingCommand>(*ping));

    auto download = decodeCommand("  {\"cmd\":\"download\",\"id\":\"42\"}  \r");
    REQUIRE(download.ok());
    CHECK(std::get<DownloadCommand>(*download).highPriority);

    auto state = decodeCommand(R"({"cmd":"state","ids":["1","2"],"future":"ignored"})");
    REQUIRE(state.ok());
    CHECK(std::get<StateCommand>(*state).ids.size() == 2);
}

TEST_CASE("Command decoding rejects malformed or unsafe input", "[providers][protocol]") {
    struct Row {
        const char* line;
        ErrorCode code;
    };
    const Row rows[] = {
        {"", ErrorCode::ParseError},
        {"not json", ErrorCode::ParseError},
        {"[1,2]", ErrorCode::ParseError},
        {R"({"id":"1"})", ErrorCode::ParseError},
        {R"({"cmd":5})", ErrorCode::ParseError},
        {R"({"cmd":"state"})", ErrorCode::ParseError},
        {R"({"cmd":"state","ids":[]})", ErrorCode::InvalidArgument},
        {R"({"cmd":"state","ids":["1",2]})", ErrorCode::InvalidArgument},
        {R"({"cmd":"state","ids":["01"]})", ErrorCode::InvalidArgument},
        {R"({"cmd":"subscribe","id":"12x"})", ErrorCode::InvalidArgument},
        {R"({"cmd":"subscribe","id":"18446744073709551616"})", ErrorCode::InvalidArgument},
        {R"({"cmd":"subscribe","id":"C:\\evil"})", ErrorCode::InvalidArgument},
        {R"({"cmd":"subscribe","id":123})", ErrorCode::ParseError},
        {R"({"cmd":"download","id":"1","highPriority":"yes"})", ErrorCode::ParseError},
        {R"({"cmd":"unsubscribe","id":"1"})", ErrorCode::ValidationFailed},
        {R"({"cmd":"delete","id":"1"})", ErrorCode::ValidationFailed},
        {R"({"cmd":"open","url":"https://example.invalid"})", ErrorCode::ValidationFailed},
        {R"({"protocol":2,"cmd":"ping"})", ErrorCode::UnsupportedVersion},
        {R"({"protocol":"1","cmd":"ping"})", ErrorCode::UnsupportedVersion},
    };
    for (const Row& row : rows) {
        INFO(row.line);
        auto r = decodeCommand(row.line);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().code == row.code);
    }

    std::string tooMany = R"({"cmd":"state","ids":[)";
    for (std::size_t i = 0; i <= kMaxIdsPerStateCommand; ++i)
        tooMany += (i ? ",\"" : "\"") + std::to_string(i + 1) + "\"";
    tooMany += "]}";
    auto r = decodeCommand(tooMany);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().code == ErrorCode::InvalidArgument);

    const std::string huge(kMaxHelperLineBytes + 1, ' ');
    auto big = decodeCommand(huge);
    REQUIRE_FALSE(big.ok());
    CHECK(big.error().code == ErrorCode::LimitExceeded);
}

TEST_CASE("Rejected commands map to protocol error events", "[providers][protocol]") {
    auto check = [](const char* line, const char* code, const char* cmd) {
        auto r = decodeCommand(line);
        REQUIRE_FALSE(r.ok());
        const ErrorEvent e = makeErrorEvent(r.error(), peekCommandName(line));
        CHECK(e.code == code);
        CHECK(e.cmd == cmd);
        CHECK_FALSE(e.message.empty());
        auto round = decodeEvent(encodeEvent(e));
        REQUIRE(round.ok());
        CHECK(std::get<ErrorEvent>(*round) == e);
    };
    check("garbage", "bad_request", "");
    check(R"({"cmd":"subscribe","id":"x"})", "invalid_id", "subscribe");
    check(R"({"cmd":"unsubscribe","id":"1"})", "unknown_command", "unsubscribe");
    check(R"({"protocol":9,"cmd":"ping"})", "unsupported_protocol", "ping");
}

TEST_CASE("Events round-trip through the wire format", "[providers][protocol]") {
    ReadyEvent ready;
    ready.ok = true;
    ready.loggedOn = true;
    ready.ownsApp = false;
    ready.appId = "281990";
    ready.ugcInterface = "STEAMUGC_INTERFACE_VERSION016";
    ready.initFunction = "SteamAPI_InitSafe";
    ready.subscribedCount = 27;

    ReadyEvent failed;
    failed.ok = false;
    failed.error = HelperFailure::SteamNotRunning;
    failed.message = "Steam is not running.";

    ItemStateEvent state;
    state.status.id = "703156866";
    state.status.flags = 1 | 16;
    state.status.bytesDownloaded = 5;
    state.status.bytesTotal = 10'000'000'000ull;
    state.status.installPath = "D:/Steam Library/\xC3\x9C"
                               "n\xC3\xAF"
                               "c\xC3\xB8"
                               "d\xC3\xA9/\xE6\x97\xA5";
    state.status.sizeOnDisk = 4096;
    state.status.timestamp = 1782489192;

    const HelperEvent events[] = {
        ready,
        failed,
        state,
        SubscribeResultEvent{"1121692237", true, 1},
        SubscribeResultEvent{"1121692237", false, 9},
        DownloadRequestedEvent{"1121692237", true},
        PongEvent{},
        ErrorEvent{"invalid_id", "bad id", "state"},
    };
    for (const auto& event : events) {
        const std::string line = encodeEvent(event);
        INFO(line);
        CHECK(line.find('\n') == std::string::npos);
        CHECK(line.rfind(R"({"event":)", 0) == 0);
        auto decoded = decodeEvent(line);
        REQUIRE(decoded.ok());
        CHECK(*decoded == event);
    }

    const std::string stateLine = encodeEvent(state);
    CHECK(stateLine.find(R"("subscribed":true,"installed":false,"downloading":true)") !=
          std::string::npos);
    CHECK(
        encodeEvent(failed) ==
        R"({"event":"ready","protocol":1,"ok":false,"error":"steam_not_running","message":"Steam is not running."})");
}

TEST_CASE("Event decoding validates fields and protocol version", "[providers][protocol]") {
    const char* bad[] = {
        "",
        "Setting breakpad minidump AppID = 281990",
        R"({"event":"mystery"})",
        R"({"event":"state","id":"1"})",
        R"({"event":"state","id":"1","flags":-1})",
        R"({"event":"state","id":"1","flags":4294967296})",
        R"({"event":"state","id":"abc","flags":1})",
        R"({"event":"subscribe_result","id":"1","ok":true})",
        R"({"event":"download_requested","id":"1"})",
        R"({"event":"ready","protocol":1})",
        R"({"event":"ready","protocol":1,"ok":true,"loggedOn":true})",
    };
    for (const char* line : bad) {
        INFO(line);
        CHECK_FALSE(decodeEvent(line).ok());
    }

    auto future = decodeEvent(R"({"event":"ready","protocol":2,"ok":true})");
    REQUIRE_FALSE(future.ok());
    CHECK(future.error().code == ErrorCode::UnsupportedVersion);

    // Booleans on the wire are informational; flags are authoritative.
    auto mismatch = decodeEvent(R"({"event":"state","id":"7","flags":5,"subscribed":false})");
    REQUIRE(mismatch.ok());
    CHECK(as<ItemStateEvent>(*mismatch).status.flags == 5);

    auto unknownFailure =
        decodeEvent(R"({"event":"ready","protocol":1,"ok":false,"error":"weird","message":"m"})");
    REQUIRE(unknownFailure.ok());
    CHECK(as<ReadyEvent>(*unknownFailure).error == HelperFailure::InitFailed);
}

TEST_CASE("Recorded Stellaris helper transcript decodes", "[providers][protocol][fixture]") {
    const std::string transcript = readFixture("providers/helper_transcript_stellaris.jsonl");
    REQUIRE_FALSE(transcript.empty());
    LineSplitter splitter;
    const auto lines = splitter.push(transcript);
    REQUIRE(lines.size() == 8);

    std::vector<HelperEvent> events;
    for (const auto& line : lines) {
        auto e = decodeEvent(line);
        REQUIRE(e.ok());
        events.push_back(std::move(*e));
    }
    const ReadyEvent& ready = as<ReadyEvent>(events[0]);
    CHECK(ready.ok);
    CHECK(ready.loggedOn);
    CHECK(ready.ownsApp);
    CHECK(ready.appId == "281990");
    CHECK(ready.ugcInterface == "STEAMUGC_INTERFACE_VERSION016");
    CHECK(ready.initFunction == "SteamAPI_InitSafe");
    CHECK(ready.subscribedCount == 27);
    CHECK(std::holds_alternative<PongEvent>(events[1]));

    const ItemStatus& installed = as<ItemStateEvent>(events[2]).status;
    CHECK(installed.id == "703156866");
    CHECK(decodeItemFlags(installed.flags).installedAndCurrent());
    CHECK(installed.sizeOnDisk == 429416);
    CHECK(installed.installPath ==
          R"(D:\SteamLibrary\steamapps\workshop\content\281990\703156866)");

    const ItemStatus& missing = as<ItemStateEvent>(events[6]).status;
    CHECK(missing.id == "1121692237");
    CHECK(missing.flags == 0);
    CHECK(missing.installPath.empty());

    // Re-encoding reproduces the recorded bytes exactly.
    std::string reencoded;
    for (const auto& e : events)
        reencoded += encodeEvent(e) + "\n";
    CHECK(reencoded == transcript);
}

TEST_CASE("Line splitter handles partial, CRLF, empty and oversized lines",
          "[providers][protocol]") {
    LineSplitter splitter(16);
    CHECK(splitter.push("{\"a\"").empty());
    auto lines = splitter.push(":1}\r\n\n{\"b\":2}\n{\"c");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == R"({"a":1})");
    CHECK(lines[1] == R"({"b":2})");

    lines = splitter.push(std::string(40, 'x') + "\n{\"d\":4}\n");
    REQUIRE(lines.size() == 1); // "{"c" + 40 x's exceeded the limit and was dropped
    CHECK(lines[0] == R"({"d":4})");
    CHECK(splitter.droppedLines() == 1);

    lines = splitter.push("0123456789abcdef\n");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].size() == 16);
}

TEST_CASE("Helper argument validation", "[providers][protocol]") {
    CHECK(isAcceptedSteamApiLibraryName("steam_api64.dll"));
    CHECK(isAcceptedSteamApiLibraryName("steam_api.dll"));
    CHECK_FALSE(isAcceptedSteamApiLibraryName("libsteam_api.so"));
    CHECK_FALSE(isAcceptedSteamApiLibraryName("Steam_API64.dll"));
    CHECK_FALSE(isAcceptedSteamApiLibraryName("steam_api64.dll.bak"));
    CHECK_FALSE(isAcceptedSteamApiLibraryName("evil.dll"));

    pmm::test::TempDir dir("args");
    const fs::path folder =
        dir.path() / pmm::test::u8path("Ünïcødé dir") / pmm::test::u8path("日本語");
    fs::create_directories(folder);
    const char* name = "steam_api64.dll";
    const fs::path library = folder / name;
    {
        std::ofstream(library, std::ios::binary) << "not really a library";
    }
    CHECK(validateHelperArguments("281990", library).ok());

    auto badApp = validateHelperArguments("28199O", library);
    REQUIRE_FALSE(badApp.ok());
    CHECK(badApp.error().code == ErrorCode::InvalidArgument);

    auto wrongName = validateHelperArguments("281990", folder / "steam_api_wrapper.dll");
    REQUIRE_FALSE(wrongName.ok());
    CHECK(wrongName.error().code == ErrorCode::InvalidArgument);

    auto missing = validateHelperArguments("281990", dir.path() / name);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().code == ErrorCode::NotFound);

    const fs::path dirNamedLikeLibrary = dir.path() / "sub" / name;
    fs::create_directories(dirNamedLikeLibrary);
    auto notFile = validateHelperArguments("281990", dirNamedLikeLibrary);
    REQUIRE_FALSE(notFile.ok());
    CHECK(notFile.error().code == ErrorCode::InvalidArgument);

    CHECK(exitCodeFor(HelperFailure::SteamNotRunning) == HelperExitCode::SteamNotRunning);
    CHECK(std::string(toString(HelperFailure::SymbolsMissing)) == "symbols_missing");
    CHECK(helperFailureFromString("not_logged_on") == HelperFailure::NotLoggedOn);
}
