#include "platform/fs.hpp"
#include "platform/paths.hpp"

#include <catch2/catch_test_macros.hpp>
#include <windows.h>

#include <string>

using namespace pmm::platform;

namespace {

void setVariable(const std::string& name, const std::string* value) {
    const fs::path wideName = pathFromUtf8(name);
    const fs::path wideValue = value ? pathFromUtf8(*value) : fs::path();
    SetEnvironmentVariableW(wideName.c_str(), value ? wideValue.c_str() : nullptr);
}

} // namespace

TEST_CASE("standardDirectories returns per-user locations for this OS", "[platform][paths]") {
    const StandardDirectories dirs = standardDirectories();
    CHECK(fs::is_directory(dirs.home));
    CHECK_FALSE(dirs.documents.empty());
    CHECK(dirs.appData.filename() == kAppFolderName);
    CHECK(dirs.cache.filename() == "cache");
    CHECK(dirs.cache.parent_path().filename() == kAppFolderName);
    CHECK(dirs.logs.filename() == "logs");
}

TEST_CASE("environmentVariable reads UTF-8 values and distinguishes unset", "[platform][paths]") {
    const std::string name = "PMM_PLATFORM_TEST_VARIABLE";
    const std::string value = "välue with spaces 日本語";
    setVariable(name, &value);
    CHECK(environmentVariable(name) == value);

    const std::string longValue(5000, 'L'); // larger than the initial buffer
    setVariable(name, &longValue);
    CHECK(environmentVariable(name) == longValue);

    setVariable(name, nullptr);
    CHECK_FALSE(environmentVariable(name).has_value());
    CHECK_FALSE(environmentVariable("").has_value());
}
