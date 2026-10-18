// =============================================================================
// DATA ROOT — implementation
// =============================================================================
//
// See data_root.h.

#include "core/data_root.h"

// SDL is only needed to ask UWP where the local folder is, so nothing else
// (the path-resolution tests included) has to build against it.
#if defined(_WIN32)
#include <winapifamily.h>
#endif
#if defined(WINAPI_FAMILY) && defined(WINAPI_FAMILY_APP) && WINAPI_FAMILY == WINAPI_FAMILY_APP
#define NOCTURNE_DATA_ROOT_UWP 1
#include <SDL.h>
#include <SDL_system.h>   // SDL_WinRTGetFSPathUTF8
#endif

namespace {

std::string compute_root() {
#if defined(NOCTURNE_DATA_ROOT_UWP)
    // The app's local folder: read-write, survives updates, and where files
    // copied in through Device Portal end up.
    const char *folder = SDL_WinRTGetFSPathUTF8(SDL_WINRT_PATH_LOCAL_FOLDER);
    if (folder != nullptr && folder[0] != '\0') {
        std::string root(folder);
        for (char &c : root) {
            if (c == '\\') c = '/';
        }
        if (root.back() != '/') root += '/';
        return root;
    }
#endif
    return std::string();
}

bool is_absolute(const char *path) {
    if (path[0] == '/' || path[0] == '\\') return true;
    return path[0] != '\0' && path[1] == ':';   // drive letter
}

}  // namespace

std::string nocturne_data_root() {
    static const std::string root = compute_root();
    return root;
}

std::string nocturne_data_path(const char *path) {
    if (path == nullptr) return std::string();
    const std::string &root = nocturne_data_root();
    if (root.empty() || is_absolute(path)) return std::string(path);

    if (path[0] == '.' && (path[1] == '/' || path[1] == '\\')) path += 2;
    return root + path;
}
