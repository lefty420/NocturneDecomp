#include "fs_util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace nl {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

std::vector<std::string> split_path(const std::string &rel) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : rel) {
        if (c == '\\' || c == '/') {
            if (!cur.empty() && cur != ".") out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty() && cur != ".") out.push_back(cur);
    return out;
}

std::string join_path(const std::string &a, const std::string &b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "/" + b;
}

namespace {
// Name in `dir` equal to `name` ignoring case; empty if none.
std::string match_in_dir(const std::string &dir, const std::string &name) {
    std::error_code ec;
    std::string want = lower(name);
    for (fs::directory_iterator it(dir.empty() ? "." : dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::string n = it->path().filename().string();
        if (lower(n) == want) return n;
    }
    return std::string();
}
}  // namespace

std::string find_ci(const std::string &root, const std::string &rel) {
    std::string cur = root;
    for (const std::string &part : split_path(rel)) {
        std::string m = match_in_dir(cur, part);
        if (m.empty()) return std::string();
        cur = join_path(cur, m);
    }
    return cur;
}

std::string resolve_for_write(const std::string &root, const std::string &rel) {
    std::string cur = root;
    bool exists = true;
    for (const std::string &part : split_path(rel)) {
        std::string m = exists ? match_in_dir(cur, part) : std::string();
        if (m.empty()) {
            exists = false;
            m = part;
        }
        cur = join_path(cur, m);
    }
    return cur;
}

bool make_dirs(const std::string &path) {
    std::error_code ec;
    fs::create_directories(path, ec);
    return !ec;
}

uint64_t file_size_of(const std::string &path) {
    std::error_code ec;
    auto s = fs::file_size(path, ec);
    return ec ? 0 : (uint64_t)s;
}

std::string human_size(uint64_t b) {
    char buf[32];
    if (b >= (1ull << 30)) std::snprintf(buf, sizeof buf, "%.1f GB", b / double(1ull << 30));
    else if (b >= (1ull << 20)) std::snprintf(buf, sizeof buf, "%.1f MB", b / double(1ull << 20));
    else if (b >= (1ull << 10)) std::snprintf(buf, sizeof buf, "%.0f KB", b / double(1ull << 10));
    else std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)b);
    return buf;
}

}  // namespace nl
