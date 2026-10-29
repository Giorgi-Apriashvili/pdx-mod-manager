#pragma once

// Europa Universalis IV adapter (Steam app 236850). EU4 is not installed on
// the development machine: the dlc_load.json/mod/*.mod formats are taken from
// leftover launcher output for EU4 v1.37.5.0 found there, everything else
// (launcher-settings.json, executable, in-game behaviour) is unverified.
// Pre-launcher-v2 versions that list mods in settings.txt are not supported.
// Details: docs/games/eu4.md.

#include "games/dlc_load_adapter.hpp"

namespace pmm::games {

const GameTraits& eu4Traits() noexcept;

class Eu4Adapter final : public DlcLoadJsonAdapter {
public:
    Eu4Adapter() : DlcLoadJsonAdapter(eu4Traits()) {}
};

} // namespace pmm::games
