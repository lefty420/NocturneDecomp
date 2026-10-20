#include "importer.h"

#include "gamefiles.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace launcher {

bool copy_file_with_progress(const std::string &from, const std::string &to, const std::atomic<bool> &cancel,
                             uint64_t &bytesDone, std::string &error) {
    std::ifstream in(from, std::ios::binary);
    if (!in) { error = "Could not read " + from; return false; }
    std::error_code ec;
    fs::create_directories(fs::path(to).parent_path(), ec);
    std::ofstream out(to, std::ios::binary | std::ios::trunc);
    if (!out) { error = "Could not write " + to; return false; }
    std::vector<char> buf(1 << 20);
    while (in) {
        if (cancel.load()) return false;
        in.read(buf.data(), (std::streamsize)buf.size());
        std::streamsize n = in.gcount();
        if (n > 0) {
            out.write(buf.data(), n);
            if (!out) { error = "Out of space writing " + to; return false; }
            bytesDone += (uint64_t)n;
        }
    }
    return true;
}

DirImporter::DirImporter(std::string destRoot, std::vector<Source> sources)
    : dest_(std::move(destRoot)), sources_(std::move(sources)) {}

DirImporter::~DirImporter() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
}

std::vector<ImportMethod> DirImporter::methods() {
    std::vector<ImportMethod> m;
    for (auto &s : sources_) m.push_back(s.method);
    return m;
}

ImportStatus DirImporter::status() {
    std::lock_guard<std::mutex> lk(mu_);
    return st_;
}

void DirImporter::cancel() { cancel_ = true; }

void DirImporter::start(const std::string &methodId) {
    if (worker_.joinable()) worker_.join();
    for (auto &s : sources_) {
        if (s.method.id == methodId) {
            cancel_ = false;
            {
                std::lock_guard<std::mutex> lk(mu_);
                st_ = ImportStatus();
                st_.state = ImportStatus::Copying;
                st_.message = "Looking for the game files...";
            }
            worker_ = std::thread([this, p = s.path] { run(p); });
            return;
        }
    }
}

void DirImporter::run(std::string srcPath) {
    auto fail = [&](const std::string &msg) {
        std::lock_guard<std::mutex> lk(mu_);
        st_.state = ImportStatus::Failed;
        st_.message = msg;
    };

    std::string root = find_game_root(srcPath);
    if (root.empty()) {
        fail("No Nocturne game folders (art, models, data, sound) were found in that location.");
        return;
    }

    // First pass: what is there, so the progress bar means something.
    struct Item { fs::path from; fs::path rel; uint64_t size; };
    std::vector<Item> items;
    uint64_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        uint64_t sz = (uint64_t)it->file_size(fec);
        items.push_back({it->path(), fs::relative(it->path(), root, fec), sz});
        total += sz;
    }
    {
        std::lock_guard<std::mutex> lk(mu_);
        st_.bytesTotal = total;
        st_.filesTotal = (int)items.size();
    }

    uint64_t done = 0;
    int n = 0;
    for (auto &it : items) {
        if (cancel_.load()) {
            std::lock_guard<std::mutex> lk(mu_);
            st_.state = ImportStatus::Cancelled;
            st_.message = "Import cancelled. Files copied so far were kept.";
            return;
        }
        {
            std::lock_guard<std::mutex> lk(mu_);
            st_.current = it.rel.generic_string();
        }
        std::string err;
        uint64_t before = done;
        std::string to = (fs::path(dest_) / it.rel).string();
        if (!copy_file_with_progress(it.from.string(), to, cancel_, done, err)) {
            if (cancel_.load()) continue;   // reported at the top of the loop
            fail(err);
            return;
        }
        (void)before;
        ++n;
        std::lock_guard<std::mutex> lk(mu_);
        st_.bytesDone = done;
        st_.filesDone = n;
    }
    std::lock_guard<std::mutex> lk(mu_);
    st_.state = ImportStatus::Done;
    st_.message = "Copied " + std::to_string(n) + " files (" + human_bytes(done) + ").";
}

}  // namespace launcher
