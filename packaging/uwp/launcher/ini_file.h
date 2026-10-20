// Minimal INI editor. Reads and rewrites the game's own system\nocturne.ini while
// leaving every line it does not touch exactly as it was (comments, order, line
// endings), so the game and the launcher can share the file.
#pragma once
#include <string>
#include <vector>

namespace nl {

class IniFile {
public:
    bool load(const std::string &path);   // false when the file is missing or unreadable
    bool save(const std::string &path) const;
    bool loaded() const { return loaded_; }

    bool has(const std::string &section, const std::string &key) const;
    int get_int(const std::string &section, const std::string &key, int fallback) const;
    std::string get(const std::string &section, const std::string &key, const std::string &fallback) const;
    void set(const std::string &section, const std::string &key, const std::string &value);
    void set_int(const std::string &section, const std::string &key, int value);

private:
    // Index of the line holding the key, or -1. `section_end` receives the index just
    // past the section's last line (or lines_.size() when the section is missing).
    int find_key(const std::string &section, const std::string &key, int *section_end, bool *section_found) const;

    std::vector<std::string> lines_;
    std::string eol_ = "\r\n";
    bool loaded_ = false;
};

}  // namespace nl
