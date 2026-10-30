#include "core/log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

TEST_CASE("Redaction hides the home directory in both slash styles", "[core][log]") {
    const pmm::RedactionContext ctx{R"(C:\Users\Alice Example)", "Alice Example"};
    const auto out = pmm::redact(
        R"(opened C:\Users\Alice Example\Documents\x.txt and C:/Users/Alice Example/y)", ctx);
    CHECK(out.find("Alice") == std::string::npos);
    CHECK(out.find(R"(~\Documents\x.txt)") != std::string::npos);
    CHECK(out.find("~/y") != std::string::npos);
}

TEST_CASE("Redaction removes credential-looking values", "[core][log]") {
    const pmm::RedactionContext ctx{"C:/Users/bob", "bob"};
    const auto out = pmm::redact("url?token=abc123&x=1 password: hunter2 api_key=\"q w\"", ctx);
    CHECK(out.find("abc123") == std::string::npos);
    CHECK(out.find("hunter2") == std::string::npos);
    CHECK(out.find("q w") == std::string::npos);
    CHECK(out.find("x=1") != std::string::npos);
}

TEST_CASE("Log sink receives structured records above the minimum level", "[core][log]") {
    std::vector<pmm::LogRecord> captured;
    pmm::setLogSink([&](const pmm::LogRecord& r) { captured.push_back(r); });
    pmm::setMinimumLogLevel(pmm::LogLevel::Info);
    pmm::logDebug("test", "hidden");
    pmm::logInfo("test", "shown", {{"count", "3"}, {"name", "two words"}});
    pmm::setLogSink({});
    REQUIRE(captured.size() == 1);
    CHECK(captured[0].message == "shown");
    const auto line = pmm::formatLogRecord(captured[0]);
    CHECK(line.find("INFO test shown count=3 name=\"two words\"") != std::string::npos);
}
