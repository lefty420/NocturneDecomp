#include "imports.h"

#include <cstdlib>
#include <system_error>

namespace fs = std::filesystem;

namespace nl {

namespace {

// Candidate source folders that need no picker: a folder in the dev-mode share, or a
// drive that is already visible as a path.
std::vector<fs::path> candidate_sources() {
    std::vector<fs::path> c;
    if (const char *env = std::getenv("NOCTURNE_IMPORT_DIR")) c.push_back(env);   // for tests
#ifdef _WIN32
    c.push_back("D:\\DevelopmentFiles\\Nocturne");
    c.push_back("D:\\DevelopmentFiles");
    for (char d = 'E'; d <= 'Z'; ++d) {
        c.push_back(fs::path(std::string(1, d) + ":\\"));
    }
#endif
    return c;
}

fs::path first_game_folder() {
    for (const fs::path &p : candidate_sources()) {
        std::error_code ec;
        if (!fs::is_directory(p, ec)) continue;
        const fs::path root = find_game_root(p);
        if (!root.empty()) return root;
    }
    return {};
}

}  // namespace

std::vector<ImportProvider> make_portable_providers() {
    std::vector<ImportProvider> v;
    ImportProvider p;
    p.title = "Import from a folder or drive";
    p.subtitle = "Looks in DevelopmentFiles and any attached drive";
    p.unavailable_reason = []() -> std::string {
        if (first_game_folder().empty())
            return "No Nocturne folder was found in DevelopmentFiles or on an attached drive. "
                   "Put your game folder there (or use Device Portal), then try again.";
        return {};
    };
    p.run = [](CopyProgress &progress, const fs::path &data_root) -> CopyOutcome {
        const fs::path src = first_game_folder();
        if (src.empty()) {
            progress.set_error("The folder disappeared before the copy started.");
            return CopyOutcome::Failed;
        }
        return copy_tree(src, data_root, progress);
    };
    v.push_back(std::move(p));
    return v;
}

}  // namespace nl
