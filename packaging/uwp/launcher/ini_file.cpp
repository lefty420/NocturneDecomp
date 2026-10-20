#include "ini_file.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "fs_util.h"

namespace nl {

namespace {
std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
}  // namespace

bool IniFile::load(const std::string &path) {
    lines_.clear();
    loaded_ = false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string data = ss.str();
    eol_ = data.find("\r\n") != std::string::npos ? "\r\n" : (data.find('\n') != std::string::npos ? "\n" : "\r\n");
    std::string cur;
    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i] == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            lines_.push_back(cur);
            cur.clear();
        } else {
            cur += data[i];
        }
    }
    if (!cur.empty()) lines_.push_back(cur);
    loaded_ = true;
    return true;
}

bool IniFile::save(const std::string &path) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (const std::string &l : lines_) out << l << eol_;
    return (bool)out;
}

int IniFile::find_key(const std::string &section, const std::string &key, int *section_end, bool *section_found) const {
    std::string want_s = lower(section), want_k = lower(key);
    bool in_section = false, found_section = false;
    int key_line = -1, end = (int)lines_.size();
    int last_content = -1;
    for (int i = 0; i < (int)lines_.size(); ++i) {
        std::string t = trim(lines_[i]);
        if (!t.empty() && t[0] == '[') {
            if (in_section) {
                end = last_content + 1;
                in_section = false;
                if (key_line >= 0) break;
            }
            size_t close = t.find(']');
            if (close != std::string::npos && lower(trim(t.substr(1, close - 1))) == want_s) {
                in_section = true;
                found_section = true;
                last_content = i;
            }
            continue;
        }
        if (in_section) {
            if (!t.empty()) last_content = i;
            if (!t.empty() && t[0] != ';') {
                size_t eq = t.find('=');
                if (eq != std::string::npos && lower(trim(t.substr(0, eq))) == want_k && key_line < 0) key_line = i;
            }
        }
    }
    if (in_section) end = last_content + 1;
    if (section_end) *section_end = found_section ? end : (int)lines_.size();
    if (section_found) *section_found = found_section;
    return key_line;
}

bool IniFile::has(const std::string &section, const std::string &key) const {
    return find_key(section, key, nullptr, nullptr) >= 0;
}

std::string IniFile::get(const std::string &section, const std::string &key, const std::string &fallback) const {
    int i = find_key(section, key, nullptr, nullptr);
    if (i < 0) return fallback;
    const std::string &l = lines_[(size_t)i];
    return trim(l.substr(l.find('=') + 1));
}

int IniFile::get_int(const std::string &section, const std::string &key, int fallback) const {
    std::string v = get(section, key, "");
    if (v.empty()) return fallback;
    char *end = nullptr;
    long n = std::strtol(v.c_str(), &end, 10);
    return end == v.c_str() ? fallback : (int)n;
}

void IniFile::set(const std::string &section, const std::string &key, const std::string &value) {
    int end = 0;
    bool section_found = false;
    int i = find_key(section, key, &end, &section_found);
    if (i >= 0) {
        std::string &l = lines_[(size_t)i];
        size_t eq = l.find('=');
        l = l.substr(0, eq + 1) + value;
        return;
    }
    std::string line = key + "=" + value;
    if (section_found) {
        lines_.insert(lines_.begin() + end, line);
    } else {
        if (!lines_.empty() && !trim(lines_.back()).empty()) lines_.push_back("");
        lines_.push_back("[" + section + "]");
        lines_.push_back(line);
    }
}

void IniFile::set_int(const std::string &section, const std::string &key, int value) {
    set(section, key, std::to_string(value));
}

}  // namespace nl
