#pragma once

// Europa Universalis IV adapter (Steam app 236850). Uses the dlc_load.json
// scheme; discovery, launcher-settings.json, userdir.txt, the mod inventory
// and the generated files were checked read-only against a real Windows
// installation (v1.37.5.0). Launching and the in-game load order are
// unverified. Pre-launcher-v2 versions that list mods in settings.txt are not
// supported. Details: docs/games/eu4.md.

#include "games/dlc_load_adapter.hpp"

namespace pmm::games {

const GameTraits& eu4Traits() noexcept;

class Eu4Adapter final : public DlcLoadJsonAdapter {
public:
    Eu4Adapter() : DlcLoadJsonAdapter(eu4Traits()) {}
};

} // namespace pmm::games
