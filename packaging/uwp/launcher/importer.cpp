#include "importer.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>

#include "fs_util.h"

namespace nl {

bool import_skip(const std::string &name) {
    static const char *deny[] = {".exe", ".dll", ".ocx", ".scr", ".cab", ".inf", ".url", ".lnk", ".hlp", ".chm", ".tmp", ".part", ".db"};
    std::string l = lower(name);
    for (const char *d : deny) {
        size_t n = std::char_traits<char>::length(d);
        if (l.size() >= n && l.compare(l.size() - n, n, d) == 0) return true;
    }
    return false;
}

std::string import_dest_for_loose(const std::string &name) {
    std::string l = lower(name);
    auto ends = [&](const char *e) {
        size_t n = std::char_traits<char>::length(e);
        return l.size() >= n && l.compare(l.size() - n, n, e) == 0;
    };
    if (ends(".avi")) return "video/" + name;
    if (ends(".ini")) return "system/" + name;
    return name;
}

namespace {

bool holds_pods(const std::string &dir) { return !find_ci(dir, "ACT1.POD").empty() || !find_ci(dir, "ENEMY.POD").empty(); }

std::string find_game_root(const std::string &dir, int depth) {
    if (holds_pods(dir)) return dir;
    if (depth <= 0) return std::string();
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        std::string r = find_game_root(it->path().string(), depth - 1);
        if (!r.empty()) return r;
    }
    return std::string();
}

}  // namespace

ImportPlan plan_folder_import(const std::string &src_root) {
    ImportPlan plan;
    std::error_code ec;
    if (src_root.empty() || !fs::is_directory(src_root, ec)) {
        plan.error = "That folder can't be read.";
        return plan;
    }
    plan.game_root = find_game_root(src_root, 3);
    if (plan.game_root.empty()) {
        plan.error = "No ACT1.POD or ENEMY.POD found in that folder (looked three levels down). Pick the folder that holds the game's .POD files.";
        return plan;
    }
    fs::path base(plan.game_root);
    for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        std::string name = it->path().filename().string();
        if (import_skip(name)) continue;
        PlanItem item;
        item.src = it->path().string();
        item.rel = it->path().lexically_relative(base).generic_string();
        item.size = (uint64_t)it->file_size(e2);
        plan.total_bytes += item.size;
        plan.items.push_back(std::move(item));
    }
    if (plan.items.empty()) plan.error = "The folder has no game files to copy.";
    return plan;
}

namespace {

class FolderImportService : public ImportService {
public:
    FolderImportService(std::string root, std::string source) : root_(std::move(root)), source_(std::move(source)) {}
    ~FolderImportService() override {
        cancel_flag_ = true;
        if (worker_.joinable()) worker_.join();
    }

    std::vector<ImportMethod> methods() override {
        ImportMethod m;
        m.id = "folder";
        m.label = "Copy from a folder";
        m.description = "Copy a game folder that is already on this device";
        std::error_code ec;
        m.available = !source_.empty() && fs::is_directory(source_, ec);
        if (!m.available) m.unavailable_reason = "No source folder was given.";
        return {m};
    }

    void start(const std::string &) override {
        std::lock_guard<std::mutex> lk(mu_);
        if (p_.state == ImportState::Working) return;
        if (worker_.joinable()) worker_.join();
        p_ = ImportProgress();
        p_.state = ImportState::Working;
        cancel_flag_ = false;
        worker_ = std::thread([this] { run(); });
    }

    ImportProgress progress() override {
        std::lock_guard<std::mutex> lk(mu_);
        return p_;
    }
    void cancel() override { cancel_flag_ = true; }
    void reset() override {
        std::lock_guard<std::mutex> lk(mu_);
        if (p_.state != ImportState::Working) p_ = ImportProgress();
    }

private:
    void finish(ImportState s, const std::string &msg) {
        std::lock_guard<std::mutex> lk(mu_);
        p_.state = s;
        p_.message = msg;
        p_.current.clear();
    }

    void run() {
        ImportPlan plan = plan_folder_import(source_);
        if (!plan.error.empty()) return finish(ImportState::Failed, plan.error);
        {
            std::lock_guard<std::mutex> lk(mu_);
            p_.files_total = (int)plan.items.size();
            p_.bytes_total = plan.total_bytes;
        }
        make_dirs(root_);
        std::vector<char> buf(1 << 20);
        int copied = 0, skipped = 0;
        for (const PlanItem &item : plan.items) {
            if (cancel_flag_) return finish(ImportState::Cancelled, "Cancelled. Files already copied were kept.");
            {
                std::lock_guard<std::mutex> lk(mu_);
                p_.current = item.rel;
            }
            std::string dest = resolve_for_write(root_, item.rel);
            if (file_size_of(dest) == item.size && fs::exists(dest)) {   // already there from an earlier run
                ++skipped;
                std::lock_guard<std::mutex> lk(mu_);
                ++p_.files_done;
                p_.bytes_done += item.size;
                continue;
            }
            std::error_code ec;
            fs::create_directories(fs::path(dest).parent_path(), ec);
            std::string part = dest + ".part";
            FILE *in = std::fopen(item.src.c_str(), "rb");
            FILE *out = in ? std::fopen(part.c_str(), "wb") : nullptr;
            if (!in || !out) {
                if (in) std::fclose(in);
                return finish(ImportState::Failed, "Could not " + std::string(in ? "write " : "read ") + item.rel + ".");
            }
            bool ok = true;
            size_t n;
            while (ok && (n = std::fread(buf.data(), 1, buf.size(), in)) > 0) {
                if (std::fwrite(buf.data(), 1, n, out) != n) ok = false;
                std::lock_guard<std::mutex> lk(mu_);
                p_.bytes_done += n;
                if (cancel_flag_) ok = false;
            }
            std::fclose(in);
            std::fclose(out);
            if (!ok) {
                fs::remove(part, ec);
                if (cancel_flag_) return finish(ImportState::Cancelled, "Cancelled. Files already copied were kept.");
                return finish(ImportState::Failed, "Copying " + item.rel + " failed - is there enough free space?");
            }
            fs::remove(dest, ec);
            fs::rename(part, dest, ec);
            if (ec) return finish(ImportState::Failed, "Could not finish writing " + item.rel + ".");
            ++copied;
            std::lock_guard<std::mutex> lk(mu_);
            ++p_.files_done;
        }
        char msg[160];
        std::snprintf(msg, sizeof msg, "Copied %d file%s%s.", copied, copied == 1 ? "" : "s",
                      skipped ? (" (" + std::to_string(skipped) + " already present)").c_str() : "");
        finish(ImportState::Done, msg);
    }

    std::string root_, source_;
    std::mutex mu_;
    ImportProgress p_;
    std::atomic<bool> cancel_flag_{false};
    std::thread worker_;
};

}  // namespace

std::unique_ptr<ImportService> make_folder_import_service(const std::string &data_root, const std::string &source_dir) {
    return std::make_unique<FolderImportService>(data_root, source_dir);
}

}  // namespace nl
