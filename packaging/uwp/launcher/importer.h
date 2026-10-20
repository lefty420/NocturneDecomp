// Adding the user's own copy of the game to the app's data folder.
//
// The launcher talks to an ImportService. On UWP the platform service (WinRT file
// pickers, removable storage, DevelopmentFiles) is in import_winrt.cpp; anywhere
// else a plain folder-to-folder service is used, which is also what the tests run.
// The layout rules and the copy loop are shared, so every route ends up with the
// same files in the same places.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nl {

struct ImportMethod {
    std::string id;
    std::string label;
    std::string description;
    bool available = true;
    std::string unavailable_reason;
};

enum class ImportState { Idle, Choosing, Working, Done, Failed, Cancelled };

struct ImportProgress {
    ImportState state = ImportState::Idle;
    int files_done = 0, files_total = 0;
    uint64_t bytes_done = 0, bytes_total = 0;
    std::string current;   // file being copied
    std::string message;   // result or error, in plain words
};

class ImportService {
public:
    virtual ~ImportService() = default;
    virtual std::vector<ImportMethod> methods() = 0;
    virtual void start(const std::string &method_id) = 0;   // returns at once; poll progress()
    virtual ImportProgress progress() = 0;                  // thread-safe snapshot
    virtual void cancel() = 0;
    virtual void reset() = 0;                               // back to Idle after Done/Failed/Cancelled
};

// ---- shared layout rules -----------------------------------------------------

// Files that are never copied (installers, Windows binaries the port replaces, shortcuts).
bool import_skip(const std::string &name);
// Where a loose file (chosen one by one, without its folder) belongs: "*.pod" at the
// top, "*.avi" under video\, "*.ini" under system\, anything else at the top.
std::string import_dest_for_loose(const std::string &name);

struct PlanItem {
    std::string src;   // full source path
    std::string rel;   // destination relative to the data folder, '/' separated
    uint64_t size = 0;
};
struct ImportPlan {
    std::vector<PlanItem> items;
    uint64_t total_bytes = 0;
    std::string game_root;   // the folder found to hold ACT1.POD / ENEMY.POD
    std::string error;       // non-empty: nothing to copy, and why
};

// Walks `src_root` (looking up to three levels down for the folder holding the .POD
// files) and lists what to copy, preserving sub-folders.
ImportPlan plan_folder_import(const std::string &src_root);

// Copies a plan into `data_root` on a worker thread, with progress and cancel.
std::unique_ptr<ImportService> make_folder_import_service(const std::string &data_root, const std::string &source_dir);

// UWP only (import_winrt.cpp); nullptr elsewhere.
std::unique_ptr<ImportService> make_platform_import_service(const std::string &data_root);

}  // namespace nl
