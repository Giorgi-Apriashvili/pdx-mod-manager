#pragma once

// Crusader Kings III adapter (Steam app 1158310). Uses the dlc_load.json
// scheme; the installation layout (binaries/ck3.exe, launcher/
// launcher-settings.json), dlc_load.json and the launcher-written descriptors
// were checked read-only against a real Windows installation (1.20.1.2), and
// the supported_version rule against the error.log of a 1.19 build.
// Launching, game/userdir.txt and the in-game load order are unverified.
// Details: docs/games/ck3.md.

#include "games/dlc_load_adapter.hpp"

namespace pmm::games {

const GameTraits& ck3Traits() noexcept;

class Ck3Adapter final : public DlcLoadJsonAdapter {
public:
    Ck3Adapter() : DlcLoadJsonAdapter(ck3Traits()) {}
};

} // namespace pmm::games
