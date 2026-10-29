#include "core/dependencies.hpp"

#include <algorithm>
#include <set>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace pmm {
namespace {

std::string trimmed(std::string_view text) {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return std::string(text);
}

std::string folded(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// Names are scoped to a game: the key is "<game>\x1f<name>".
std::string scopedKey(const GameKey& game, std::string_view name) {
    std::string key = game;
    key += '\x1f';
    key += name;
    return key;
}

std::string displayName(const PlaysetEntry& entry, const InstalledModMap& inventory) {
    const auto it = inventory.find(entry.identity);
    if (it != inventory.end()) {
        std::string name = trimmed(it->second.descriptor.name);
        if (!name.empty())
            return name;
    }
    return trimmed(entry.nameHint);
}

class NameIndex {
public:
    void add(const GameKey& game, const std::string& name, std::size_t position) {
        m_exact[scopedKey(game, name)].push_back(position);
        m_folded[scopedKey(game, folded(name))].push_back(position);
    }

    const std::vector<std::size_t>* find(const GameKey& game, const std::string& name) const {
        if (const auto it = m_exact.find(scopedKey(game, name)); it != m_exact.end())
            return &it->second;
        if (const auto it = m_folded.find(scopedKey(game, folded(name))); it != m_folded.end())
            return &it->second;
        return nullptr;
    }

private:
    std::unordered_map<std::string, std::vector<std::size_t>> m_exact;
    std::unordered_map<std::string, std::vector<std::size_t>> m_folded;
};

std::string position(std::size_t index) {
    return "#" + std::to_string(index + 1);
}

DependencyIssue makeIssue(std::size_t entryIndex, const std::string& name, DependencyIssueKind kind,
                          std::optional<std::size_t> dependencyIndex) {
    DependencyIssue issue{entryIndex, name, kind, dependencyIndex, {}};
    const std::string quotedName = "\"" + name + "\"";
    switch (kind) {
    case DependencyIssueKind::NotInstalled:
        issue.message = "Requires " + quotedName + ", which is not installed.";
        break;
    case DependencyIssueKind::NotInPlayset:
        issue.message = "Requires " + quotedName + ", which is installed but not in this playset.";
        break;
    case DependencyIssueKind::Disabled:
        issue.message = "Requires " + quotedName + " (" + position(*dependencyIndex) +
                        "), which is disabled in this playset.";
        break;
    case DependencyIssueKind::OrderedAfter:
        issue.message = "Requires " + quotedName + " (" + position(*dependencyIndex) +
                        "), which must load before this mod (" + position(entryIndex) +
                        "). Move it above this mod.";
        break;
    }
    return issue;
}

} // namespace

const char* toString(DependencyIssueKind kind) noexcept {
    switch (kind) {
    case DependencyIssueKind::NotInstalled:
        return "not_installed";
    case DependencyIssueKind::NotInPlayset:
        return "not_in_playset";
    case DependencyIssueKind::Disabled:
        return "disabled";
    case DependencyIssueKind::OrderedAfter:
        return "ordered_after";
    }
    return "not_installed";
}

std::vector<DependencyIssue> checkDependencies(std::span<const PlaysetEntry> entries,
                                               const InstalledModMap& inventory) {
    NameIndex playsetNames;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const std::string name = displayName(entries[i], inventory);
        if (!name.empty())
            playsetNames.add(entries[i].identity.game, name, i);
    }
    std::unordered_set<std::string> installedExact;
    std::unordered_set<std::string> installedFolded;
    for (const auto& [identity, mod] : inventory) {
        const std::string name = trimmed(mod.descriptor.name);
        if (name.empty())
            continue;
        installedExact.insert(scopedKey(identity.game, name));
        installedFolded.insert(scopedKey(identity.game, folded(name)));
    }

    std::vector<DependencyIssue> issues;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const PlaysetEntry& entry = entries[i];
        if (!entry.enabled)
            continue;
        const auto installed = inventory.find(entry.identity);
        if (installed == inventory.end())
            continue;
        const GameKey& game = entry.identity.game;

        std::set<std::string> seen;
        for (const std::string& raw : installed->second.descriptor.dependencies) {
            const std::string name = trimmed(raw);
            if (name.empty() || !seen.insert(name).second)
                continue;

            std::vector<std::size_t> candidates;
            bool self = false;
            if (const auto* found = playsetNames.find(game, name)) {
                for (const std::size_t c : *found) {
                    if (c == i)
                        self = true;
                    else
                        candidates.push_back(c);
                }
            }
            if (candidates.empty()) {
                if (self)
                    continue; // a mod listing its own name is not a missing dependency
                const bool isInstalled = installedExact.count(scopedKey(game, name)) != 0 ||
                                         installedFolded.count(scopedKey(game, folded(name))) != 0;
                issues.push_back(makeIssue(i, name,
                                           isInstalled ? DependencyIssueKind::NotInPlayset
                                                       : DependencyIssueKind::NotInstalled,
                                           std::nullopt));
                continue;
            }

            std::optional<std::size_t> enabledAfter;
            bool satisfied = false;
            for (const std::size_t c : candidates) {
                if (!entries[c].enabled)
                    continue;
                if (c < i)
                    satisfied = true;
                else if (!enabledAfter)
                    enabledAfter = c;
            }
            if (satisfied)
                continue;
            if (enabledAfter)
                issues.push_back(
                    makeIssue(i, name, DependencyIssueKind::OrderedAfter, enabledAfter));
            else
                issues.push_back(
                    makeIssue(i, name, DependencyIssueKind::Disabled, candidates.front()));
        }
    }
    return issues;
}

std::vector<DependencyIssue>
dependencyIssuesIntroducedByEnabling(std::span<const PlaysetEntry> entries,
                                     const InstalledModMap& inventory,
                                     std::span<const std::size_t> indices) {
    std::vector<PlaysetEntry> after(entries.begin(), entries.end());
    for (const std::size_t i : indices) {
        if (i < after.size())
            after[i].enabled = true;
    }

    using Key = std::tuple<std::size_t, std::string, DependencyIssueKind>;
    std::set<Key> existing;
    for (const DependencyIssue& issue : checkDependencies(entries, inventory))
        existing.emplace(issue.entryIndex, issue.dependencyName, issue.kind);

    std::vector<DependencyIssue> introduced;
    for (DependencyIssue& issue : checkDependencies(after, inventory)) {
        if (existing.count(Key{issue.entryIndex, issue.dependencyName, issue.kind}) == 0)
            introduced.push_back(std::move(issue));
    }
    return introduced;
}

} // namespace pmm
