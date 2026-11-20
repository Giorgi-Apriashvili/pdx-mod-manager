#pragma once

// Victoria 3 adapter (Steam app 529340). Uses the content_load.json scheme;
// the installation layout (binaries/victoria3.exe, launcher/
// launcher-settings.json), content_load.json, the mods' metadata.json and
// the Workshop layout were checked read-only against a real Windows
// installation (1.15.2); the direct launch, the in-game load order (the later
// mod wins a shared file) and the supported_game_version rule were observed
// with probe mods on 1.15.2 (docs/evidence/2026-11-21-vic3-load-order.md).
// game/userdir.txt is unverified. Details: docs/games/vic3.md.

#include "games/content_load_adapter.hpp"

namespace pmm::games {

const GameTraits& vic3Traits() noexcept;

class Vic3Adapter final : public ContentLoadJsonAdapter {
public:
    Vic3Adapter() : ContentLoadJsonAdapter(vic3Traits()) {}
};

} // namespace pmm::games
