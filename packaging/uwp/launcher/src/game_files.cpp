#include "game_files.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace nl {

namespace {

const std::vector<FileCheck> kChecks = {
    {"Artwork and textures", "art", "*.raw", 1, true},
    {"3D models", "models", "*", 1, true},
    {"Levels and missions", "world", "*.msn", 1, true},
    {"Game data", "data", "*", 1, true},
    {"Backdrops", "backdrop", "*.pvs", 1, true},
    {"Intro movie", "video", "*.avi", 1, false},
};

std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// '*' and '?' wildcards, case-insensitive.
bool wild(const char *p, const char *s) {
    if (*p == '\0') return *s == '\0';
    if (*p == '*') {
        for (const char *t = s;; ++t) {
            if (wild(p + 1, t)) return true;
            if (*t == '\0') return false;
        }
    }
    if (*s == '\0') return false;
    if (*p == '?' || std::tolower(static_cast<unsigned char>(*p)) == std::tolower(static_cast<unsigned char>(*s)))
        return wild(p + 1, s + 1);
    return false;
}

// The child of `dir` whose name equals `name` ignoring case, or an empty path.
fs::path child_ci(const fs::path &dir, const std::string &name) {
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (lower(it->path().filename().string()) == lower(name)) return it->path();
    }
    return {};
}

}  // namespace

const std::vector<FileCheck> &game_file_checks() { return kChecks; }

ScanResult scan_game_files(const fs::path &root) {
    ScanResult out;
    for (const FileCheck &c : kChecks) {
        CheckResult r;
        r.check = &c;
        const fs::path dir = child_ci(root, c.dir);
        if (!dir.empty()) {
            std::error_code ec;
            for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec)) {
                std::error_code e2;
                if (!it->is_regular_file(e2)) continue;
                if (!wild(c.pattern, it->path().filename().string().c_str())) continue;
                r.files++;
                const auto sz = it->file_size(e2);
                if (!e2) r.bytes += sz;
            }
        }
        if (r.files >= c.min_count) {
            r.state = CheckState::Ok;
        } else if (c.required) {
            r.state = CheckState::Missing;
            out.missing_required++;
        } else {
            r.state = CheckState::OptionalMissing;
        }
        out.total_files += r.files;
        out.total_bytes += r.bytes;
        out.items.push_back(r);
    }
    out.ready = out.missing_required == 0;
    return out;
}

namespace {

// How many of the game's folders does `dir` hold?
int game_dirs_in(const fs::path &dir) {
    int n = 0;
    for (const FileCheck &c : kChecks) {
        std::error_code ec;
        const fs::path p = child_ci(dir, c.dir);
        if (!p.empty() && fs::is_directory(p, ec)) n++;
    }
    return n;
}

}  // namespace

fs::path find_game_root(const fs::path &picked) {
    std::error_code ec;
    if (!fs::is_directory(picked, ec)) return {};
    if (game_dirs_in(picked) >= 2) return picked;
    // One or two levels down, taking the candidate with the most matches.
    fs::path best;
    int best_n = 1;
    for (fs::directory_iterator a(picked, ec), end; !ec && a != end; a.increment(ec)) {
        std::error_code e2;
        if (!a->is_directory(e2)) continue;
        int n = game_dirs_in(a->path());
        if (n > best_n) { best_n = n; best = a->path(); }
        for (fs::directory_iterator b(a->path(), e2), end2; !e2 && b != end2; b.increment(e2)) {
            std::error_code e3;
            if (!b->is_directory(e3)) continue;
            n = game_dirs_in(b->path());
            if (n > best_n) { best_n = n; best = b->path(); }
        }
    }
    return best;
}

CopyOutcome copy_tree(const fs::path &src, const fs::path &dst, CopyProgress &p) {
    std::error_code ec;
    struct Item { fs::path from; fs::path to; uint64_t size; };
    std::vector<Item> items;
    uint64_t total = 0;
    p.set_current("Looking at the files...");
    for (fs::recursive_directory_iterator it(src, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (p.cancel) return CopyOutcome::Cancelled;
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const uint64_t sz = it->file_size(e2);
        items.push_back({it->path(), dst / fs::relative(it->path(), src, e2), e2 ? 0 : sz});
        total += e2 ? 0 : sz;
    }
    if (ec) {
        p.set_error("Could not read the source folder: " + ec.message());
        return CopyOutcome::Failed;
    }
    p.bytes_total = total;
    p.files_total = static_cast<int>(items.size());
    if (items.empty()) {
        p.set_error("The folder has no files in it.");
        return CopyOutcome::Failed;
    }

    std::vector<char> buf(1 << 20);
    for (const Item &it : items) {
        if (p.cancel) return CopyOutcome::Cancelled;
        p.set_current(it.from.filename().string());
        std::error_code e2;
        // Resume: same name and size means it already made it across.
        if (fs::exists(it.to, e2) && fs::file_size(it.to, e2) == it.size) {
            p.bytes_done += it.size;
            p.files_done++;
            p.files_skipped++;
            continue;
        }
        fs::create_directories(it.to.parent_path(), e2);
        if (e2) {
            p.set_error("Could not create " + it.to.parent_path().string() + ": " + e2.message());
            return CopyOutcome::Failed;
        }
        std::ifstream in(it.from, std::ios::binary);
        // Write to a temporary name first so a half-copied file is never mistaken for a whole one.
        const fs::path tmp = it.to.string() + ".part";
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!in || !out) {
            p.set_error("Could not open " + (in ? it.to.string() : it.from.string()));
            return CopyOutcome::Failed;
        }
        while (in) {
            if (p.cancel) { out.close(); fs::remove(tmp, e2); return CopyOutcome::Cancelled; }
            in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            const std::streamsize n = in.gcount();
            if (n <= 0) break;
            out.write(buf.data(), n);
            if (!out) {
                out.close();
                fs::remove(tmp, e2);
                p.set_error("Could not write " + it.to.string() + " (is the storage full?)");
                return CopyOutcome::Failed;
            }
            p.bytes_done += static_cast<uint64_t>(n);
        }
        out.close();
        fs::rename(tmp, it.to, e2);
        if (e2) {
            p.set_error("Could not finish " + it.to.string() + ": " + e2.message());
            return CopyOutcome::Failed;
        }
        p.files_done++;
    }
    return CopyOutcome::Done;
}

std::string format_count(uint64_t n) {
    std::string s = std::to_string(n), out;
    int c = 0;
    for (auto it = s.rbegin(); it != s.rend(); ++it, ++c) {
        if (c && c % 3 == 0) out.push_back(',');
        out.push_back(*it);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::string format_bytes(uint64_t n) {
    char b[32];
    if (n >= (1ull << 30)) std::snprintf(b, sizeof b, "%.1f GB", n / double(1ull << 30));
    else if (n >= (1ull << 20)) std::snprintf(b, sizeof b, "%.0f MB", n / double(1ull << 20));
    else if (n >= 1024) std::snprintf(b, sizeof b, "%.0f KB", n / 1024.0);
    else std::snprintf(b, sizeof b, "%llu B", static_cast<unsigned long long>(n));
    return b;
}

}  // namespace nl
