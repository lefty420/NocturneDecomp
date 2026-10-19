# Builds SDL2_ttf (with its vendored FreeType + HarfBuzz) as a static library for UWP.
# SDL2 itself is built separately (VisualC-WinRT); here it is only an include path.
cmake_minimum_required(VERSION 3.20)
project(nocturne_uwp_ttf C CXX)

set(NOCTURNE_SDL_SRC "" CACHE PATH "SDL2 source tree (for headers)")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_library(SDL2_stub INTERFACE)
target_include_directories(SDL2_stub INTERFACE "${NOCTURNE_SDL_SRC}/include")
add_library(SDL2::SDL2 ALIAS SDL2_stub)
add_library(SDL2::SDL2-static ALIAS SDL2_stub)
set(SDL2_FOUND TRUE)
set(SDL2_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "" FORCE)

set(SDL2TTF_VENDORED ON  CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES  OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL  OFF CACHE BOOL "" FORCE)
add_subdirectory("${NOCTURNE_TTF_SRC}" sdl_ttf EXCLUDE_FROM_ALL)
