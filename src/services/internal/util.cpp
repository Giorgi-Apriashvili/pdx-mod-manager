#include "services/internal/util.hpp"

#include <algorithm>
#include <map>
#include <tuple>

namespace pmm::services::detail {

std::string trimmed(std::string_view text) {
    const auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
    };
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return std::string(text);
}

InstalledModMap toMap(std::span<const InstalledMod> inventory) {
    InstalledModMap map;
    for (const InstalledMod& mod : inventory)
        map.emplace(mod.identity, mod);
    return map;
}

std::vector<std::string> names(std::span<const PlaysetSummary> playsets) {
    std::vector<std::string> out;
    out.reserve(playsets.size());
    for (const PlaysetSummary& summary : playsets)
        out.push_back(summary.name);
    return out;
}

std::string entryLabel(const PlaysetEntry& entry) {
    return entry.nameHint.empty() ? entry.identity.id : entry.nameHint;
}

void sortInventory(std::vector<InstalledMod>& inventory) {
    std::sort(inventory.begin(), inventory.end(), [](const InstalledMod& a, const InstalledMod& b) {
        return std::tie(a.identity.source, a.identity.id) <
               std::tie(b.identity.source, b.identity.id);
    });
}

std::string countLabel(std::span<const PlaysetEntry> entries,
                       std::span<const std::size_t> indices) {
    std::vector<std::size_t> unique(indices.begin(), indices.end());
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    if (unique.size() == 1 && unique.front() < entries.size())
        return "\"" + entryLabel(entries[unique.front()]) + "\"";
    return std::to_string(unique.size()) + (unique.size() == 1 ? " mod" : " mods");
}

std::vector<AcquisitionJob> latestPerIdentity(std::span<const AcquisitionJob> jobs) {
    std::map<ModIdentity, const AcquisitionJob*> latest;
    for (const AcquisitionJob& job : jobs) {
        auto [it, inserted] = latest.emplace(job.identity, &job);
        if (!inserted &&
            std::tie(job.updatedAt, job.id) > std::tie(it->second->updatedAt, it->second->id))
            it->second = &job;
    }
    std::vector<AcquisitionJob> out;
    out.reserve(latest.size());
    for (const auto& [identity, job] : latest)
        out.push_back(*job);
    std::sort(out.begin(), out.end(),
              [](const AcquisitionJob& a, const AcquisitionJob& b) { return a.id < b.id; });
    return out;
}

std::vector<AcquisitionJob> overlayJobs(std::vector<AcquisitionJob> persisted,
                                        std::span<const AcquisitionJob> live) {
    for (const AcquisitionJob& job : live) {
        const auto it = std::find_if(persisted.begin(), persisted.end(),
                                     [&](const AcquisitionJob& p) { return p.id == job.id; });
        if (it != persisted.end())
            *it = job;
        else
            persisted.push_back(job);
    }
    return persisted;
}

} // namespace pmm::services::detail
