#include "core/readiness.hpp"

#include <algorithm>
#include <map>
#include <utility>

namespace pmm {
namespace {

std::string orDefault(const std::string& text, const char* fallback) {
    return text.empty() ? std::string(fallback) : text;
}

std::string joined(const std::vector<std::string>& parts, const char* separator) {
    std::string out;
    for (const std::string& part : parts) {
        if (!out.empty())
            out += separator;
        out += part;
    }
    return out;
}

void addInstalledWarnings(const PlaysetEntry& entry, const InstalledMod& installed,
                          EntryReadiness& readiness) {
    const std::string& version = installed.descriptor.version;
    if (!entry.versionHint.empty() && !version.empty() && entry.versionHint != version)
        readiness.warnings.push_back("The shared setup used version " + entry.versionHint +
                                     "; the installed version is " + version + ".");
    if (!entry.fingerprint.empty() && !installed.fingerprint.empty() &&
        entry.fingerprint != installed.fingerprint)
        readiness.warnings.push_back(
            "The installed files differ from the shared setup (different fingerprint).");
    for (const std::string& problem : installed.problems)
        readiness.warnings.push_back(problem);
}

double downloadProgress(const AcquisitionJob& job) {
    if (job.bytesTotal == 0)
        return -1.0; // indeterminate
    return std::min(1.0,
                    static_cast<double>(job.bytesDownloaded) / static_cast<double>(job.bytesTotal));
}

EntryReadiness fromJob(const PlaysetEntry& entry, const AcquisitionJob& job) {
    EntryReadiness r;
    switch (job.state) {
    case AcquisitionState::Pending:
        r.status = EntryStatus::Pending;
        r.detail = "Queued for download.";
        break;
    case AcquisitionState::Subscribing:
        r.status = EntryStatus::Pending;
        r.detail = "Subscribing.";
        break;
    case AcquisitionState::Subscribed:
        r.status = EntryStatus::Pending;
        r.detail = "Subscribed; waiting for the download to start.";
        break;
    case AcquisitionState::DownloadRequested:
        r.status = EntryStatus::Pending;
        r.detail = "Download requested; waiting for the provider.";
        break;
    case AcquisitionState::Downloading:
        r.status = EntryStatus::Downloading;
        r.detail = "Downloading.";
        r.progress = downloadProgress(job);
        break;
    case AcquisitionState::Installed:
        // The provider says it is done, but the inventory has not seen usable
        // content yet: never report Ready on the provider's word alone.
        r.status = EntryStatus::Pending;
        r.detail = "Download finished; verifying the installed files.";
        break;
    case AcquisitionState::Failed:
        r.status = EntryStatus::Failed;
        r.detail = orDefault(job.errorMessage,
                             "The download failed. Check that the provider is running and online, "
                             "then retry.");
        break;
    case AcquisitionState::Unavailable:
        r.status = EntryStatus::Unavailable;
        r.detail = orDefault(job.errorMessage,
                             "This mod cannot be downloaded: it may have been removed, made "
                             "private, or belong to another game.");
        break;
    case AcquisitionState::NeedsUserAction:
        r.status = EntryStatus::NeedsUserAction;
        r.detail = orDefault(job.errorMessage, entry.identity.source == source::Local
                                                   ? "Map or install this local mod manually."
                                                   : "Action required to acquire this mod.");
        break;
    case AcquisitionState::Cancelled:
        r.status = EntryStatus::Missing;
        r.detail = "The download was cancelled. Retry to acquire this mod.";
        break;
    }
    return r;
}

const char* statusLabel(EntryStatus status) noexcept {
    switch (status) {
    case EntryStatus::Ready:
        return "ready";
    case EntryStatus::Missing:
        return "missing";
    case EntryStatus::Pending:
        return "pending";
    case EntryStatus::Downloading:
        return "downloading";
    case EntryStatus::Failed:
        return "download failed";
    case EntryStatus::Unavailable:
        return "unavailable";
    case EntryStatus::NeedsUserAction:
        return "needs your action";
    case EntryStatus::Broken:
        return "broken";
    }
    return "not ready";
}

std::string countText(std::size_t count, const char* singular, const char* plural) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

} // namespace

EntryReadiness computeReadiness(const PlaysetEntry& entry, const InstalledMod* installed,
                                const AcquisitionJob* job) {
    if (job && job->identity != entry.identity)
        job = nullptr;

    EntryReadiness r;
    if (installed) {
        switch (installed->state) {
        case InstallState::Broken:
            r.status = EntryStatus::Broken;
            r.detail = installed->problems.empty()
                           ? std::string("The installed content is missing or its descriptor is "
                                         "invalid.")
                           : joined(installed->problems, "; ");
            return r;
        case InstallState::Downloading:
            r.status = EntryStatus::Downloading;
            r.detail = "The provider is updating this mod.";
            if (job && job->state == AcquisitionState::Downloading)
                r.progress = downloadProgress(*job);
            return r;
        case InstallState::NeedsUpdate:
            r.warnings.push_back("An update is pending; the currently installed version will be "
                                 "used.");
            [[fallthrough]];
        case InstallState::Ready:
            r.status = EntryStatus::Ready;
            r.detail = "Installed.";
            addInstalledWarnings(entry, *installed, r);
            return r;
        }
    }

    if (job)
        return fromJob(entry, *job);

    if (entry.identity.source == source::SteamWorkshop) {
        r.status = EntryStatus::Missing;
        r.detail = "Not installed. Download it from Steam Workshop.";
    } else {
        r.status = EntryStatus::NeedsUserAction;
        r.detail = "Local mods cannot be downloaded. Map this entry to an installed local mod or "
                   "install it manually.";
    }
    return r;
}

std::vector<EntryReadiness> computePlaysetReadiness(std::span<const PlaysetEntry> entries,
                                                    const InstalledModMap& inventory,
                                                    std::span<const AcquisitionJob> jobs) {
    std::map<ModIdentity, const AcquisitionJob*> latest;
    for (const AcquisitionJob& job : jobs) {
        auto& slot = latest[job.identity];
        if (!slot || std::pair(job.updatedAt, job.id) > std::pair(slot->updatedAt, slot->id))
            slot = &job;
    }

    std::vector<EntryReadiness> result;
    result.reserve(entries.size());
    for (const PlaysetEntry& entry : entries) {
        const auto installed = inventory.find(entry.identity);
        const auto job = latest.find(entry.identity);
        result.push_back(
            computeReadiness(entry, installed != inventory.end() ? &installed->second : nullptr,
                             job != latest.end() ? job->second : nullptr));
    }
    for (const DependencyIssue& issue : checkDependencies(entries, inventory))
        result[issue.entryIndex].warnings.push_back(issue.message);
    return result;
}

LaunchReadiness evaluateLaunch(std::span<const PlaysetEntry> entries,
                               std::span<const EntryReadiness> readiness) {
    LaunchReadiness out;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const PlaysetEntry& entry = entries[i];
        const EntryReadiness* r = i < readiness.size() ? &readiness[i] : nullptr;
        const bool ready = r && r->status == EntryStatus::Ready;
        if (!entry.enabled) {
            if (!ready)
                ++out.unresolvedDisabledCount;
            continue;
        }
        ++out.enabledCount;
        if (ready)
            continue;

        LaunchBlocker blocker;
        blocker.index = i;
        blocker.name = entry.nameHint.empty() ? entry.identity.id : entry.nameHint;
        blocker.status = r ? r->status : EntryStatus::Missing;
        blocker.reason = "#" + std::to_string(i + 1) + " " + blocker.name + ": " +
                         (r ? statusLabel(r->status) : "status unknown");
        if (r && !r->detail.empty())
            blocker.reason += " - " + r->detail;
        out.blockers.push_back(std::move(blocker));
    }

    out.canLaunch = out.blockers.empty();
    if (out.canLaunch) {
        out.summary = out.enabledCount == 0
                          ? std::string("Ready to launch without mods.")
                          : "Ready to launch with " +
                                countText(out.enabledCount, "enabled mod.", "enabled mods.");
        if (out.unresolvedDisabledCount > 0)
            out.summary += " " +
                           countText(out.unresolvedDisabledCount, "disabled entry is",
                                     "disabled entries are") +
                           " unresolved; disabled entries do not block launching.";
    } else {
        out.summary = countText(out.blockers.size(), "enabled mod is", "enabled mods are") +
                      " not ready. Resolve or disable them to play.";
    }
    return out;
}

} // namespace pmm
