#include "services/internal/util.hpp"

#include "sharing/utf8.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace pmm::services::detail {
namespace {

bool isAsciiSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string asciiFolded(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

} // namespace

std::string trimmed(std::string_view text) {
    while (!text.empty() && isAsciiSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isAsciiSpace(text.back()))
        text.remove_suffix(1);
    return std::string(text);
}

std::string_view utf8Prefix(std::string_view text, std::size_t maxBytes) {
    if (text.size() <= maxBytes)
        return text;
    std::size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut; // continuation byte: the character starts earlier
    return text.substr(0, cut);
}

Result<std::string> checkedPlaysetName(std::string_view name) {
    std::string out = trimmed(name);
    if (!sharing::isValidUtf8(out) || sharing::containsControlCharacters(out))
        return makeError(ErrorCode::InvalidArgument,
                         "A playset name must be plain text without control characters.");
    if (out.size() > sharing::kMaxPlaysetNameBytes)
        return makeError(ErrorCode::InvalidArgument,
                         "A playset name can be at most " +
                             std::to_string(sharing::kMaxPlaysetNameBytes) +
                             " bytes long (so that it fits into share codes).");
    return out;
}

std::string uniquePlaysetName(std::string_view name, std::span<const std::string> existingNames,
                              std::string_view suffix, std::size_t maxBytes) {
    const std::string_view base = name.empty() ? std::string_view("Imported playset") : name;
    std::set<std::string> taken;
    for (const std::string& existing : existingNames)
        taken.insert(asciiFolded(existing));
    const auto candidate = [&](const std::string& counter) {
        const std::size_t tail = suffix.size() + counter.size();
        std::string_view stem = utf8Prefix(base, maxBytes > tail ? maxBytes - tail : 0);
        if (stem.size() < base.size()) {
            while (!stem.empty() && isAsciiSpace(stem.back()))
                stem.remove_suffix(1);
        }
        std::string out(stem);
        out.append(suffix);
        out.append(counter);
        return out;
    };
    std::string result = candidate({});
    for (std::size_t n = 2; taken.count(asciiFolded(result)) != 0; ++n)
        result = candidate(" (" + std::to_string(n) + ")");
    return result;
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
