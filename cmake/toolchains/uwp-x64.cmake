# UWP / Xbox (Dev Mode) x64: clang-cl with the Microsoft linker, Ninja generator.
#
# Run from an environment set up for the UWP ("store") toolchain, e.g. vcvarsall.bat
# x64 uwp, so INCLUDE/LIB/LIBPATH point at the store variants of the CRT and SDK.
# Used by the xbox-uwp link workflow; see docs/xbox-uwp.md.
set(CMAKE_SYSTEM_NAME      Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT DEFINED CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER   clang-cl)
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER clang-cl)
endif()

# WINAPI_FAMILY_APP restricts the Windows headers to what a UWP app may call. -mmmx
# matches the other lanes (the decompiled MMX code). clang-cl handles MS-style inline
# asm blocks natively, so the Linux lane's -fasm-blocks has no equivalent here.
set(_NOCTURNE_TARGET_FLAGS "/DWINAPI_FAMILY=WINAPI_FAMILY_APP /D_WIN32_WINNT=0x0A00 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS -mmmx")
set(CMAKE_C_FLAGS_INIT   "${_NOCTURNE_TARGET_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${_NOCTURNE_TARGET_FLAGS} /EHsc")

# Only the umbrella library: kernel32.lib & co. are desktop-only and a UWP app that
# links them is rejected by the store checks.
set(CMAKE_C_STANDARD_LIBRARIES_INIT   "WindowsApp.lib")
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "WindowsApp.lib")
set(CMAKE_EXE_LINKER_FLAGS_INIT "/APPCONTAINER /SUBSYSTEM:WINDOWS /MANIFEST:NO")

set(NOCTURNE_UWP ON CACHE BOOL "Building for UWP" FORCE)
