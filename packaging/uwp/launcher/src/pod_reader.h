// Reads the directory of a POD2 archive (the game's .pod files) and single entries from it, without loading the
// whole archive. Layout: see scripts/Python/extract_pod.py, which is taken from the game's own loader.
//
//   0x00 "POD2" | 0x04 checksum | 0x08 description[80] | 0x58 file_count | 0x5c audit_count
//   0x60 entries[file_count], 20 bytes each: name_offset, size, offset (absolute), timestamp, checksum
//   then the name table (NUL-terminated names; name_offset is relative to it).
//
// Only POD2 is understood; other kinds are reported as unreadable and skipped. Header-only, no platform calls.
#pragma once

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace nl {

struct PodEntry {
    std::string name;       // as stored (Windows path)
    std::uint32_t size = 0;
    std::uint32_t offset = 0;
};

inline std::uint32_t pod_u32(const unsigned char *p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// Fills `out` with the archive's entries. False when the file is not a readable POD2.
inline bool read_pod_directory(const std::filesystem::path &path, std::vector<PodEntry> &out) {
    out.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    unsigned char head[0x60];
    if (!in.read(reinterpret_cast<char *>(head), sizeof head)) return false;
    if (std::memcmp(head, "POD2", 4) != 0) return false;
    const std::uint32_t count = pod_u32(head + 0x58);
    if (count == 0 || count > 400000) return false;
    std::vector<unsigned char> dir(static_cast<size_t>(count) * 20);
    if (!in.read(reinterpret_cast<char *>(dir.data()), static_cast<std::streamsize>(dir.size()))) return false;
    std::uint32_t first_data = 0xFFFFFFFFu;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t offset = pod_u32(&dir[i * 20 + 8]);
        if (offset < first_data) first_data = offset;
    }
    const std::uint64_t table_start = 0x60 + static_cast<std::uint64_t>(count) * 20;
    if (first_data <= table_start || first_data - table_start > (16u << 20)) return false;
    std::vector<char> names(first_data - table_start);
    if (!in.read(names.data(), static_cast<std::streamsize>(names.size()))) return false;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t name_offset = pod_u32(&dir[i * 20]);
        if (name_offset >= names.size()) continue;
        const char *s = names.data() + name_offset;
        const size_t room = names.size() - name_offset;
        PodEntry e;
        const void *nul = std::memchr(s, 0, room);
        e.name.assign(s, nul ? static_cast<size_t>(static_cast<const char *>(nul) - s) : room);
        e.size = pod_u32(&dir[i * 20 + 4]);
        e.offset = pod_u32(&dir[i * 20 + 8]);
        out.push_back(std::move(e));
    }
    return !out.empty();
}

inline bool read_pod_entry(const std::filesystem::path &path, const PodEntry &e, std::vector<unsigned char> &bytes,
                           std::uint32_t max_size = 48u << 20) {
    bytes.clear();
    if (e.size == 0 || e.size > max_size) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(static_cast<std::streamoff>(e.offset));
    bytes.resize(e.size);
    if (!in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        bytes.clear();
        return false;
    }
    return true;
}

}  // namespace nl
