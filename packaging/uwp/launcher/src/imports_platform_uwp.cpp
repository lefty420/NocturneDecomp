// UWP import providers: the system folder picker and removable (USB) storage.
//
// C++/CX, so this file is compiled by MSVC with /ZW. NOTHING HERE HAS RUN ON A CONSOLE.
// Microsoft documents folder pickers as unsupported on Xbox (community reports say they
// work in practice) and the USB route needs capabilities declared in the manifest (see
// docs/xbox-uwp.md). Every failure is turned into a plain message for the Game files
// screen instead of being hidden, so the first real test also tells us which route works.
//
// The copy goes through StorageFolder/StorageFile, not file paths: a folder the picker
// hands back is only reachable through the broker, not through std::filesystem.

#include "imports.h"

#include <collection.h>
#include <ppltasks.h>
#include <roapi.h>
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <future>

using namespace Platform;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Storage::Pickers;
using namespace Windows::UI::Core;
using namespace concurrency;

namespace nl {

namespace {

std::string narrow(Platform::String ^s) {
    if (s == nullptr || s->Length() == 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s->Data(), static_cast<int>(s->Length()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s->Data(), static_cast<int>(s->Length()), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring lower_w(std::wstring s) {
    for (wchar_t &c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::wstring widen(const char *s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring out(static_cast<size_t>(n > 0 ? n - 1 : 0), L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, &out[0], n);
    return out;
}

template <class T>
T await(IAsyncOperation<T> ^op) {
    return create_task(op).get();
}

std::string describe(Platform::Exception ^e) {
    char hr[32];
    std::snprintf(hr, sizeof hr, " (0x%08X)", static_cast<unsigned>(e->HResult));
    return narrow(e->Message) + hr;
}

// How many level archives (ACT1.POD ... ACT5.POD) does `f` hold directly? Three or more is an install.
int game_dirs_in(StorageFolder ^f) {
    int n = 0;
    for (StorageFile ^file : await(f->GetFilesAsync())) {
        const std::wstring name = lower_w(file->Name->Data());
        if (name.size() == 8 && name.compare(0, 3, L"act") == 0 && name.compare(4, 4, L".pod") == 0) ++n;
    }
    return n;
}

// The picked folder itself, or the best candidate one or two levels down.
StorageFolder ^find_root(StorageFolder ^picked) {
    if (game_dirs_in(picked) >= 3) return picked;
    StorageFolder ^best = nullptr;
    int best_n = 2;
    for (StorageFolder ^a : await(picked->GetFoldersAsync())) {
        int n = game_dirs_in(a);
        if (n > best_n) { best_n = n; best = a; }
        for (StorageFolder ^b : await(a->GetFoldersAsync())) {
            n = game_dirs_in(b);
            if (n > best_n) { best_n = n; best = b; }
        }
    }
    return best;
}

struct Entry {
    StorageFile ^file;
    std::vector<std::wstring> dirs;   // relative folder path
    uint64_t size;
};

void enumerate(StorageFolder ^f, std::vector<std::wstring> &rel, std::vector<Entry> &out, CopyProgress &p) {
    if (p.cancel) return;
    for (StorageFile ^file : await(f->GetFilesAsync())) {
        if (p.cancel) return;
        if (!worth_copying(narrow(file->Name))) continue;
        const uint64_t size = await(file->GetBasicPropertiesAsync())->Size;
        out.push_back({file, rel, size});
    }
    for (StorageFolder ^sub : await(f->GetFoldersAsync())) {
        rel.push_back(sub->Name->Data());
        enumerate(sub, rel, out, p);
        rel.pop_back();
    }
}

CopyOutcome copy_storage_folder(StorageFolder ^src, CopyProgress &p) {
    p.set_current("Looking at the files...");
    std::vector<Entry> items;
    std::vector<std::wstring> rel;
    enumerate(src, rel, items, p);
    if (p.cancel) return CopyOutcome::Cancelled;
    uint64_t total = 0;
    for (const Entry &e : items) total += e.size;
    p.bytes_total = total;
    p.files_total = static_cast<int>(items.size());
    if (items.empty()) {
        p.set_error("The folder has no files in it.");
        return CopyOutcome::Failed;
    }

    StorageFolder ^root = ApplicationData::Current->LocalFolder;
    for (const Entry &e : items) {
        if (p.cancel) return CopyOutcome::Cancelled;
        p.set_current(narrow(e.file->Name));
        StorageFolder ^dest = root;
        for (const std::wstring &d : e.dirs)
            dest = await(dest->CreateFolderAsync(ref new Platform::String(d.c_str()), CreationCollisionOption::OpenIfExists));
        // Resume: same name and size means it already made it across.
        IStorageItem ^existing = await(dest->TryGetItemAsync(e.file->Name));
        StorageFile ^existing_file = dynamic_cast<StorageFile ^>(existing);
        if (existing_file != nullptr && await(existing_file->GetBasicPropertiesAsync())->Size == e.size) {
            p.bytes_done += e.size;
            p.files_done++;
            p.files_skipped++;
            continue;
        }
        await(e.file->CopyAsync(dest, e.file->Name, NameCollisionOption::ReplaceExisting));
        p.bytes_done += e.size;
        p.files_done++;
    }
    return CopyOutcome::Done;
}

// Asks the UI thread to show the folder picker and waits for the answer.
StorageFolder ^pick_folder(std::string &error) {
    std::promise<StorageFolder ^> done;
    auto fut = done.get_future();
    auto dispatcher = CoreApplication::MainView->Dispatcher;
    dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([&done]() {
        try {
            FolderPicker ^fp = ref new FolderPicker();
            fp->SuggestedStartLocation = PickerLocationId::ComputerFolder;
            fp->FileTypeFilter->Append("*");
            create_task(fp->PickSingleFolderAsync()).then([&done](task<StorageFolder ^> t) {
                try { done.set_value(t.get()); }
                catch (Platform::Exception ^) { done.set_exception(std::current_exception()); }
            });
        } catch (Platform::Exception ^) {
            done.set_exception(std::current_exception());
        }
    }));
    try {
        return fut.get();
    } catch (Platform::Exception ^e) {
        error = describe(e);
    } catch (...) {
        error = "unknown error";
    }
    return nullptr;
}

}  // namespace

void add_platform_providers(std::vector<ImportProvider> &out) {
    {
        ImportProvider p;
        p.title = "Choose a folder myself";
        p.subtitle = "Point to any folder holding your game files (the console may restrict this)";
        p.unavailable_reason = [] { return std::string(); };
        p.run = [](CopyProgress &progress, const std::filesystem::path &) -> CopyOutcome {
            RoInitialize(RO_INIT_MULTITHREADED);
            try {
                std::string err;
                progress.set_current("Waiting for you to choose a folder...");
                StorageFolder ^picked = pick_folder(err);
                if (picked == nullptr) {
                    if (!err.empty()) {
                        progress.set_error("The console would not open the folder picker: " + err +
                                           ". Use Device Portal to copy the files in instead.");
                        return CopyOutcome::Failed;
                    }
                    return CopyOutcome::Cancelled;
                }
                StorageFolder ^root = find_root(picked);
                if (root == nullptr) {
                    progress.set_error("That folder does not look like a Nocturne install (expected ACT1.POD, ACT2.POD and the "
                                       "other .POD files). Pick the folder that contains them.");
                    return CopyOutcome::Failed;
                }
                return copy_storage_folder(root, progress);
            } catch (Platform::Exception ^e) {
                progress.set_error("Copy failed: " + describe(e));
                return CopyOutcome::Failed;
            }
        };
        out.push_back(std::move(p));
    }
    {
        // Experimental: read the game straight from the drive, so nothing is copied and nothing is lost
        // when the app is uninstalled. Needs the console to let the app open the drive by path.
        ImportProvider p;
        p.title = "Use the USB drive directly (experimental)";
        p.subtitle = "Keeps the files on the drive: nothing is copied, nothing is lost if you reinstall";
        p.in_place = true;
        p.unavailable_reason = [] { return std::string(); };
        p.run = [](CopyProgress &progress, const std::filesystem::path &data_root) -> CopyOutcome {
            RoInitialize(RO_INIT_MULTITHREADED);
            try {
                progress.set_current("Looking for drives...");
                auto drives = await(KnownFolders::RemovableDevices->GetFoldersAsync());
                for (StorageFolder ^drive : drives) {
                    StorageFolder ^root = find_root(drive);
                    if (root == nullptr) continue;
                    const std::string path = narrow(root->Path);
                    std::error_code ec;
                    if (path.empty() || !std::filesystem::is_directory(std::filesystem::path(path), ec)) {
                        progress.set_error("The console would not let the app open the drive by path, so the files can't be "
                                           "used in place. Use \"Import from a USB drive\" instead.");
                        return CopyOutcome::Failed;
                    }
                    std::ofstream out(data_root / "data_dir.txt", std::ios::trunc);
                    out << path << "\n";
                    if (!out) {
                        progress.set_error("Could not remember the drive's location.");
                        return CopyOutcome::Failed;
                    }
                    progress.set_current(path);
                    return CopyOutcome::Done;
                }
                progress.set_error("No drive with a Nocturne folder (the one holding ACT1.POD and the other .POD files) was found.");
                return CopyOutcome::Failed;
            } catch (Platform::Exception ^e) {
                progress.set_error("Could not read the drive: " + describe(e));
                return CopyOutcome::Failed;
            }
        };
        out.push_back(std::move(p));
    }
    {
        ImportProvider p;
        p.title = "Import from a USB drive";
        p.subtitle = "Finds a Nocturne folder on an attached drive; also runs by itself at start-up";
        p.unavailable_reason = [] { return std::string(); };
        p.probe = []() -> std::string {
            RoInitialize(RO_INIT_MULTITHREADED);
            try {
                for (StorageFolder ^drive : await(KnownFolders::RemovableDevices->GetFoldersAsync())) {
                    if (find_root(drive) != nullptr) {
                        std::wstring w = drive->Name->Data();
                        return "drive " + std::string(w.begin(), w.end());
                    }
                }
            } catch (Platform::Exception ^) {
            }
            return std::string();
        };
        p.run = [](CopyProgress &progress, const std::filesystem::path &) -> CopyOutcome {
            RoInitialize(RO_INIT_MULTITHREADED);
            try {
                progress.set_current("Looking for drives...");
                auto drives = await(KnownFolders::RemovableDevices->GetFoldersAsync());
                if (drives->Size == 0) {
                    progress.set_error("No USB drive was found. Plug one in with your Nocturne folder on it, or the app "
                                       "may not be allowed to see removable storage on this console.");
                    return CopyOutcome::Failed;
                }
                for (StorageFolder ^drive : drives) {
                    StorageFolder ^root = find_root(drive);
                    if (root != nullptr) return copy_storage_folder(root, progress);
                }
                progress.set_error("A drive is attached, but no Nocturne folder (the one holding ACT1.POD and the other .POD files) was found on it. "
                                   "Only file types the app has declared can be seen on removable storage.");
                return CopyOutcome::Failed;
            } catch (Platform::Exception ^e) {
                progress.set_error("Could not read the drive: " + describe(e));
                return CopyOutcome::Failed;
            }
        };
        out.push_back(std::move(p));
    }
}

}  // namespace nl
