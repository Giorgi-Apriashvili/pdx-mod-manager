#include "services/service_context.hpp"

#include "core/catalog.hpp"
#include "core/log.hpp"
#include "providers/local_fingerprint.hpp"

namespace pmm::services {

const games::GameAdapter* ServiceContext::adapterFor(std::string_view gameKey) const noexcept {
    for (const games::GameAdapter* adapter : adapters) {
        if (adapter && adapter->key() == gameKey)
            return adapter;
    }
    return nullptr;
}

std::int64_t ServiceContext::now() const {
    return clock ? clock() : nowUnixMs();
}

void useSystemDefaults(ServiceContext& context) {
    context.clock = [] { return nowUnixMs(); };
    context.startProcess = [](const platform::ProcessSpec& spec) {
        return platform::startDetached(spec);
    };
    context.listProcesses = [] { return platform::listProcesses(); };
    context.fingerprint = [](const std::filesystem::path& root) {
        return providers::fingerprintDirectory(root);
    };
}

namespace settings_key {
std::string activePlayset(const GameKey& game) {
    return "game." + game + ".activePlaysetId";
}
} // namespace settings_key

std::string gameDisplayName(const GameKey& game) {
    const GameCatalogEntry* entry = findGame(game);
    return entry ? std::string(entry->displayName) : game;
}

} // namespace pmm::services
