#include "game_files.h"

#include <fstream>

#include "fs_util.h"

namespace nl {

namespace {

std::vector<FileStatus> default_list() {
    return {
        {"ACT1.POD", "Act 1 data", Need::Required},
        {"ENEMY.POD", "Enemy data", Need::Required},
        {"ACT2.POD", "Act 2 data", Need::Recommended},
        {"VIDEO\\opening.avi", "Opening movie", Need::Recommended},
        {"SYSTEM\\nocturne.ini", "Settings - the game creates this on first run", Need::Optional},
    };
}

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

void load_manifest(const std::string &root, std::vector<FileStatus> &list) {
    std::string path = find_ci(root, "launcher_manifest.txt");
    if (path.empty()) return;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t sp = line.find_first_of(" \t");
        if (sp == std::string::npos) continue;
        std::string kind = lower(line.substr(0, sp)), rest = trim(line.substr(sp));
        Need need;
        if (kind == "required") need = Need::Required;
        else if (kind == "recommended") need = Need::Recommended;
        else if (kind == "optional") need = Need::Optional;
        else continue;
        std::string note;
        size_t hash = rest.find('#');
        if (hash != std::string::npos) {
            note = trim(rest.substr(hash + 1));
            rest = trim(rest.substr(0, hash));
        }
        bool dup = false;
        for (FileStatus &f : list)
            if (lower(f.rel) == lower(rest)) {
                f.need = need;
                dup = true;
            }
        if (!dup && !rest.empty()) list.push_back({rest, note.empty() ? "From launcher_manifest.txt" : note, need});
    }
}

}  // namespace

Inventory scan_game_files(const std::string &root) {
    Inventory inv;
    inv.root = root;
    inv.files = default_list();
    load_manifest(root, inv.files);

    for (FileStatus &f : inv.files) {
        std::string p = find_ci(root, f.rel);
        f.present = !p.empty() && !fs::is_directory(p);
        f.size = f.present ? file_size_of(p) : 0;
        if (f.need == Need::Required) {
            ++inv.required_total;
            if (f.present) ++inv.required_present;
        } else if (f.need == Need::Recommended) {
            ++inv.recommended_total;
            if (f.present) ++inv.recommended_present;
        }
    }

    std::error_code ec;
    for (fs::recursive_directory_iterator it(root.empty() ? "." : root, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        ++inv.total_files;
        inv.total_bytes += (uint64_t)it->file_size(e2);
        std::string name = it->path().filename().string();
        if (it->path().parent_path() == fs::path(root.empty() ? "." : root) && lower(name).size() > 4 &&
            lower(name).substr(lower(name).size() - 4) == ".pod") {
            bool known = false;
            for (const FileStatus &f : inv.files)
                if (lower(f.rel) == lower(name)) known = true;
            if (!known) inv.other_pods.push_back(name);
        }
    }
    inv.any = inv.total_files > 0;
    inv.ready = inv.required_present == inv.required_total;
    inv.complete = inv.ready && inv.recommended_present == inv.recommended_total;
    return inv;
}

}  // namespace nl
