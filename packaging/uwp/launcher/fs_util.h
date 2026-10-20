// Path helpers shared by the launcher. The game uses Windows-style relative paths
// ("system\\nocturne.ini", "ACT1.POD") with arbitrary case; everything here
// resolves them case-insensitively so the same code works on a case-sensitive
// test machine and on Windows.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nl {

namespace fs = std::filesystem;

std::string lower(std::string s);
std::vector<std::string> split_path(const std::string &rel);   // on '\' and '/'
std::string join_path(const std::string &a, const std::string &b);   // always '/'

// The existing path for `rel` under `root`, matching each component ignoring case.
// Empty string when something is missing.
std::string find_ci(const std::string &root, const std::string &rel);

// root + rel with each existing component's real spelling; missing components are
// appended as given. Used as a write target.
std::string resolve_for_write(const std::string &root, const std::string &rel);

bool make_dirs(const std::string &path);
uint64_t file_size_of(const std::string &path);   // 0 if missing
std::string human_size(uint64_t bytes);

}  // namespace nl
