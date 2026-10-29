#pragma once

// Stellaris adapter (Steam app 281990). Uses the dlc_load.json scheme; the
// file formats, launcher-settings.json and the supported_version rule were
// observed on a real Windows installation (v4.5.2), read-only. Linux/macOS
// paths are unverified. Details: docs/games/stellaris.md.

#include "games/dlc_load_adapter.hpp"

namespace pmm::games {

const GameTraits& stellarisTraits() noexcept;

class StellarisAdapter final : public DlcLoadJsonAdapter {
public:
    StellarisAdapter() : DlcLoadJsonAdapter(stellarisTraits()) {}
};

} // namespace pmm::games
