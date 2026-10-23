#pragma once
// The ways game files can be brought into the app's data folder. Each is an
// ImportProvider; the UI lists whichever ones exist on this platform and tells the
// player plainly when one is refused (Xbox restricts several of these, see
// docs/xbox-uwp.md, and none has been tried on a console yet).

#include "game_files.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace nl {

struct ImportProvider {
    std::string title;      // "Import from a USB drive"
    std::string subtitle;   // one line under it
    bool in_place = false;  // leaves the files where they are (writes data_dir.txt) instead of copying them
    // Empty when the provider can run; otherwise a short reason to show the player.
    std::function<std::string()> unavailable_reason;
    // Optional. Runs on a worker thread at start-up when the game files are missing; returns a short
    // description of where files were found ("drive E:") or an empty string. When it finds something
    // the launcher starts this provider's import by itself.
    std::function<std::string()> probe;
    // Runs on a worker thread; reports through `progress`.
    std::function<CopyOutcome(CopyProgress &progress, const std::filesystem::path &data_root)> run;
};

// Providers that work on any platform with a real file system.
std::vector<ImportProvider> make_portable_providers();

// Providers that need the platform's own APIs (the UWP file picker, removable
// storage). Defined per platform; the default build adds none.
void add_platform_providers(std::vector<ImportProvider> &out);

}  // namespace nl
