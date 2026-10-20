#pragma once
// What the game needs on disk, how to tell whether it is there, and how to copy it in.
// No SDL and no UI here, so it can be tested on its own.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace nl {

// One thing the game wants in its data folder. PROVISIONAL: these names come from the
// paths the decompiled code opens (art/*.raw, world/*.msn, ...), not from a checked
// retail install. Edit kChecks in game_files.cpp if the real layout differs.
struct FileCheck {
    const char *label;     // shown to the player
    const char *dir;       // folder under the data root
    const char *pattern;   // file pattern inside it ("*" for anything)
    int min_count;         // how many matching files make it "present"
    bool required;         // optional items never block Play
};

enum class CheckState { Ok, Missing, OptionalMissing };

struct CheckResult {
    const FileCheck *check = nullptr;
    CheckState state = CheckState::Missing;
    int files = 0;
    uint64_t bytes = 0;
};

struct ScanResult {
    std::vector<CheckResult> items;
    bool ready = false;            // every required item present
    int missing_required = 0;
    int total_files = 0;
    uint64_t total_bytes = 0;
};

const std::vector<FileCheck> &game_file_checks();

// Counts files under `root` against the table. Folder and file names compare
// case-insensitively, like the Windows file system the game came from.
ScanResult scan_game_files(const std::filesystem::path &root);

// Given a folder the player picked, finds the folder that actually holds the game's
// directories: the folder itself, or one or two levels down. Returns an empty path when
// nothing under it looks like the game.
std::filesystem::path find_game_root(const std::filesystem::path &picked);

// Progress shared between the copy thread and the UI.
struct CopyProgress {
    std::atomic<uint64_t> bytes_done{0};
    std::atomic<uint64_t> bytes_total{0};
    std::atomic<int> files_done{0};
    std::atomic<int> files_total{0};
    std::atomic<int> files_skipped{0};
    std::atomic<bool> cancel{false};
    std::mutex mutex;              // guards `current` and `error`
    std::string current;
    std::string error;
    std::string current_copy() { std::lock_guard<std::mutex> l(mutex); return current; }
    std::string error_copy() { std::lock_guard<std::mutex> l(mutex); return error; }
    void set_current(const std::string &s) { std::lock_guard<std::mutex> l(mutex); current = s; }
    void set_error(const std::string &s) { std::lock_guard<std::mutex> l(mutex); error = s; }
};

enum class CopyOutcome { Done, Cancelled, Failed };

// Copies the whole tree under `src` into `dst`, keeping relative paths. A file that is
// already in `dst` with the same size is skipped, so an interrupted copy can be resumed.
CopyOutcome copy_tree(const std::filesystem::path &src, const std::filesystem::path &dst, CopyProgress &progress);

// "1,234" style number and "812 MB" style size, for display.
std::string format_count(uint64_t n);
std::string format_bytes(uint64_t n);

}  // namespace nl
