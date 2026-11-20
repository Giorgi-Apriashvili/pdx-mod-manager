#pragma once

// Hearts of Iron IV adapter (Steam app 394360). Uses the dlc_load.json
// scheme; the installation layout (hoi4.exe and launcher-settings.json in the
// installation folder, as for Stellaris), dlc_load.json and the
// launcher-written descriptors were checked read-only against a real Windows
// installation (1.20.0.0), and the supported_version rule against the
// error.log of a 1.19.5.0 run. The user-data precedence (gameDataPath before
// userdir.txt) is inferred from strings in hoi4.exe. Launching and the
// in-game load order are unverified. Details: docs/games/hoi4.md.

#include "games/dlc_load_adapter.hpp"

namespace pmm::games {

const GameTraits& hoi4Traits() noexcept;

class Hoi4Adapter final : public DlcLoadJsonAdapter {
public:
    Hoi4Adapter() : DlcLoadJsonAdapter(hoi4Traits()) {}
};

} // namespace pmm::games
