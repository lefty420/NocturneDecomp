#pragma once
// Reads and writes launcher.ini, the file the game's controller code reads at run time
// (shims/core/launcher_settings.*).

#include "core/launcher_settings.h"

#include <filesystem>

namespace nl {

NocturneLauncherSettings load_settings(const std::filesystem::path &file);
bool save_settings(const std::filesystem::path &file, const NocturneLauncherSettings &s);

}  // namespace nl
