#include "core/dependencies.hpp"

#include "core/catalog.hpp"

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

// Resolves the names one playset's entries list (the rules in the header).
class Resolver {
public:
    Resolver(std::span<const PlaysetEntry> entries, const InstalledModMap& inventory)
        : m_entries(entries), m_inventory(inventory) {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const std::string name = displayName(entries[i], inventory);
            if (!name.empty())
                m_playsetNames.add(entries[i].identity.game, name, i);
        }
        for (const auto& [identity, mod] : inventory) {
            const std::string name = trimmed(mod.descriptor.name);
            if (name.empty())
                continue;
            m_installedExact.insert(scopedKey(identity.game, name));
            m_installedFolded.insert(scopedKey(identity.game, folded(name)));
        }
    }

    // Empty when entries[i] is not installed.
    std::vector<ResolvedDependency> resolve(std::size_t i) const {
        std::vector<ResolvedDependency> out;
        const PlaysetEntry& entry = m_entries[i];
        const auto installed = m_inventory.find(entry.identity);
        if (installed == m_inventory.end())
            return out;
        const GameKey& game = entry.identity.game;

        std::set<std::string> seen;
        for (const std::string& raw : installed->second.descriptor.dependencies) {
            std::string name = trimmed(raw);
            if (name.empty() || !seen.insert(name).second)
                continue;

            std::vector<std::size_t> candidates;
            bool self = false;
            if (const auto* found = m_playsetNames.find(game, name)) {
                for (const std::size_t c : *found) {
                    if (c == i)
                        self = true;
                    else
                        candidates.push_back(c);
                }
            }
            if (candidates.empty()) {
                if (self)
                    continue; // a mod listing its own name names no other mod
                const bool isInstalled =
                    m_installedExact.count(scopedKey(game, name)) != 0 ||
                    m_installedFolded.count(scopedKey(game, folded(name))) != 0;
                out.push_back({std::move(name),
                               isInstalled ? DependencyResolution::NotInPlayset
                                           : DependencyResolution::NotInstalled,
                               std::nullopt});
                continue;
            }

            std::optional<std::size_t> enabledBefore;
            std::optional<std::size_t> enabledAfter;
            for (const std::size_t c : candidates) {
                if (!m_entries[c].enabled)
                    continue;
                if (c < i) {
                    if (!enabledBefore)
                        enabledBefore = c;
                } else if (!enabledAfter) {
                    enabledAfter = c;
                }
            }
            if (enabledBefore)
                out.push_back(
                    {std::move(name), DependencyResolution::EnabledEarlier, enabledBefore});
            else if (enabledAfter)
                out.push_back({std::move(name), DependencyResolution::EnabledLater, enabledAfter});
            else
                out.push_back(
                    {std::move(name), DependencyResolution::Disabled, candidates.front()});
        }
        return out;
    }

private:
    std::span<const PlaysetEntry> m_entries;
    const InstalledModMap& m_inventory;
    NameIndex m_playsetNames;
    std::unordered_set<std::string> m_installedExact;
    std::unordered_set<std::string> m_installedFolded;
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

std::vector<std::vector<ResolvedDependency>>
resolveDependencies(std::span<const PlaysetEntry> entries, const InstalledModMap& inventory) {
    const Resolver resolver(entries, inventory);
    std::vector<std::vector<ResolvedDependency>> out;
    out.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
        out.push_back(resolver.resolve(i));
    return out;
}

std::vector<DependencyIssue> checkDependencies(std::span<const PlaysetEntry> entries,
                                               const InstalledModMap& inventory) {
    const Resolver resolver(entries, inventory);
    std::vector<DependencyIssue> issues;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const PlaysetEntry& entry = entries[i];
        if (!entry.enabled ||
            dependencyMeaningOf(entry.identity.game) != DependencyMeaning::Required)
            continue;
        for (const ResolvedDependency& dependency : resolver.resolve(i)) {
            switch (dependency.resolution) {
            case DependencyResolution::EnabledEarlier:
                break;
            case DependencyResolution::EnabledLater:
                issues.push_back(makeIssue(i, dependency.name, DependencyIssueKind::OrderedAfter,
                                           dependency.index));
                break;
            case DependencyResolution::Disabled:
                issues.push_back(
                    makeIssue(i, dependency.name, DependencyIssueKind::Disabled, dependency.index));
                break;
            case DependencyResolution::NotInPlayset:
                issues.push_back(
                    makeIssue(i, dependency.name, DependencyIssueKind::NotInPlayset, std::nullopt));
                break;
            case DependencyResolution::NotInstalled:
                issues.push_back(
                    makeIssue(i, dependency.name, DependencyIssueKind::NotInstalled, std::nullopt));
                break;
            }
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
