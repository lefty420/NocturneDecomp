#pragma once
// Shipped default configuration. The package carries preset files (defaults\nocturne.ini and
// defaults\launcher.ini); the first time each is applied - and again whenever its "; defaults-version: N"
// line goes up - its values are merged into the player's own files, so a new install starts with the
// graphics, controls and options the port was set up with, and the player does not have to redo them in
// the game's menus. A preset only sets the keys it lists. Everything else in the player's file stays,
// and after the preset has been applied once, the player's later changes are left alone.
//
// Header-only and free of platform calls so the Linux test harness can exercise the merge.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace nl {

namespace detail {

inline std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

inline std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline std::vector<std::string> split_lines(const std::string &text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

inline bool section_header(const std::string &line, std::string *name) {
    const std::string t = trim(line);
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
        *name = lower(trim(t.substr(1, t.size() - 2)));
        return true;
    }
    return false;
}

inline bool key_value(const std::string &line, std::string *key, std::string *value) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == ';' || t[0] == '#' || t[0] == '[') return false;
    const size_t eq = t.find('=');
    if (eq == std::string::npos) return false;
    *key = trim(t.substr(0, eq));
    *value = trim(t.substr(eq + 1));
    return !key->empty();
}

}  // namespace detail

// The "; defaults-version: N" number at the top of a preset, or 0 when it has none.
inline int preset_version(const std::string &preset) {
    for (const std::string &l : detail::split_lines(preset)) {
        const std::string t = detail::lower(detail::trim(l));
        const std::string tag = "defaults-version:";
        const size_t p = t.find(tag);
        if (!t.empty() && (t[0] == ';' || t[0] == '#') && p != std::string::npos) return std::atoi(t.c_str() + p + tag.size());
    }
    return 0;
}

// Sets every key the preset lists inside `existing`, keeping everything else (other keys, comments, order,
// the file's line endings). A missing section or key is added.
inline std::string merge_ini(const std::string &existing, const std::string &preset) {
    const bool crlf = existing.find("\r\n") != std::string::npos || existing.empty();
    std::vector<std::string> lines = detail::split_lines(existing);

    std::string section, section_text;
    for (const std::string &pl : detail::split_lines(preset)) {
        std::string name, key, value;
        if (detail::section_header(pl, &name)) { section = name; section_text = detail::trim(pl); continue; }
        if (!detail::key_value(pl, &key, &value) || section.empty()) continue;

        // Locate the section in the player's file.
        size_t begin = lines.size(), end = lines.size();
        bool found = false;
        for (size_t i = 0; i < lines.size(); ++i) {
            std::string n;
            if (detail::section_header(lines[i], &n)) {
                if (found) { end = i; break; }
                if (n == section) { found = true; begin = i + 1; end = lines.size(); }
            }
        }
        if (!found) {
            if (!lines.empty() && !detail::trim(lines.back()).empty()) lines.push_back("");
            lines.push_back(section_text);
            lines.push_back(key + "=" + value);
            // Later keys of this section find the header just added.
            continue;
        }
        // Replace the key if present, else add after the section's last non-blank line.
        bool done = false;
        size_t last = begin;
        for (size_t i = begin; i < end; ++i) {
            std::string k, v;
            if (!detail::trim(lines[i]).empty()) last = i + 1;
            if (detail::key_value(lines[i], &k, &v) && detail::lower(k) == detail::lower(key)) {
                lines[i] = k + "=" + value;
                done = true;
                break;
            }
        }
        if (!done) lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(last), key + "=" + value);
    }

    std::string out;
    for (const std::string &l : lines) out += l + (crlf ? "\r\n" : "\n");
    return out;
}

inline bool read_text(const std::filesystem::path &p, std::string *out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    *out = ss.str();
    return true;
}

inline bool write_text_atomic(const std::filesystem::path &p, const std::string &text) {
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    const std::filesystem::path tmp = p.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << text;
        if (!out) return false;
    }
    std::filesystem::rename(tmp, p, ec);
    return !ec;
}

// Finds `name` inside `dir` ignoring case ("System" / "SYSTEM"); empty when absent.
inline std::filesystem::path find_ci(const std::filesystem::path &dir, const std::string &name) {
    std::error_code ec;
    for (const auto &e : std::filesystem::directory_iterator(dir, ec)) {
        if (detail::lower(e.path().filename().string()) == detail::lower(name)) return e.path();
    }
    return {};
}

enum class ApplyResult { NoPreset, TargetMissing, AlreadyApplied, Applied, Failed };

// Merges `preset_file` into `target` if the marker file says this version has not been applied yet.
// With create_target false a missing target is left alone (the game's own file does not exist until the
// game files are in place). The marker records the version after a successful merge.
inline ApplyResult apply_preset(const std::filesystem::path &preset_file, const std::filesystem::path &target,
                                bool create_target, const std::filesystem::path &marker) {
    std::string preset;
    if (!read_text(preset_file, &preset)) return ApplyResult::NoPreset;
    const int version = preset_version(preset);
    if (version <= 0) return ApplyResult::NoPreset;
    std::string done;
    if (read_text(marker, &done) && std::atoi(done.c_str()) >= version) return ApplyResult::AlreadyApplied;
    std::string existing;
    const bool have = !target.empty() && read_text(target, &existing);
    if (!have && !create_target) return ApplyResult::TargetMissing;
    if (target.empty()) return ApplyResult::TargetMissing;
    if (!write_text_atomic(target, merge_ini(existing, preset))) return ApplyResult::Failed;
    write_text_atomic(marker, std::to_string(version) + "\n");
    return ApplyResult::Applied;
}

}  // namespace nl
