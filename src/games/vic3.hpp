#pragma once

// Victoria 3 adapter (Steam app 529340). Uses the content_load.json scheme;
// the installation layout (binaries/victoria3.exe, launcher/
// launcher-settings.json), content_load.json, the mods' metadata.json and
// the Workshop layout were checked read-only against a real Windows
// installation (1.15.2). Launching, game/userdir.txt, the in-game load order
// and the supported_game_version rule are unverified. Details:
// docs/games/vic3.md.

#include "games/content_load_adapter.hpp"

namespace pmm::games {

const GameTraits& vic3Traits() noexcept;

class Vic3Adapter final : public ContentLoadJsonAdapter {
public:
    Vic3Adapter() : ContentLoadJsonAdapter(vic3Traits()) {}
};

} // namespace pmm::games
