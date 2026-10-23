// =============================================================================
// PATH ROOTS — implementation
// =============================================================================
//
// See path_roots.h.

#include "core/path_roots.h"
#include "watcom/path.h"

#if defined(NOCTURNE_USER_DIR_FROM_SDL)
#include <SDL.h>
#endif

#if defined(NOCTURNE_UWP_LAUNCHER)
#include "core/data_root.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace {

// The value of --<name> <dir> or --<name>=<dir>, or null.
const char *option(int argc, char **argv, const char *name) {
    const size_t length = strlen(name);
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg == nullptr || strncmp(arg, name, length) != 0) continue;
        if (arg[length] == '=') return arg + length + 1;
        if (arg[length] == '\0' && i + 1 < argc) return argv[i + 1];
    }
    return nullptr;
}

std::string pick(const char *from_command_line, const char *environment_name,
                 const char *compiled_in) {
    const char *chosen = from_command_line;
    if (chosen == nullptr) chosen = getenv(environment_name);
    if (chosen == nullptr) chosen = compiled_in;
    if (chosen == nullptr || chosen[0] == '\0') return std::string();

    std::error_code ignored;
    std::filesystem::path path = std::filesystem::absolute(chosen, ignored);
    if (ignored) path = chosen;
    std::string result = path.lexically_normal().generic_string();
    while (result.size() > 1 && result.back() == '/') result.pop_back();
    return result;
}

}  // namespace

void nocturne_init_path_roots(int argc, char **argv) {
#if defined(NOCTURNE_UWP_LAUNCHER)
    // UWP: the install folder is read-only; the game files the launcher copies in, the saves and the
    // logs all live in the app's local folder, so it is both the data and the user directory.
    std::string uwp_root = nocturne_data_root();
    while (uwp_root.size() > 1 && uwp_root.back() == '/') uwp_root.pop_back();
    // The player may have chosen to use the game files on a USB drive in place (launcher: data_dir.txt).
    std::string uwp_data = uwp_root;
    {
        FILE *f = fopen((uwp_root + "/data_dir.txt").c_str(), "rb");
        if (f != nullptr) {
            char line[1024] = {0};
            if (fgets(line, sizeof line, f) != nullptr) {
                std::string dir = line;
                while (!dir.empty() && (dir.back() == '\n' || dir.back() == '\r' || dir.back() == ' ')) dir.pop_back();
                std::error_code ec;
                if (!dir.empty() && std::filesystem::is_directory(std::filesystem::path(dir), ec)) uwp_data = dir;
            }
            fclose(f);
        }
    }
    const char *default_data = uwp_data.empty() ? nullptr : uwp_data.c_str();
#elif defined(NOCTURNE_DEFAULT_DATA_DIR)
    const char *default_data = NOCTURNE_DEFAULT_DATA_DIR;
#else
    const char *default_data = nullptr;
#endif

#if defined(NOCTURNE_UWP_LAUNCHER)
    const char *default_user = uwp_root.empty() ? nullptr : uwp_root.c_str();   // saves and logs stay in the app
#elif defined(NOCTURNE_USER_DIR_FROM_SDL)
    // SDL_GetPrefPath creates the directory and keeps no hold on the string.
    char *pref = SDL_GetPrefPath("NocturneDecomp", "Nocturne");
    const std::string sdl_user = pref != nullptr ? pref : "";
    SDL_free(pref);
    const char *default_user = sdl_user.c_str();
#elif defined(NOCTURNE_DEFAULT_USER_DIR)
    const char *default_user = NOCTURNE_DEFAULT_USER_DIR;
#else
    const char *default_user = nullptr;
#endif

    WatcomPathRoots roots;
    roots.data = pick(option(argc, argv, "--data-dir"), "NOCTURNE_DATA_DIR", default_data);
    roots.user = pick(option(argc, argv, "--user-dir"), "NOCTURNE_USER_DIR", default_user);
    // One directory given as both is the default arrangement with a new working
    // directory, and needs no overlay.
    if (roots.user == roots.data) roots.user.clear();

    if (!roots.user.empty()) {
        std::error_code ignored;
        std::filesystem::create_directories(roots.user, ignored);
    }
    watcom_set_path_roots(roots);

    if (!roots.data.empty() || !roots.user.empty()) {
        fprintf(stderr, "nocturne: data directory %s, user directory %s\n",
                roots.data.empty() ? "(working directory)" : roots.data.c_str(),
                roots.user.empty() ? "(the data directory)" : roots.user.c_str());
    }
}
