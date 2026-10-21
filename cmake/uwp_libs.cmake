# uwp_libs.cmake — the UWP builds of SDL2, SDL2_ttf (+FreeType), FFmpeg and ANGLE's
# headers, taken from a directory the CI link workflow assembles from the earlier
# library builds. Replaces bundledlibs.cmake (which builds mingw/Linux versions from
# source) when NOCTURNE_UWP is on, and sets the same variables it does.
#
#   <NOCTURNE_UWP_LIBS_DIR>/sdl/{include/, inc/SDL2/, SDL2.lib, SDL2.dll}
#   <NOCTURNE_UWP_LIBS_DIR>/ttf/{SDL_ttf.h, SDL2_ttf.lib, freetype.lib}
#   <NOCTURNE_UWP_LIBS_DIR>/ffmpeg/{include/, lib/libav*.lib, lib/libsw*.lib}
#   <NOCTURNE_UWP_LIBS_DIR>/angle/{include/, libEGL.dll, libGLESv2.dll}
set(NOCTURNE_UWP_LIBS_DIR "" CACHE PATH "Directory holding the UWP builds of the third-party libraries")
if(NOT NOCTURNE_UWP_LIBS_DIR OR NOT IS_DIRECTORY "${NOCTURNE_UWP_LIBS_DIR}")
    message(FATAL_ERROR "NOCTURNE_UWP is on but NOCTURNE_UWP_LIBS_DIR is not a directory: '${NOCTURNE_UWP_LIBS_DIR}'")
endif()

# sdl/inc/SDL2 is a copy of the headers, for the shims that write <SDL2/SDL.h>.
set(SDL2_INCLUDE_DIRS "${NOCTURNE_UWP_LIBS_DIR}/sdl/include" "${NOCTURNE_UWP_LIBS_DIR}/sdl/inc")
set(SDL2_CFLAGS_OTHER "")
set(SDL2_LIBRARIES    "${NOCTURNE_UWP_LIBS_DIR}/sdl/SDL2.lib")

set(SDL2TTF_INCLUDE_DIRS "${NOCTURNE_UWP_LIBS_DIR}/ttf")
set(SDL2TTF_CFLAGS_OTHER "")
set(SDL2TTF_LIBRARIES    "${NOCTURNE_UWP_LIBS_DIR}/ttf/SDL2_ttf.lib" "${NOCTURNE_UWP_LIBS_DIR}/ttf/freetype.lib")

set(FFMPEG_INCLUDE_DIRS "${NOCTURNE_UWP_LIBS_DIR}/ffmpeg/include")
set(FFMPEG_CFLAGS_OTHER "")
set(FFMPEG_LIBRARIES
    "${NOCTURNE_UWP_LIBS_DIR}/ffmpeg/lib/libavformat.lib"
    "${NOCTURNE_UWP_LIBS_DIR}/ffmpeg/lib/libavcodec.lib"
    "${NOCTURNE_UWP_LIBS_DIR}/ffmpeg/lib/libswscale.lib"
    "${NOCTURNE_UWP_LIBS_DIR}/ffmpeg/lib/libswresample.lib"
    "${NOCTURNE_UWP_LIBS_DIR}/ffmpeg/lib/libavutil.lib")
set(NOCTURNE_FFMPEG_DEP "")

# ANGLE: only its headers are needed to compile; SDL's WinRT backend loads libEGL /
# libGLESv2 at run time, so those DLLs travel in the package, not the link line.
set(NOCTURNE_ANGLE_INCLUDE_DIR "${NOCTURNE_UWP_LIBS_DIR}/angle/include" CACHE PATH "" FORCE)
set(NOCTURNE_GLES ON CACHE BOOL "" FORCE)

# Objects built separately (the C++/CX WinRT entry shim needs MSVC's cl, which clang-cl cannot do).
set(NOCTURNE_UWP_EXTRA_OBJECTS "" CACHE STRING "Extra object files to link into the exe")
message(STATUS "UWP libraries from ${NOCTURNE_UWP_LIBS_DIR}")
