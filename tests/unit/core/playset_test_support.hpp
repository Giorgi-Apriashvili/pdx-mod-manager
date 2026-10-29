#pragma once

// Builders for playset entries and installed mods used by the playset logic
// tests. Identifiers and names are placeholders.

#include "core/dependencies.hpp"
#include "core/model.hpp"

#include <string>
#include <utility>
#include <vector>

namespace pmm::test {

inline PlaysetEntry workshop(std::string id, std::string name, bool enabled = true,
                             GameKey game = "stellaris") {
    PlaysetEntry entry;
    entry.identity = ModIdentity{std::move(game), source::SteamWorkshop, std::move(id)};
    entry.nameHint = std::move(name);
    entry.enabled = enabled;
    return entry;
}

inline PlaysetEntry local(std::string id, std::string name, bool enabled = true,
                          GameKey game = "stellaris") {
    PlaysetEntry entry = workshop(std::move(id), std::move(name), enabled, std::move(game));
    entry.identity.source = source::Local;
    return entry;
}

inline InstalledMod installed(const ModIdentity& identity, std::string name,
                              std::vector<std::string> dependencies = {},
                              InstallState state = InstallState::Ready) {
    InstalledMod mod;
    mod.identity = identity;
    mod.descriptor.name = std::move(name);
    mod.descriptor.dependencies = std::move(dependencies);
    mod.state = state;
    return mod;
}

inline void add(InstalledModMap& inventory, InstalledMod mod) {
    const ModIdentity key = mod.identity;
    inventory[key] = std::move(mod);
}

} // namespace pmm::test
