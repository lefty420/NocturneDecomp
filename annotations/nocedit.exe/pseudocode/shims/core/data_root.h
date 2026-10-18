#pragma once

// =============================================================================
// DATA ROOT — where the game's relative file paths live
// =============================================================================
//
// An addition, not a reconstruction. The game names its files relative to the
// working directory (".\\system\\nocturne.ini", "ACT1.POD", "save\\SAVE1.NOC").
// On Linux and desktop Windows that directory is where the player put the game.
//
// On UWP (Xbox Dev Mode included) it is the package's install folder, which is
// read-only, and a UWP app cannot change its working directory. The files a
// player copies in through Device Portal land in the app's local folder
// instead. So on UWP every relative path is resolved against that folder.
//
// Everywhere else the root is empty and nothing changes: nocturne_data_path()
// returns its argument unchanged.

#include <string>

// The directory relative paths are resolved against, with forward slashes and a
// trailing '/'. Empty when the working directory is the right place.
std::string nocturne_data_root();

// `path` with the data root prepended when `path` is relative; otherwise `path`
// as given. A leading "./" or ".\\" is dropped before the root is added. Never
// fails; a null `path` gives an empty string.
std::string nocturne_data_path(const char *path);
