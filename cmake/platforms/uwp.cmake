# UWP / Xbox Dev Mode (x64): clang-cl against the store CRT, OpenGL ES 3.0 through ANGLE.
# Selected by cmake/toolchains/uwp-x64.cmake. The contract is in cmake/platforms/README.md;
# the port's notes are in docs/xbox-uwp.md.

set(NOCTURNE_PLATFORM_SHIM_EXCLUDE "/win32/winsock_posix\\.c$")
# ws2_32 (sockets) and bcrypt (FFmpeg's random seed) are allowed to UWP apps but are not in
# WindowsApp.lib, which the toolchain file links. The extra objects are the WinRT entry shim and
# the launcher's C++/CX import providers, which need MSVC's cl /ZW and are built by the CI workflow.
set(NOCTURNE_UWP_EXTRA_OBJECTS "" CACHE STRING "Extra object files to link into the exe")
set(NOCTURNE_PLATFORM_LIBRARIES ${NOCTURNE_UWP_EXTRA_OBJECTS} ws2_32 bcrypt)
set(NOCTURNE_PLATFORM_LAUNCHERS OFF)
set(NOCTURNE_PACKAGE_FORMAT "zip")
set(NOCTURNE_GLES ON CACHE BOOL "OpenGL ES 3.0 (ANGLE)" FORCE)

# Prebuilt SDL2, SDL2_ttf, FFmpeg and ANGLE from CI rather than a source build.
set(NOCTURNE_DEPENDENCIES_FILE "${CMAKE_SOURCE_DIR}/cmake/uwp_libs.cmake")

# main.cpp is in an object library, so this must be set directory-wide, before any target exists.
add_compile_definitions(NOCTURNE_UWP_LAUNCHER=1)

# Hot pixel loops. The console build otherwise compiles without optimisation, and profiling the running
# game (docs/xbox-uwp.md) put most CPU time in the software lighting blends (core/dstrender) and in the
# pixel conversion and upload code (shims/gl, shims/renderer). Build just those at -O2, the way the
# project already does for its scanline rasterisers: -fwrapv is applied globally, -fno-strict-aliasing keeps
# the decompiler's pointer type-punning valid. Optimising the whole game broke input handling on the
# console, so it stays at the default everywhere else.
set(_PSEUDO "${CMAKE_SOURCE_DIR}/annotations/nocedit.exe/pseudocode")
file(GLOB _uwp_hot CONFIGURE_DEPENDS
    "${_PSEUDO}/src/core/dstrender.cpp/*.cpp"
    "${_PSEUDO}/shims/gl/*.cpp"
    "${_PSEUDO}/shims/renderer/*.cpp")
set_source_files_properties(${_uwp_hot} PROPERTIES
    COMPILE_OPTIONS "-O2;-fno-strict-aliasing"
    SKIP_PRECOMPILE_HEADERS ON)

function(nocturne_platform_finalize target)
    # The launcher (settings, adding the game files) runs before the game.
    set(_L "${CMAKE_SOURCE_DIR}/packaging/uwp/launcher/src")
    target_sources(${target} PRIVATE ${_L}/gfx.cpp ${_L}/app.cpp ${_L}/game_files.cpp ${_L}/imports.cpp
        ${_L}/settings_store.cpp ${_L}/launcher_main.cpp ${_L}/launcher_entry.cpp)
    target_include_directories(${target} PRIVATE ${_L}
        "${CMAKE_SOURCE_DIR}/annotations/nocedit.exe/pseudocode/shims")
    # A UWP app starts at WinMain (SDL's WinRT shell, linked in as an extra object), not a console main().
    set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE ON)
endfunction()
