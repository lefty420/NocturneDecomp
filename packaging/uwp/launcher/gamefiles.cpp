#include "gamefiles.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace launcher {

static std::string lower(std::string s) {
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

const std::vector<Component> &components() {
    static const std::vector<Component> list = {
        {"art",    nullptr,        "Art",        "Textures and 2D images",         true},
        {"models", nullptr,        "Models",     "3D models and animation",        true},
        {"data",   nullptr,        "Game data",  "Levels, scripts and tables",     true},
        {"sound",  nullptr,        "Sound",      "Sound effects and voices",       true},
        {"video",  "opening.avi",  "Intro movie","Optional, plays at start-up",    false},
    };
    return list;
}

std::string find_child(const std::string &dir, const std::string &name) {
    std::error_code ec;
    const std::string want = lower(name);
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (lower(it->path().filename().string()) == want) return it->path().string();
    }
    return std::string();
}

static void measure(const std::string &dir, int &files, uint64_t &bytes) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (it->is_regular_file(fec)) {
            ++files;
            auto sz = it->file_size(fec);
            if (!fec) bytes += sz;
        }
    }
}

Scan scan_game_files(const std::string &root) {
    Scan scan;
    scan.root = root;
    for (const Component &c : components()) {
        ComponentState st;
        st.comp = &c;
        std::string d = find_child(root, c.dir);
        std::error_code ec;
        if (!d.empty() && fs::is_directory(d, ec)) {
            if (c.file) {
                std::string f = find_child(d, c.file);
                if (!f.empty()) {
                    st.present = true;
                    st.files = 1;
                    st.bytes = (uint64_t)fs::file_size(f, ec);
                }
            } else {
                measure(d, st.files, st.bytes);
                st.present = st.files > 0;
            }
        }
        if (c.required && !st.present) ++scan.missingRequired;
        scan.totalFiles += st.files;
        scan.totalBytes += st.bytes;
        scan.items.push_back(st);
    }
    scan.ready = scan.missingRequired == 0;
    return scan;
}

static bool looks_like_root(const std::string &dir) {
    int hits = 0;
    for (const Component &c : components()) {
        if (!c.required) continue;
        if (!find_child(dir, c.dir).empty()) ++hits;
    }
    return hits >= 2;   // two of the required folders is enough to be sure
}

std::string find_game_root(const std::string &candidate) {
    if (candidate.empty()) return std::string();
    if (looks_like_root(candidate)) return candidate;
    std::error_code ec;
    for (fs::directory_iterator a(candidate, fs::directory_options::skip_permission_denied, ec), end; !ec && a != end; a.increment(ec)) {
        std::error_code dec;
        if (!a->is_directory(dec)) continue;
        if (looks_like_root(a->path().string())) return a->path().string();
    }
    ec.clear();
    for (fs::directory_iterator a(candidate, fs::directory_options::skip_permission_denied, ec), end; !ec && a != end; a.increment(ec)) {
        std::error_code dec;
        if (!a->is_directory(dec)) continue;
        std::error_code ec2;
        for (fs::directory_iterator b(a->path(), fs::directory_options::skip_permission_denied, ec2), end2; !ec2 && b != end2; b.increment(ec2)) {
            std::error_code dec2;
            if (!b->is_directory(dec2)) continue;
            if (looks_like_root(b->path().string())) return b->path().string();
        }
    }
    return std::string();
}

std::string human_bytes(uint64_t b) {
    char buf[32];
    if (b >= (1ull << 30)) std::snprintf(buf, sizeof buf, "%.2f GB", (double)b / (1ull << 30));
    else if (b >= (1ull << 20)) std::snprintf(buf, sizeof buf, "%.1f MB", (double)b / (1ull << 20));
    else if (b >= (1ull << 10)) std::snprintf(buf, sizeof buf, "%.0f KB", (double)b / (1ull << 10));
    else std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)b);
    return buf;
}

}  // namespace launcher
