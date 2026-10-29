// pmm_platform_echo: child process used by the startDetached() tests. Writes
// its arguments, working directory and $PMM_ECHO_VAR as JSON to the file named
// by $PMM_ECHO_OUTPUT (via a temporary file and rename, so the test never sees
// a partial file).

#include "platform/fs.hpp"
#include "platform/paths.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

int run(const std::vector<std::string>& arguments) {
    namespace fs = std::filesystem;
    using pmm::platform::environmentVariable;
    using pmm::platform::pathFromUtf8;
    using pmm::platform::pathToUtf8;

    const auto output = environmentVariable("PMM_ECHO_OUTPUT");
    if (!output || output->empty())
        return 2;
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);

    nlohmann::json report = nlohmann::json::object();
    report["args"] = arguments;
    report["cwd"] = pathToUtf8(cwd);
    const auto value = environmentVariable("PMM_ECHO_VAR");
    report["env"] = value ? nlohmann::json(*value) : nlohmann::json(nullptr);

    const fs::path target = pathFromUtf8(*output);
    return pmm::platform::writeFileAtomic(target, report.dump()).ok() ? 0 : 3;
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i)
        arguments.push_back(pmm::platform::pathToUtf8(std::filesystem::path(argv[i])));
    return run(arguments);
}
#else
int main(int argc, char** argv) {
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
