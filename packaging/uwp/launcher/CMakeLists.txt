# The launcher: SDL2 only (SDL_Renderer), no game code. Builds here for the Linux test
# harness (launcher_shot) and is linked into the UWP app by the shell project.
cmake_minimum_required(VERSION 3.16)
project(nocturne_launcher CXX)
set(CMAKE_CXX_STANDARD 17)

set(NOCTURNE_SDL_INCLUDE "" CACHE PATH "SDL2 include directory")
set(NOCTURNE_SDL_LIB "" CACHE FILEPATH "SDL2 library (Linux test build)")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(FONTS_CPP ${CMAKE_CURRENT_BINARY_DIR}/fonts_embedded.cpp)
add_custom_command(OUTPUT ${FONTS_CPP}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed.py ${FONTS_CPP}
        launcher_font_regular=${CMAKE_CURRENT_SOURCE_DIR}/assets/Poppins-Regular.ttf
        launcher_font_medium=${CMAKE_CURRENT_SOURCE_DIR}/assets/Poppins-Medium.ttf
        launcher_font_bold=${CMAKE_CURRENT_SOURCE_DIR}/assets/Poppins-Bold.ttf
    DEPENDS tools/embed.py assets/Poppins-Regular.ttf assets/Poppins-Medium.ttf assets/Poppins-Bold.ttf)

add_library(nocturne_launcher STATIC gamefiles.cpp settings.cpp importer.cpp gfx.cpp launcher.cpp ${FONTS_CPP})
target_include_directories(nocturne_launcher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${NOCTURNE_SDL_INCLUDE})

if(NOT MSVC)
    add_executable(launcher_shot shot.cpp)
    target_link_libraries(launcher_shot nocturne_launcher ${NOCTURNE_SDL_LIB} pthread)
endif()
