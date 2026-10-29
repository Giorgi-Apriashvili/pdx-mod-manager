#pragma once

// Plain-text diagnostics report for "Copy diagnostics": application and
// protocol versions, OS, per game the installation summary and version,
// inventory counts, playsets, recent acquisition jobs and errors, recent
// launches and interrupted configuration transactions. The whole text is
// passed through core redact() (home directory -> "~", user name -> "<user>",
// credential-like key=value pairs stripped) before it is returned.

#include "core/log.hpp"
#include "services/acquisition_coordinator.hpp"
#include "services/service_context.hpp"
#include "sharing/share_code.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace pmm::services {

struct DiagnosticsOptions {
    std::string appVersion;
    int protocolVersion = sharing::kProtocolVersion;
    // Live provider/job state from the coordinator, when available.
    std::vector<AcquisitionSnapshot> acquisition;
    std::size_t recentJobs = 10;
    std::size_t recentLaunches = 5;
    // Defaults to currentRedactionContext().
    std::optional<RedactionContext> redaction;
};

std::string diagnosticsText(const ServiceContext& context, const DiagnosticsOptions& options);

} // namespace pmm::services
