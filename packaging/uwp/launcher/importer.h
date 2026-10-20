// =============================================================================
// IMPORTER — getting the player's own game files into the app's folder
// =============================================================================
//
// The launcher talks to this interface only. DirImporter (below) copies from a
// folder the OS lets the app read as an ordinary path. The UWP build adds an
// importer that goes through WinRT storage (pickers, removable drives), which
// is where Xbox's restrictions apply; see docs/xbox-uwp.md.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace launcher {

struct ImportMethod {
    std::string id;       // stable key
    std::string title;    // "Choose a folder"
    std::string detail;   // one line on where it looks
};

struct ImportStatus {
    enum State { Idle, Picking, Copying, Done, Failed, Cancelled } state = Idle;
    std::string message;        // result or error, shown to the player
    std::string current;        // file being copied
    uint64_t bytesDone = 0, bytesTotal = 0;
    int filesDone = 0, filesTotal = 0;
};

class Importer {
public:
    virtual ~Importer() {}
    virtual std::vector<ImportMethod> methods() = 0;
    // Returns at once; the work runs elsewhere. Poll status().
    virtual void start(const std::string &methodId) = 0;
    virtual ImportStatus status() = 0;
    virtual void cancel() = 0;
};

// Copies a game install from a folder readable as a normal path into `destRoot`.
// `sources` lists (id, title, detail, path) the player can pick from.
class DirImporter : public Importer {
public:
    struct Source { ImportMethod method; std::string path; };
    DirImporter(std::string destRoot, std::vector<Source> sources);
    ~DirImporter() override;
    std::vector<ImportMethod> methods() override;
    void start(const std::string &methodId) override;
    ImportStatus status() override;
    void cancel() override;

private:
    void run(std::string srcPath);
    std::string dest_;
    std::vector<Source> sources_;
    std::mutex mu_;
    ImportStatus st_;
    std::atomic<bool> cancel_{false};
    std::thread worker_;
};

// Shared by importers: the folder under the destination the files end up in is the
// destination root itself, so the game finds art/, models/ ... relative to it.
bool copy_file_with_progress(const std::string &from, const std::string &to, const std::atomic<bool> &cancel,
                             uint64_t &bytesDone, std::string &error);

}  // namespace launcher
