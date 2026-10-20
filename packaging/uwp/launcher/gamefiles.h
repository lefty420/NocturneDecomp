// =============================================================================
// GAME FILES — what the launcher expects to find, and how to check it
// =============================================================================
//
// No game data ships with this project. The player copies their own Nocturne
// install into the app's local folder; this module only looks at what is there.
//
// The layout is the original install's: the game opens "art/", "models/",
// "data/", "sound/" and "video/opening.avi" relative to its working directory.
// Matching is case-insensitive, because the original ran on Windows.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace launcher {

struct Component {
    const char *dir;       // folder under the game root, e.g. "art"
    const char *file;      // when non-null, a single file inside dir that must exist
    const char *title;
    const char *detail;
    bool required;
};

struct ComponentState {
    const Component *comp = nullptr;
    bool present = false;
    int files = 0;
    uint64_t bytes = 0;
};

struct Scan {
    std::string root;
    std::vector<ComponentState> items;
    bool ready = false;          // every required component present
    int missingRequired = 0;
    int totalFiles = 0;
    uint64_t totalBytes = 0;
};

const std::vector<Component> &components();

// Looks inside `root` (case-insensitively) and reports each component.
Scan scan_game_files(const std::string &root);

// `candidate` is a folder the player picked. It may be the game root itself, or a
// folder that contains it ("Nocturne/", "Games/Nocturne/"). Returns the folder that
// holds the game's directories, searching up to two levels down; empty if none.
std::string find_game_root(const std::string &candidate);

// Case-insensitive child lookup; returns the real child path or empty.
std::string find_child(const std::string &dir, const std::string &name);

std::string human_bytes(uint64_t bytes);

}  // namespace launcher
