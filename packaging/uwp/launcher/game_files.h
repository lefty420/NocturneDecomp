// What the launcher expects to find in the data folder, and what is actually there.
//
// The game reads its data relative to its working directory: ACT1.POD and friends
// at the top, system\nocturne.ini, video\opening.avi, save\. The list below comes
// from the file names in the decompiled code; it is a best guess, so a user can
// extend or correct it by dropping a launcher_manifest.txt next to the game files
// (one "required|recommended|optional <path>" per line).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nl {

enum class Need { Required, Recommended, Optional };

struct FileStatus {
    std::string rel;     // as the game spells it, e.g. "VIDEO\\opening.avi"
    std::string note;    // what it is for
    Need need;
    bool present = false;
    uint64_t size = 0;
};

struct Inventory {
    std::string root;
    std::vector<FileStatus> files;
    std::vector<std::string> other_pods;   // .pod files found that are not in the list
    int required_total = 0, required_present = 0;
    int recommended_total = 0, recommended_present = 0;
    uint64_t total_bytes = 0;
    int total_files = 0;
    bool ready = false;      // every Required file present
    bool complete = false;   // Required and Recommended all present
    bool any = false;        // anything at all in the folder
};

Inventory scan_game_files(const std::string &root);

}  // namespace nl
